#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

class Instance;
class CommandPool;

struct BufferCopyRegion {
    VkDeviceSize srcOffset = 0;
    VkDeviceSize dstOffset = 0;
    VkDeviceSize size = 0;
};

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;

    void* mapped = nullptr;

    void createBuffer(Instance* instance, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    void destroyBuffer(Instance* instance);
    void map(Instance* instance);
    void unmap(Instance* instance);
    void upload(Instance* instance, const void* srcData, VkDeviceSize dataSize);

    static void copyBuffer(
        Instance* instance,
        VkBuffer srcBuffer,
        VkBuffer dstBuffer,
        const std::vector<BufferCopyRegion>& regions,
        CommandPool pool
    );

    void cleanup(Instance* instance);
};