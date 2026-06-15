#define NOMINMAX
#include "Renderer.h"
#include "TransferFuction.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <Windows.h>

namespace {
constexpr VkFormat ColorFormat = VK_FORMAT_B8G8R8A8_UNORM;
constexpr VkFormat DepthFormat = VK_FORMAT_D32_SFLOAT;

std::vector<char> readBinary(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open shader: " + path.string());
    std::vector<char> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(bytes.data(), bytes.size());
    return bytes;
}

std::filesystem::path moduleDirectory()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&moduleDirectory), &module);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

void ensureImageBuffer(RenderImage& image, int width, int height)
{
    const int length = width * height * 4;
    if (image.length != length) {
        free(image.front);
        free(image.back);
        image.front = malloc(length);
        image.back = malloc(length);
        image.length = length;
    }
    image.width = width;
    image.height = height;
}

}

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    if (device) vkDeviceWaitIdle(device);
    for (auto& view : views) destroyViewResources(view);
    for (auto& buffer : uniformBuffers) destroyBuffer(buffer);
    destroyBuffer(vertexBuffer);
    destroyBuffer(indexBuffer);
    destroyBuffer(mprVertexBuffer);
    destroyImage(volumeImage);
    destroyImage(lutImage);
    if (volumeSampler) vkDestroySampler(device, volumeSampler, nullptr);
    if (lutSampler) vkDestroySampler(device, lutSampler, nullptr);
    if (mprPipeline) vkDestroyPipeline(device, mprPipeline, nullptr);
    if (volumePipeline) vkDestroyPipeline(device, volumePipeline, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (descriptorSetLayout) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
    if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
    if (device) vkDestroyDevice(device, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    free(image3D.front);
    free(image3D.back);
    for (auto& image : sliceImage) {
        free(image.front);
        free(image.back);
    }
}

void Renderer::init()
{
    createInstance();
    createDevice();
    createRenderPass();
    createDescriptors();
    createSamplers();
    createGeometry();
    createPipelines();
}

void Renderer::createInstance()
{
    VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "renderer_vulkan";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    info.pApplicationInfo = &app;
    if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS) throw std::runtime_error("vkCreateInstance failed");
}

void Renderer::createDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (!count) throw std::runtime_error("No Vulkan physical device");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    physicalDevice = devices[0];

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
    bool found = false;
    for (uint32_t i = 0; i < familyCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamily = i;
            found = true;
            break;
        }
    }
    if (!found) throw std::runtime_error("No Vulkan graphics queue");

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS) throw std::runtime_error("vkCreateDevice failed");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.queueFamilyIndex = queueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) throw std::runtime_error("vkCreateCommandPool failed");
}

uint32_t Renderer::findMemoryType(uint32_t bits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) return i;
    }
    throw std::runtime_error("No compatible Vulkan memory type");
}

void Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer, VkMemoryPropertyFlags properties)
{
    destroyBuffer(buffer);
    buffer.size = size;
    VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &info, nullptr, &buffer.buffer) != VK_SUCCESS) throw std::runtime_error("vkCreateBuffer failed");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer.buffer, &requirements);
    VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);
    if (vkAllocateMemory(device, &allocation, nullptr, &buffer.memory) != VK_SUCCESS) throw std::runtime_error("vkAllocateMemory failed");
    vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0);
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) vkMapMemory(device, buffer.memory, 0, size, 0, &buffer.mapped);
}

void Renderer::destroyBuffer(Buffer& buffer)
{
    if (!device) return;
    if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
    if (buffer.buffer) vkDestroyBuffer(device, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    buffer = {};
}

void Renderer::createImage(uint32_t width, uint32_t height, uint32_t depth, VkImageType type, VkFormat format,
    VkImageUsageFlags usage, VkImageAspectFlags aspect, Image& image)
{
    destroyImage(image);
    image.format = format;
    VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.imageType = type;
    info.format = format;
    info.extent = { width, height, depth };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &info, nullptr, &image.image) != VK_SUCCESS) throw std::runtime_error("vkCreateImage failed");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image.image, &requirements);
    VkMemoryAllocateInfo allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &allocation, nullptr, &image.memory) != VK_SUCCESS) throw std::runtime_error("vkAllocateMemory image failed");
    vkBindImageMemory(device, image.image, image.memory, 0);

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = image.image;
    viewInfo.viewType = type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D : (type == VK_IMAGE_TYPE_1D ? VK_IMAGE_VIEW_TYPE_1D : VK_IMAGE_VIEW_TYPE_2D);
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &image.view) != VK_SUCCESS) throw std::runtime_error("vkCreateImageView failed");
}

void Renderer::destroyImage(Image& image)
{
    if (!device) return;
    if (image.view) vkDestroyImageView(device, image.view, nullptr);
    if (image.image) vkDestroyImage(device, image.image, nullptr);
    if (image.memory) vkFreeMemory(device, image.memory, nullptr);
    image = {};
}

VkCommandBuffer Renderer::beginCommands()
{
    VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc.commandPool = commandPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer command{};
    vkAllocateCommandBuffers(device, &alloc, &command);
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command, &begin);
    return command;
}

void Renderer::endCommands(VkCommandBuffer command)
{
    vkEndCommandBuffer(command);
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, commandPool, 1, &command);
}

void Renderer::uploadImage(const void* data, VkDeviceSize size, uint32_t width, uint32_t height, uint32_t depth, Image& image)
{
    Buffer staging;
    createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging);
    memcpy(staging.mapped, data, size);

    VkCommandBuffer command = beginCommands();
    VkImageMemoryBarrier toTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image.image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { width, height, depth };
    vkCmdCopyBufferToImage(command, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier toShader = toTransfer;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);
    endCommands(command);
    destroyBuffer(staging);
}

void Renderer::createRenderPass()
{
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = ColorFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    attachments[1].format = DepthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depth{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;

    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    VkRenderPassCreateInfo info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = dependencies;
    if (vkCreateRenderPass(device, &info, nullptr, &renderPass) != VK_SUCCESS) throw std::runtime_error("vkCreateRenderPass failed");
}

void Renderer::createDescriptors()
{
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bindings[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bindings[2] = { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) throw std::runtime_error("descriptor layout failed");

    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 }
    };
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = 4;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) throw std::runtime_error("descriptor pool failed");
    VkDescriptorSetLayout layouts[] = { descriptorSetLayout, descriptorSetLayout, descriptorSetLayout, descriptorSetLayout };
    VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 4;
    alloc.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(device, &alloc, descriptorSets) != VK_SUCCESS) throw std::runtime_error("descriptor allocation failed");
    for (auto& uniform : uniformBuffers) createBuffer(sizeof(ShaderUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform);
}

void Renderer::createSamplers()
{
    VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0.0f;
    if (vkCreateSampler(device, &info, nullptr, &volumeSampler) != VK_SUCCESS) throw std::runtime_error("volume sampler failed");
    if (vkCreateSampler(device, &info, nullptr, &lutSampler) != VK_SUCCESS) throw std::runtime_error("lut sampler failed");
}

void Renderer::createGeometry()
{
    const float mprVertices[] = {
        -1.0f,  1.0f, 0.0f, -1.0f, -1.0f, 0.0f,  1.0f,  1.0f, 0.0f,
         1.0f,  1.0f, 0.0f, -1.0f, -1.0f, 0.0f,  1.0f, -1.0f, 0.0f
    };
    createBuffer(sizeof(mprVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mprVertexBuffer);
    memcpy(mprVertexBuffer.mapped, mprVertices, sizeof(mprVertices));
}

void Renderer::createPipelines()
{
    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) throw std::runtime_error("pipeline layout failed");

    const auto shaderDirectory = moduleDirectory() / "shaders";
    auto makeModule = [&](const char* name) {
        const auto code = readBinary(shaderDirectory / name);
        VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module{};
        if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) throw std::runtime_error("shader module failed");
        return module;
    };
    auto makePipeline = [&](const char* vertexName, const char* fragmentName, bool depthEnabled, VkPipeline& pipeline) {
        VkShaderModule vertex = makeModule(vertexName);
        VkShaderModule fragment = makeModule(fragmentName);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr };
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment, "main", nullptr };
        VkVertexInputBindingDescription binding{ 0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX };
        VkVertexInputAttributeDescription attribute{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
        VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 1;
        vertexInput.pVertexAttributeDescriptions = &attribute;
        VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depth.depthTestEnable = depthEnabled;
        depth.depthWriteEnable = depthEnabled;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;
        VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamics;
        VkGraphicsPipelineCreateInfo info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout;
        info.renderPass = renderPass;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) throw std::runtime_error("graphics pipeline failed");
        vkDestroyShaderModule(device, vertex, nullptr);
        vkDestroyShaderModule(device, fragment, nullptr);
    };
    makePipeline("volume.vert.spv", "volume.frag.spv", true, volumePipeline);
    makePipeline("mpr.vert.spv", "mpr.frag.spv", false, mprPipeline);
}

void Renderer::updateProjection()
{
    if (viewW[3] <= 0 || viewH[3] <= 0 || renderParams.width <= 0) return;
    const float aspect = float(viewW[3]) / float(viewH[3]);
    const float maxSide = std::max({ float(renderParams.width * renderParams.spacingX), float(renderParams.height * renderParams.spacingY), float(renderParams.depth * renderParams.spacingZ) });
    const float radius = maxSide * 0.6f;
    projectMatrix = aspect >= 1.0f
        ? glm::ortho(-radius * aspect, radius * aspect, -radius, radius, -radius * 4.0f, radius * 4.0f)
        : glm::ortho(-radius, radius, -radius / aspect, radius / aspect, -radius * 4.0f, radius * 4.0f);
}

void Renderer::updateRenderParameters(RenderParameters params)
{
    std::lock_guard lock(dataMutex);
    vkDeviceWaitIdle(device);
    renderParams = params;
    const float x = float(params.width * params.spacingX);
    const float y = float(params.height * params.spacingY);
    const float z = float(params.depth * params.spacingZ);
    renderBox = RenderBox(x, y, z);
    modelMatrix = glm::mat4(1.0f);
    viewMatrix = glm::lookAt(glm::vec3(0, -y, 0), glm::vec3(), glm::vec3(0, 0, 1));
    updateProjection();

    createBuffer(BOX_VERTEX_COUNT * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer);
    memcpy(vertexBuffer.mapped, renderBox.getVertices(), vertexBuffer.size);
    createBuffer(BOX_INDEX_COUNT * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer);
    memcpy(indexBuffer.mapped, renderBox.getIndices(), indexBuffer.size);

    createImage(params.width, params.height, params.depth, VK_IMAGE_TYPE_3D, VK_FORMAT_R16_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, volumeImage);
    uploadImage(params.volumeData, VkDeviceSize(params.width) * params.height * params.depth * sizeof(uint16_t),
        params.width, params.height, params.depth, volumeImage);

    const auto lut = TransferFuction::BuildRGBA_LUT();
    createImage(static_cast<uint32_t>(lut.size()), 1, 1, VK_IMAGE_TYPE_1D, VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, lutImage);
    uploadImage(lut.data(), sizeof(lut), static_cast<uint32_t>(lut.size()), 1, 1, lutImage);
}

void Renderer::destroyViewResources(ViewResources& view)
{
    if (!device) return;
    if (view.framebuffer) vkDestroyFramebuffer(device, view.framebuffer, nullptr);
    destroyImage(view.color);
    destroyImage(view.depth);
    destroyBuffer(view.readback);
    view = {};
}

void Renderer::resizeViewport(int index, int width, int height)
{
    if (index < 0 || index >= 4 || width <= 0 || height <= 0) return;
    std::lock_guard lock(dataMutex);
    vkDeviceWaitIdle(device);
    viewW[index] = width;
    viewH[index] = height;
    destroyViewResources(views[index]);
    createImage(width, height, 1, VK_IMAGE_TYPE_2D, ColorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT, views[index].color);
    createImage(width, height, 1, VK_IMAGE_TYPE_2D, DepthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, views[index].depth);
    VkImageView attachments[] = { views[index].color.view, views[index].depth.view };
    VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    info.renderPass = renderPass;
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.width = width;
    info.height = height;
    info.layers = 1;
    if (vkCreateFramebuffer(device, &info, nullptr, &views[index].framebuffer) != VK_SUCCESS) throw std::runtime_error("framebuffer failed");
    createBuffer(VkDeviceSize(width) * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, views[index].readback);
    updateProjection();
}

void Renderer::updateDescriptorSet(int index)
{
    VkDescriptorImageInfo volume{ volumeSampler, volumeImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo lut{ lutSampler, lutImage.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorBufferInfo uniforms{ uniformBuffers[index].buffer, 0, sizeof(ShaderUniforms) };
    VkWriteDescriptorSet writes[3]{};
    writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSets[index], 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &volume, nullptr, nullptr };
    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSets[index], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &lut, nullptr, nullptr };
    writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSets[index], 2, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uniforms, nullptr };
    vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
}

void Renderer::drawView(int index, VkPipeline pipeline)
{
    VkCommandBuffer command = beginCommands();
    VkClearValue clears[2]{};
    clears[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    clears[1].depthStencil = { 1.0f, 0 };
    VkRenderPassBeginInfo begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    begin.renderPass = renderPass;
    begin.framebuffer = views[index].framebuffer;
    begin.renderArea.extent = { static_cast<uint32_t>(viewW[index]), static_cast<uint32_t>(viewH[index]) };
    begin.clearValueCount = 2;
    begin.pClearValues = clears;
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[index], 0, nullptr);
    VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(viewW[index]), static_cast<float>(viewH[index]), 0.0f, 1.0f };
    VkRect2D scissor{ { 0, 0 }, { static_cast<uint32_t>(viewW[index]), static_cast<uint32_t>(viewH[index]) } };
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    VkDeviceSize offset = 0;
    if (index == 3) {
        vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer.buffer, &offset);
        vkCmdBindIndexBuffer(command, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, BOX_INDEX_COUNT, 1, 0, 0, 0);
    } else {
        vkCmdBindVertexBuffers(command, 0, 1, &mprVertexBuffer.buffer, &offset);
        vkCmdDraw(command, 6, 1, 0, 0);
    }
    vkCmdEndRenderPass(command);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { static_cast<uint32_t>(viewW[index]), static_cast<uint32_t>(viewH[index]), 1 };
    vkCmdCopyImageToBuffer(command, views[index].color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, views[index].readback.buffer, 1, &copy);
    endCommands(command);
}

void Renderer::copyViewToCpu(int index)
{
    std::lock_guard canvasLock(canvasMutex[index]);
    RenderImage& image = index == 3 ? image3D : sliceImage[index];
    ensureImageBuffer(image, viewW[index], viewH[index]);
    memcpy(image.back, views[index].readback.mapped, image.length);
    std::swap(image.front, image.back);
}

void Renderer::render(int mask)
{
    if (!volumeImage.image || !lutImage.image || !vertexBuffer.buffer) return;
    std::lock_guard lock(dataMutex);
    for (int i = 0; i < 4; ++i) {
        if (!(mask & (1 << i)) || !views[i].framebuffer) continue;
        ShaderUniforms uniforms{};
        uniforms.model = modelMatrix;
        uniforms.view = viewMatrix;
        uniforms.projection = projectMatrix;
        uniforms.volumePhysicalSize = glm::vec4(float(renderParams.width * renderParams.spacingX), float(renderParams.height * renderParams.spacingY), float(renderParams.depth * renderParams.spacingZ), 0);
        uniforms.viewportWindow = { viewW[i], viewH[i], renderParams.windowCenter, renderParams.windowWidth };
        const int maxSteps = int(std::sqrt(
            uniforms.volumePhysicalSize.x * uniforms.volumePhysicalSize.x +
            uniforms.volumePhysicalSize.y * uniforms.volumePhysicalSize.y +
            uniforms.volumePhysicalSize.z * uniforms.volumePhysicalSize.z));
        uniforms.dimensions = { renderParams.width, renderParams.height, renderParams.depth, maxSteps };
        if (i == 3) {
            const glm::vec3 ray = glm::normalize(glm::vec3(glm::inverse(viewMatrix) * glm::vec4(0, 0, -1, 0)));
            uniforms.viewRay = glm::vec4(ray, 0);
        } else {
            uniforms.origin = glm::vec4(sliceStates[i].origin, 0);
            uniforms.axisU = glm::vec4(sliceStates[i].axisU, 0);
            uniforms.axisV = glm::vec4(sliceStates[i].axisV, 0);
            uniforms.sliceMapping = { sliceStates[i].mapping.centerU, sliceStates[i].mapping.centerV, sliceStates[i].mapping.halfU, sliceStates[i].mapping.halfV };
        }
        memcpy(uniformBuffers[i].mapped, &uniforms, sizeof(uniforms));
        updateDescriptorSet(i);
        drawView(i, i == 3 ? volumePipeline : mprPipeline);
        copyViewToCpu(i);
    }
}

void Renderer::getImage(RenderImage* image)
{
    if (image && image3D.width) *image = { image3D.width, image3D.height, image3D.front, nullptr, image3D.length };
}

void Renderer::getSliceImage(int index, RenderImage* image)
{
    if (image && index >= 0 && index < 3 && sliceImage[index].width) *image = { sliceImage[index].width, sliceImage[index].height, sliceImage[index].front, nullptr, sliceImage[index].length };
}

void Renderer::updateSlice(int index, glm::vec3 origin, glm::vec3 u, glm::vec3 v, SliceDisplayMapping mapping)
{
    sliceStates[index] = { origin, u, v, mapping };
}

void Renderer::rotateCamera(float x, float y)
{
    glm::mat4 camera = glm::inverse(viewMatrix);
    const glm::vec3 up = glm::normalize(glm::vec3(camera[1][0], camera[1][1], camera[1][2]));
    camera = glm::rotate(glm::mat4(1.0f), x, up) * camera;
    const glm::vec3 right = glm::normalize(glm::vec3(camera[0][0], camera[0][1], camera[0][2]));
    camera = glm::rotate(glm::mat4(1.0f), y, right) * camera;
    viewMatrix = glm::inverse(camera);
}

void Renderer::scaleCamera(float) {}

RENDERER_API Renderer* CreateRenderer() { return new Renderer(); }
RENDERER_API void DeleteRenderer(Renderer* p) { delete p; }
RENDERER_API void Init(Renderer* p) { p->init(); }
RENDERER_API void Render(Renderer* p, int mask) { p->render(mask); }
RENDERER_API void GetImage(Renderer* p, RenderImage* image) { p->getImage(image); }
RENDERER_API void GetSliceImage(Renderer* p, int index, RenderImage* image) { p->getSliceImage(index, image); }
RENDERER_API void EnableSnapshot(Renderer* p) { p->enableSnapshot(); }
RENDERER_API void DisableSnapshot(Renderer* p) { p->disableSnapshot(); }
RENDERER_API void ResizeViewport(Renderer* p, int index, int width, int height) { p->resizeViewport(index, width, height); }
RENDERER_API void SetUpRenderParameters(Renderer* p, uint16_t* data, int width, int height, int depth, int windowWidth, int windowCenter, double spacing, double thickness) { p->updateRenderParameters({ width, height, depth, data, spacing, spacing, thickness, windowCenter, windowWidth }); }
RENDERER_API void SetUpSliceState(Renderer* p, int index, Vec3 origin, Vec3 u, Vec3 v, SliceDisplayMapping mapping) { p->updateSlice(index, { origin.x, origin.y, origin.z }, glm::normalize(glm::vec3(u.x, u.y, u.z)), glm::normalize(glm::vec3(v.x, v.y, v.z)), mapping); }
RENDERER_API void RotateCamera(Renderer* p, float dx, float dy) { p->rotateCamera(-glm::radians(dx), glm::radians(dy)); }
RENDERER_API void ScaleCamera(Renderer* p, float scale) { p->scaleCamera(scale); }
