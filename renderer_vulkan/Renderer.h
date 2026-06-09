#pragma once

#include "common.h"
#include "RenderBox.h"

#include <vulkan/vulkan.h>
#include <glm/matrix.hpp>
#include <mutex>
#include <vector>

#define RENDERER_API __declspec(dllexport)

class Renderer
{
public:
    Renderer();
    ~Renderer();

    void init();
    void getImage(RenderImage* image);
    void getSliceImage(int index, RenderImage* image);
    void updateRenderParameters(RenderParameters newParam);
    void render(int mask);
    void resizeViewport(int viewIndex, int width, int height);
    void updateSlice(int index, glm::vec3 origin, glm::vec3 axisU, glm::vec3 axisV);
    void rotateCamera(float xDeg, float yDeg);
    void scaleCamera(float scaleFactor);
    void enableSnapshot() {}
    void disableSnapshot() {}

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        void* mapped = nullptr;
    };

    struct alignas(16) ComputeUniforms {
        glm::mat4 inverseViewProjection{ 1.0f };
        glm::vec4 volumeSize{ 0.0f };
        glm::vec4 origin{ 0.0f };
        glm::vec4 axisU{ 0.0f };
        glm::vec4 axisV{ 0.0f };
        glm::vec4 uvBounds{ 0.0f };
        glm::ivec4 dimensions{ 0 };
        glm::ivec4 viewportWindow{ 0 };
    };

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline mprPipeline = VK_NULL_HANDLE;
    VkPipeline volumePipeline = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSets[4]{};

    Buffer volumeBuffer;
    Buffer lutBuffer;
    Buffer outputBuffers[4];
    Buffer uniformBuffers[4];

    RenderImage image3D{};
    RenderImage sliceImage[3]{};
    RenderParameters renderParams{};
    RenderBox renderBox;
    SliceDesc sliceStates[3];
    AABB sliceUVBounds[3];
    int viewW[4]{};
    int viewH[4]{};

    glm::mat4 viewMatrix{ 1.0f };
    glm::mat4 projectMatrix{ 1.0f };
    std::mutex dataMutex;
    std::mutex canvasMutex[4];

    void createInstance();
    void createDevice();
    void createDescriptors();
    void createPipelines();
    void updateProjection();
    void updateDescriptorSet(int viewIndex);
    void dispatch(int viewIndex, VkPipeline pipeline);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);
    void destroyBuffer(Buffer& buffer);
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties);
};

extern "C" {
    RENDERER_API Renderer* CreateRenderer();
    RENDERER_API void DeleteRenderer(Renderer* p);
    RENDERER_API void Init(Renderer* p);
    RENDERER_API void SetUpRenderParameters(Renderer* p, uint16_t* volumeData, int width, int height, int depth, int windowWidth, int windowCenter, double spacing, double thickness);
    RENDERER_API void Render(Renderer* p, int mask);
    RENDERER_API void GetImage(Renderer* p, RenderImage* image);
    RENDERER_API void GetSliceImage(Renderer* p, int index, RenderImage* image);
    RENDERER_API void EnableSnapshot(Renderer* p);
    RENDERER_API void DisableSnapshot(Renderer* p);
    RENDERER_API void ResizeViewport(Renderer* p, int viewIndex, int width, int height);
    RENDERER_API void SetUpSliceState(Renderer* p, int index, Vec3 origin, Vec3 axisU, Vec3 axisV);
    RENDERER_API void RotateCamera(Renderer* p, float dx, float dy);
    RENDERER_API void ScaleCamera(Renderer* p, float scaleFactor);
}
