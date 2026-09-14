#include "renderer.hpp"

#include "renderer_shared.hpp"
#include "../world/gpu_world_layout.hpp"

#include <stdexcept>

void VulkanApp::createComputeImages() {
    if (swapchain.getSwapImgFormat() != VK_FORMAT_R8G8B8A8_UNORM) {
        throw std::runtime_error("compute path currently requires VK_FORMAT_R8G8B8A8_UNORM swapchain images");
    }

    computeImages.clear();
    computeImages.resize(MAX_FRAMES_IN_FLIGHT);
    computeImagesInitialized.assign(MAX_FRAMES_IN_FLIGHT, false);

    const VkExtent2D extent = swapchain.getSwapExtent();
    for (auto& image : computeImages) {
        image.create(
            &instance,
            extent.width,
            extent.height,
            VK_SAMPLE_COUNT_1_BIT,
            swapchain.getSwapImgFormat(),
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
    }
}

void VulkanApp::createWorldBuffers() {
    if (world == nullptr) {
        throw std::runtime_error("voxel world must be set before creating world buffers");
    }

    const GpuVoxelBuffers gpuBuffers = buildGpuVoxelBuffers(*world);
    const VkDeviceSize chunkWindowIndexBufferSize = storageBufferSize(gpuBuffers.chunkWindowIndices);
    const VkDeviceSize chunkBrickMapBufferSize = storageBufferSize(gpuBuffers.chunkBrickMaps);
    brickResidency.initializeForWorld(*world);

    if (chunkWindowIndexBuffer.buffer != VK_NULL_HANDLE) {
        chunkWindowIndexBuffer.cleanup(&instance);
    }
    if (chunkBrickMapBuffer.buffer != VK_NULL_HANDLE) {
        chunkBrickMapBuffer.cleanup(&instance);
    }
    if (brickPoolBuffer.buffer != VK_NULL_HANDLE) {
        brickPoolBuffer.cleanup(&instance);
    }
    brickResidency.destroyRequestBuffers(instance);

    chunkWindowIndexBuffer.createBuffer(
        &instance,
        chunkWindowIndexBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    chunkBrickMapBuffer.createBuffer(
        &instance,
        chunkBrickMapBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    brickPoolBuffer.createBuffer(
        &instance,
        storageBufferSize(static_cast<size_t>(brickResidency.getGpuBrickCapacity()) * PACKED_BRICK_WORD_COUNT),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    brickResidency.createRequestBuffers(instance, MAX_FRAMES_IN_FLIGHT);

    uploadBufferWithStaging(instance, commandPool, chunkWindowIndexBuffer, gpuBuffers.chunkWindowIndices);
    uploadBufferWithStaging(instance, commandPool, chunkBrickMapBuffer, gpuBuffers.chunkBrickMaps);
    clearGpuUploadDirtyState(*world);
}

void VulkanApp::syncWorld() {
    if (world == nullptr) {
        throw std::runtime_error("voxel world must be set before syncing world buffers");
    }

    const GpuWorldDiff worldDiff = buildGpuWorldDiff(*world);
    const VkDeviceSize requiredChunkWindowIndexBufferSize = storageBufferSize(worldDiff.chunkWindowIndices.totalWordCount);
    const VkDeviceSize requiredChunkBrickMapBufferSize = storageBufferSize(worldDiff.chunkBrickMaps.totalWordCount);
    const VkDeviceSize requiredBrickPoolBufferSize = brickPoolBuffer.size;

    if (requiredChunkWindowIndexBufferSize > chunkWindowIndexBuffer.size ||
        requiredChunkBrickMapBufferSize > chunkBrickMapBuffer.size ||
        requiredBrickPoolBufferSize > brickPoolBuffer.size) {
        vkDeviceWaitIdle(instance.device());
        for (size_t i = 0; i < frameUploads.size(); i++) {
            cleanupUploads(i);
        }
        createWorldBuffers();
        createDescriptorSets(false);
        return;
    }

    if (worldDiff.empty()) {
        return;
    }

    brickResidency.rebuildTrackedState(*world, worldDiff.chunkBrickMaps.data, worldDiff.chunkBrickMaps.regions);

    VulkanAppFrameUploads& uploads = frameUploads.at(currentFrame);
    queueBufferUpload(uploads, chunkWindowIndexBuffer, worldDiff.chunkWindowIndices.data, byteRegionsFromWordRegions(worldDiff.chunkWindowIndices.regions));
    queueBufferUpload(uploads, chunkBrickMapBuffer, worldDiff.chunkBrickMaps.data, byteRegionsFromWordRegions(worldDiff.chunkBrickMaps.regions));
}

void VulkanApp::recordUploads(VkCommandBuffer commandBuffer) {
    VulkanAppFrameUploads& uploads = frameUploads.at(currentFrame);
    if (uploads.pending.empty()) {
        return;
    }

    std::vector<VkBufferMemoryBarrier> bufferBarriers;
    bufferBarriers.reserve(uploads.pending.size());

    for (const auto& upload : uploads.pending) {
        recordBufferCopies(commandBuffer, upload.stagingBuffer.buffer, upload.destinationBuffer, upload.regions);

        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = upload.destinationBuffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        bufferBarriers.push_back(barrier);
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        static_cast<uint32_t>(bufferBarriers.size()), bufferBarriers.data(),
        0, nullptr
    );
}

void VulkanApp::cleanupUploads(size_t frameIndex) {
    VulkanAppFrameUploads& uploads = frameUploads.at(frameIndex);
    for (auto& upload : uploads.pending) {
        upload.stagingBuffer.cleanup(&instance);
    }
    uploads.pending.clear();
}

void VulkanApp::queueBufferUpload(VulkanAppFrameUploads& uploads, Buffer& destinationBuffer, const std::vector<uint32_t>& data, const std::vector<BufferCopyRegion>& regions)
{
    if (data.empty() || regions.empty()) {
        return;
    }

    VulkanAppBufferUpload upload{};
    upload.destinationBuffer = destinationBuffer.buffer;
    upload.regions = regions;
    upload.stagingBuffer.createBuffer(
        &instance,
        storageBufferSize(data),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    upload.stagingBuffer.upload(&instance, data.data(), storageBufferSize(data));
    uploads.pending.push_back(std::move(upload));
}

void VulkanApp::resetRequestBuffer(size_t frameIndex)
{
    brickResidency.resetRequestBuffer(frameIndex);
}

void VulkanApp::ensureGpuBrickCapacity(uint32_t requiredCapacity)
{
    brickResidency.ensureGpuBrickCapacity(instance, commandPool, brickPoolBuffer, requiredCapacity, *this);
}

void VulkanApp::processBrickRequests(size_t frameIndex)
{
    if (world == nullptr) {
        return;
    }

    brickResidency.processBrickRequests(
        frameIndex,
        *world,
        instance,
        commandPool,
        brickPoolBuffer,
        chunkBrickMapBuffer,
        frameUploads.at(frameIndex),
        *this
    );
}

void VulkanApp::createDescriptorSets(bool allocateSets) {
    std::vector<VkDescriptorImageInfo> imageInfos(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorBufferInfo> chunkWindowIndexInfos(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorBufferInfo> chunkBrickMapInfos(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorBufferInfo> brickPoolInfos(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorBufferInfo> brickRequestInfos(MAX_FRAMES_IN_FLIGHT);
    std::vector<std::vector<DescriptorWrite>> descriptorWrites(MAX_FRAMES_IN_FLIGHT);
    auto& requestBuffers = brickResidency.getRequestBuffers();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        imageInfos[i].sampler = VK_NULL_HANDLE;
        imageInfos[i].imageView = computeImages[i].getView();
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        descriptorWrites[i].push_back({0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imageInfos[i]});

        chunkWindowIndexInfos[i].buffer = chunkWindowIndexBuffer.buffer;
        chunkWindowIndexInfos[i].offset = 0;
        chunkWindowIndexInfos[i].range = chunkWindowIndexBuffer.size;
        descriptorWrites[i].push_back({1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &chunkWindowIndexInfos[i], nullptr});

        chunkBrickMapInfos[i].buffer = chunkBrickMapBuffer.buffer;
        chunkBrickMapInfos[i].offset = 0;
        chunkBrickMapInfos[i].range = chunkBrickMapBuffer.size;
        descriptorWrites[i].push_back({2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &chunkBrickMapInfos[i], nullptr});

        brickPoolInfos[i].buffer = brickPoolBuffer.buffer;
        brickPoolInfos[i].offset = 0;
        brickPoolInfos[i].range = brickPoolBuffer.size;
        descriptorWrites[i].push_back({3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &brickPoolInfos[i], nullptr});

        brickRequestInfos[i].buffer = requestBuffers.at(i).buffer.buffer;
        brickRequestInfos[i].offset = 0;
        brickRequestInfos[i].range = requestBuffers.at(i).buffer.size;
        descriptorWrites[i].push_back({4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &brickRequestInfos[i], nullptr});
    }

    if (allocateSets) {
        descriptorManager.initSets(&instance, MAX_FRAMES_IN_FLIGHT, descriptorWrites);
    } else {
        descriptorManager.updateSets(&instance, descriptorWrites);
    }
}