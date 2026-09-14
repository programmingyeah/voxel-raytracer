#pragma once

#include "buffer.hpp"
#include "../world/world_upload.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class Instance;
class CommandPool;

struct ComputePushConstants {
    glm::vec4 cameraPos;
    glm::vec4 cameraForward;
    glm::vec4 cameraRight;
    glm::vec4 cameraUp;
    glm::ivec4 worldMin;
    glm::ivec4 worldMax;
    glm::ivec4 chunkWindowDimensions;
    glm::vec4 renderParams;
};

struct VulkanAppBufferUpload {
    Buffer stagingBuffer{};
    VkBuffer destinationBuffer = VK_NULL_HANDLE;
    std::vector<BufferCopyRegion> regions;
};

struct VulkanAppFrameUploads {
    std::vector<VulkanAppBufferUpload> pending;
};

VkDeviceSize storageBufferSize(const std::vector<uint32_t>& data);
VkDeviceSize storageBufferSize(size_t wordCount);
std::vector<BufferCopyRegion> byteRegionsFromWordRegions(const std::vector<GpuBufferCopyRegion>& wordRegions);
void uploadBufferWithStaging(Instance& instance, CommandPool& commandPool, Buffer& destinationBuffer, const std::vector<uint32_t>& data);
void recordBufferCopies(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, const std::vector<BufferCopyRegion>& regions);
void insertImageBarrier(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask
);
std::string formatByteSize(VkDeviceSize bytes);
void checkVkResult(VkResult result);