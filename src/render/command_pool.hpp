#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

class Instance;

class CommandPool {
public:
    void init(Instance* instance);
    VkCommandPool getHandle() const {return _pool; }
    void allocateCommandBuffers(Instance* instance, size_t count);
    VkCommandBuffer getBufferByIndex(size_t index) { return _buffers.at(index); };
    void resetBufferAtIndex(size_t index) const {vkResetCommandBuffer(_buffers.at(index), /*VkCommandBufferResetFlagBits*/ 0); };

    void cleanup(Instance* instance);
private:
    VkCommandPool _pool;
    std::vector<VkCommandBuffer> _buffers;
};