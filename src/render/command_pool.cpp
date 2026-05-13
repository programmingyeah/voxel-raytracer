#include "command_pool.hpp"
#include "instance.hpp"
#include "utils.hpp"
#include <stdexcept>

void CommandPool::init(Instance* instance) {
    QueueFamilyIndices queueFamilyIndices = instance->findQueueFamilies(instance->physicalDevice());

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(instance->device(), &poolInfo, nullptr, &_pool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
}

void CommandPool::allocateCommandBuffers(Instance* instance, size_t count) {
    //note that buffer indices are NOT returned, they need to be kept track of outside the class, fix in the future once more command buffers are needed (p distant future..)
    const size_t oldSize = _buffers.size();
    _buffers.resize(oldSize + count);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = _pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(count);

    if (vkAllocateCommandBuffers(instance->device(), &allocInfo, _buffers.data() + oldSize) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void CommandPool::cleanup(Instance* instance) {
    vkDestroyCommandPool(instance->device(), _pool, nullptr);
}