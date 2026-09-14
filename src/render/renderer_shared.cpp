#include "renderer_shared.hpp"

#include "command_pool.hpp"
#include "instance.hpp"

#include <cstdio>
#include <stdexcept>

VkDeviceSize storageBufferSize(const std::vector<uint32_t>& data)
{
    return sizeof(uint32_t) * static_cast<VkDeviceSize>(data.empty() ? 1 : data.size());
}

VkDeviceSize storageBufferSize(size_t wordCount)
{
    return sizeof(uint32_t) * static_cast<VkDeviceSize>(wordCount == 0 ? 1 : wordCount);
}

std::vector<BufferCopyRegion> byteRegionsFromWordRegions(const std::vector<GpuBufferCopyRegion>& wordRegions)
{
    std::vector<BufferCopyRegion> byteRegions;
    byteRegions.reserve(wordRegions.size());

    for (const GpuBufferCopyRegion& region : wordRegions) {
        byteRegions.push_back({
            sizeof(uint32_t) * static_cast<VkDeviceSize>(region.srcWordOffset),
            sizeof(uint32_t) * static_cast<VkDeviceSize>(region.dstWordOffset),
            sizeof(uint32_t) * static_cast<VkDeviceSize>(region.wordCount)
        });
    }

    return byteRegions;
}

void uploadBufferWithStaging(Instance& instance, CommandPool& commandPool, Buffer& destinationBuffer, const std::vector<uint32_t>& data)
{
    if (data.empty()) {
        return;
    }

    const VkDeviceSize uploadSize = sizeof(uint32_t) * static_cast<VkDeviceSize>(data.size());

    Buffer stagingBuffer{};
    stagingBuffer.createBuffer(
        &instance,
        uploadSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    stagingBuffer.upload(&instance, data.data(), static_cast<size_t>(uploadSize));
    Buffer::copyBuffer(
        &instance,
        stagingBuffer.buffer,
        destinationBuffer.buffer,
        std::vector<BufferCopyRegion>{{0, 0, uploadSize}},
        commandPool
    );
    stagingBuffer.cleanup(&instance);
}

void recordBufferCopies(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, const std::vector<BufferCopyRegion>& regions)
{
    if (regions.empty()) {
        return;
    }

    std::vector<VkBufferCopy> vkRegions;
    vkRegions.reserve(regions.size());
    for (const BufferCopyRegion& region : regions) {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = region.srcOffset;
        copyRegion.dstOffset = region.dstOffset;
        copyRegion.size = region.size;
        vkRegions.push_back(copyRegion);
    }

    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
}

void insertImageBarrier(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStageMask,
        dstStageMask,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

std::string formatByteSize(VkDeviceSize bytes)
{
    static const char* units[] = {"B", "KiB", "MiB", "GiB"};
    double size = static_cast<double>(bytes);
    size_t unitIndex = 0;
    while (size >= 1024.0 && unitIndex + 1 < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unitIndex]);
    return buffer;
}

void checkVkResult(VkResult result)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error("ImGui Vulkan backend call failed");
    }
}