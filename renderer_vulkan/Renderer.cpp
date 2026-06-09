#define NOMINMAX
#include "Renderer.h"
#include "TransferFuction.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <Windows.h>

namespace {
std::vector<char> readBinary(const char* path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) throw std::runtime_error(std::string("Cannot open shader: ") + path);
    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> bytes(size);
    file.seekg(0);
    file.read(bytes.data(), size);
    return bytes;
}

std::filesystem::path moduleDirectory()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&moduleDirectory),
        &module);
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

std::vector<glm::vec3> intersectSliceWithBox(const SliceDesc& slice, RenderBox box)
{
    const glm::vec3 normal = glm::normalize(glm::cross(slice.axisU, slice.axisV));
    std::vector<glm::vec3> points;
    for (const auto& edge : box.getEdges()) {
        const float da = glm::dot(edge.p1 - slice.origin, normal);
        const float db = glm::dot(edge.p2 - slice.origin, normal);
        if (da * db <= 0.0f && std::abs(da - db) > 1e-6f) {
            points.push_back(edge.p1 + (da / (da - db)) * (edge.p2 - edge.p1));
        }
    }
    return points;
}
}

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    if (device) vkDeviceWaitIdle(device);
    for (auto& buffer : outputBuffers) destroyBuffer(buffer);
    for (auto& buffer : uniformBuffers) destroyBuffer(buffer);
    destroyBuffer(volumeBuffer);
    destroyBuffer(lutBuffer);
    if (mprPipeline) vkDestroyPipeline(device, mprPipeline, nullptr);
    if (volumePipeline) vkDestroyPipeline(device, volumePipeline, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (descriptorSetLayout) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
    if (device) vkDestroyDevice(device, nullptr);
    if (instance) vkDestroyInstance(instance, nullptr);
    free(image3D.front); free(image3D.back);
    for (auto& image : sliceImage) { free(image.front); free(image.back); }
}

void Renderer::init()
{
    createInstance();
    createDevice();
    createDescriptors();
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
    for (uint32_t i = 0; i < familyCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { queueFamily = i; break; }
    }
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
    vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
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

void Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer)
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
    allocation.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device, &allocation, nullptr, &buffer.memory);
    vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0);
    vkMapMemory(device, buffer.memory, 0, size, 0, &buffer.mapped);
}

void Renderer::destroyBuffer(Buffer& buffer)
{
    if (!device) return;
    if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
    if (buffer.buffer) vkDestroyBuffer(device, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    buffer = {};
}

void Renderer::createDescriptors()
{
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (uint32_t i = 0; i < 3; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);

    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 12 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 }
    };
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = 4; poolInfo.poolSizeCount = 2; poolInfo.pPoolSizes = sizes;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
    VkDescriptorSetLayout layouts[] = { descriptorSetLayout, descriptorSetLayout, descriptorSetLayout, descriptorSetLayout };
    VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    alloc.descriptorPool = descriptorPool; alloc.descriptorSetCount = 4; alloc.pSetLayouts = layouts;
    vkAllocateDescriptorSets(device, &alloc, descriptorSets);
    for (auto& uniform : uniformBuffers) createBuffer(sizeof(ComputeUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform);
}

void Renderer::createPipelines()
{
    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 1; layoutInfo.pSetLayouts = &descriptorSetLayout;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);
    auto makePipeline = [&](const char* path, VkPipeline& pipeline) {
        const auto code = readBinary(path);
        VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        moduleInfo.codeSize = code.size();
        moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module{};
        vkCreateShaderModule(device, &moduleInfo, nullptr, &module);
        VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        info.layout = pipelineLayout;
        info.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; info.stage.module = module; info.stage.pName = "main";
        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) throw std::runtime_error("Compute pipeline creation failed");
        vkDestroyShaderModule(device, module, nullptr);
    };
    const auto shaderDirectory = moduleDirectory() / "shaders";
    makePipeline((shaderDirectory / "mpr.comp.spv").string().c_str(), mprPipeline);
    makePipeline((shaderDirectory / "volume.comp.spv").string().c_str(), volumePipeline);
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
    renderParams = params;
    const float x = float(params.width * params.spacingX), y = float(params.height * params.spacingY), z = float(params.depth * params.spacingZ);
    renderBox = RenderBox(x, y, z);
    viewMatrix = glm::lookAt(glm::vec3(0, -y, 0), glm::vec3(), glm::vec3(0, 0, 1));
    updateProjection();

    const size_t voxelCount = size_t(params.width) * params.height * params.depth;
    std::vector<uint32_t> volume(voxelCount);
    for (size_t i = 0; i < voxelCount; ++i) volume[i] = params.volumeData[i];
    createBuffer(volume.size() * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, volumeBuffer);
    memcpy(volumeBuffer.mapped, volume.data(), volumeBuffer.size);

    const auto lut = TransferFuction::BuildRGBA_LUT();
    createBuffer(sizeof(lut), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, lutBuffer);
    memcpy(lutBuffer.mapped, lut.data(), lutBuffer.size);
}

void Renderer::resizeViewport(int index, int width, int height)
{
    viewW[index] = width; viewH[index] = height;
    createBuffer(VkDeviceSize(width) * height * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, outputBuffers[index]);
    updateProjection();
}

void Renderer::updateDescriptorSet(int index)
{
    VkDescriptorBufferInfo infos[] = {
        { volumeBuffer.buffer, 0, volumeBuffer.size },
        { lutBuffer.buffer, 0, lutBuffer.size },
        { outputBuffers[index].buffer, 0, outputBuffers[index].size },
        { uniformBuffers[index].buffer, 0, uniformBuffers[index].size }
    };
    VkWriteDescriptorSet writes[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        writes[i].dstSet = descriptorSets[index]; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 3 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
}

void Renderer::dispatch(int index, VkPipeline pipeline)
{
    VkCommandBufferAllocateInfo alloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc.commandPool = commandPool; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
    VkCommandBuffer command{};
    vkAllocateCommandBuffers(device, &alloc, &command);
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command, &begin);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSets[index], 0, nullptr);
    vkCmdDispatch(command, (viewW[index] + 15) / 16, (viewH[index] + 15) / 16, 1);
    vkEndCommandBuffer(command);
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, commandPool, 1, &command);
}

void Renderer::render(int mask)
{
    if (!volumeBuffer.buffer || !lutBuffer.buffer) return;
    std::lock_guard lock(dataMutex);
    for (int i = 0; i < 4; ++i) {
        if (!(mask & (1 << i)) || viewW[i] <= 0 || viewH[i] <= 0 || !outputBuffers[i].buffer) continue;
        ComputeUniforms uniforms{};
        uniforms.inverseViewProjection = glm::inverse(projectMatrix * viewMatrix);
        uniforms.volumeSize = glm::vec4(float(renderParams.width * renderParams.spacingX), float(renderParams.height * renderParams.spacingY), float(renderParams.depth * renderParams.spacingZ), 0);
        uniforms.dimensions = { renderParams.width, renderParams.height, renderParams.depth, 0 };
        uniforms.viewportWindow = { viewW[i], viewH[i], renderParams.windowCenter, renderParams.windowWidth };
        if (i < 3) {
            uniforms.origin = glm::vec4(sliceStates[i].origin, 0);
            uniforms.axisU = glm::vec4(sliceStates[i].axisU, 0);
            uniforms.axisV = glm::vec4(sliceStates[i].axisV, 0);
            uniforms.uvBounds = { sliceUVBounds[i].min.x, sliceUVBounds[i].max.x, sliceUVBounds[i].min.y, sliceUVBounds[i].max.y };
        }
        memcpy(uniformBuffers[i].mapped, &uniforms, sizeof(uniforms));
        updateDescriptorSet(i);
        dispatch(i, i == 3 ? volumePipeline : mprPipeline);
        std::lock_guard canvasLock(canvasMutex[i]);
        RenderImage& image = i == 3 ? image3D : sliceImage[i];
        ensureImageBuffer(image, viewW[i], viewH[i]);
        memcpy(image.back, outputBuffers[i].mapped, image.length);
        std::swap(image.front, image.back);
    }
}

void Renderer::getImage(RenderImage* image) { if (image) *image = { image3D.width, image3D.height, image3D.front, nullptr, image3D.length }; }
void Renderer::getSliceImage(int index, RenderImage* image) { if (image && index >= 0 && index < 3) *image = { sliceImage[index].width, sliceImage[index].height, sliceImage[index].front, nullptr, sliceImage[index].length }; }

void Renderer::updateSlice(int index, glm::vec3 origin, glm::vec3 u, glm::vec3 v)
{
    sliceStates[index] = { origin, u, v };
    std::vector<glm::vec3> uv;
    for (const auto& point : intersectSliceWithBox(sliceStates[index], renderBox)) {
        uv.emplace_back(glm::dot(point - origin, u), glm::dot(point - origin, v), 0);
    }
    sliceUVBounds[index] = AABB::generateAABB(uv);
}

void Renderer::rotateCamera(float x, float y)
{
    glm::mat4 camera = glm::inverse(viewMatrix);
    camera = glm::rotate(glm::mat4(1), x, glm::normalize(glm::vec3(camera[1]))) * camera;
    camera = glm::rotate(glm::mat4(1), y, glm::normalize(glm::vec3(camera[0]))) * camera;
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
RENDERER_API void SetUpSliceState(Renderer* p, int index, Vec3 origin, Vec3 u, Vec3 v) { p->updateSlice(index, { origin.x, origin.y, origin.z }, glm::normalize(glm::vec3(u.x, u.y, u.z)), glm::normalize(glm::vec3(v.x, v.y, v.z))); }
RENDERER_API void RotateCamera(Renderer* p, float dx, float dy) { p->rotateCamera(-glm::radians(dx), glm::radians(dy)); }
RENDERER_API void ScaleCamera(Renderer* p, float scale) { p->scaleCamera(scale); }
