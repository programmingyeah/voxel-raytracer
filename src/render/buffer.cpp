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
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(instance->device(), buffer, nullptr);
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(instance->device(), memory, nullptr);
    }
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    mapped = nullptr;
    size = 0;
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

void Buffer::copyBuffer(
    Instance* instance,
    VkBuffer srcBuffer,
    VkBuffer dstBuffer,
    const std::vector<BufferCopyRegion>& regions,
    CommandPool pool
) {
    if (regions.empty()) {
        return;
    }

    VkCommandBuffer commandBuffer = instance->beginSingleTimeCommands(pool);
    std::vector<VkBufferCopy> vkRegions;
    vkRegions.reserve(regions.size());

    for (const BufferCopyRegion& region : regions) {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = region.srcOffset;
        copyRegion.dstOffset = region.dstOffset;
        copyRegion.size = region.size;
        vkRegions.push_back(copyRegion);
    }

    vkCmdCopyBuffer(
        commandBuffer,
        srcBuffer,
        dstBuffer,
        static_cast<uint32_t>(vkRegions.size()),
        vkRegions.data()
    );

    instance->endSingleTimeCommands(pool, commandBuffer);
}

void Buffer::cleanup(Instance* instance) {
    destroyBuffer(instance);
}