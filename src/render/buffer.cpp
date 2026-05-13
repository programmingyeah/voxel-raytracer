#include "buffer.hpp"
#include "command_pool.hpp"
#include "instance.hpp"
#include <stdexcept>
#include <cstring>

void Buffer::createBuffer(Instance* instance, VkDeviceSize bufferSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    size = bufferSize;

    if (vkCreateBuffer(instance->device(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(instance->device(), buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = instance->findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(instance->device(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(instance->device(), buffer, memory, 0);
}

void Buffer::destroyBuffer(Instance* instance) {
    vkDestroyBuffer(instance->device(), buffer, nullptr);
    vkFreeMemory(instance->device(), memory, nullptr);
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    mapped = nullptr;
}

void Buffer::map(Instance* instance) {
    vkMapMemory(instance->device(), memory, 0, size, 0, &mapped);
}

void Buffer::unmap(Instance* instance) {
    vkUnmapMemory(instance->device(), memory);
    mapped = nullptr;
}

void Buffer::upload(Instance* instance, const void* srcData, VkDeviceSize dataSize) {
    map(instance);
    std::memcpy(mapped, srcData, static_cast<size_t>(dataSize));
    unmap(instance);
}

void Buffer::copyBuffer(Instance* instance, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, CommandPool pool) {
    VkCommandBuffer commandBuffer = instance->beginSingleTimeCommands(pool);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0; // Optional
    copyRegion.dstOffset = 0; // Optional
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    instance->endSingleTimeCommands(pool, commandBuffer);
}

void Buffer::cleanup(Instance* instance) {
    vkDestroyBuffer(instance->device(), buffer, nullptr);
    vkFreeMemory(instance->device(), memory, nullptr);
}