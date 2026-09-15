#include "renderer.hpp"

#include "renderer_shared.hpp"
#include "../world/gpu_world_layout.hpp"

#include <cstring>
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

    brickResidency.initializeForWorld(*world);
    if (sharedBrickPoolBuffer.buffer != VK_NULL_HANDLE) {
        sharedBrickPoolBuffer.cleanup(&instance);
    }
    sharedBrickPoolBuffer.createBuffer(
        &instance,
        storageBufferSize(static_cast<size_t>(brickResidency.getGpuBrickCapacity()) * PACKED_BRICK_WORD_COUNT),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    brickResidency.destroyRequestBuffers(instance);
    brickResidency.createRequestBuffers(instance, MAX_FRAMES_IN_FLIGHT);

    for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
        createWorldBuffersForLod(static_cast<WorldLod>(lodIndex));
    }

    syncWorldMetadata();
}

void VulkanApp::syncWorldMetadata() {
    if (world == nullptr) {
        throw std::runtime_error("voxel world must be set before syncing world metadata");
    }

    GpuWorldMetadata metadata{};
    for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
        const WorldLod lod = static_cast<WorldLod>(lodIndex);
        GpuLodMetadata& lodMetadata = metadata.lods.at(lodIndex);
        lodMetadata.worldMin = glm::ivec4(world->getVoxelMin(lod), 0);
        lodMetadata.worldMax = glm::ivec4(world->getVoxelMax(lod), 0);
        lodMetadata.chunkWindowDimensions = glm::ivec4(world->getChunkCounts(lod), 0);
        lodMetadata.traversalInfo = glm::ivec4(
            static_cast<int32_t>(world->getChunkWorldSpan(lod)),
            static_cast<int32_t>(world->getLocalCellWorldStep(lod)),
            0,
            0
        );
    }

    if (worldMetadataBuffer.buffer == VK_NULL_HANDLE) {
        worldMetadataBuffer.createBuffer(
            &instance,
            static_cast<VkDeviceSize>(sizeof(GpuWorldMetadata)),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
    }

    std::vector<uint32_t> packed(sizeof(GpuWorldMetadata) / sizeof(uint32_t));
    std::memcpy(packed.data(), &metadata, sizeof(GpuWorldMetadata));
    uploadBufferWithStaging(instance, commandPool, worldMetadataBuffer, packed);
}

void VulkanApp::createWorldBuffersForLod(WorldLod lod) {
    const GpuVoxelBuffers gpuBuffers = buildGpuVoxelBuffers(*world, lod);
    const VkDeviceSize chunkWindowIndexBufferSize = storageBufferSize(gpuBuffers.chunkWindowIndices);
    const VkDeviceSize chunkBrickMapBufferSize = storageBufferSize(gpuBuffers.chunkBrickMaps);
    GpuLodResources& gpuLod = gpuLods.at(static_cast<size_t>(lod));

    if (gpuLod.chunkWindowIndexBuffer.buffer != VK_NULL_HANDLE) {
        gpuLod.chunkWindowIndexBuffer.cleanup(&instance);
    }
    if (gpuLod.chunkBrickMapBuffer.buffer != VK_NULL_HANDLE) {
        gpuLod.chunkBrickMapBuffer.cleanup(&instance);
    }

    gpuLod.chunkWindowIndexBuffer.createBuffer(
        &instance,
        chunkWindowIndexBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    gpuLod.chunkBrickMapBuffer.createBuffer(
        &instance,
        chunkBrickMapBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    uploadBufferWithStaging(instance, commandPool, gpuLod.chunkWindowIndexBuffer, gpuBuffers.chunkWindowIndices);
    uploadBufferWithStaging(instance, commandPool, gpuLod.chunkBrickMapBuffer, gpuBuffers.chunkBrickMaps);
    clearGpuUploadDirtyState(*world, lod);
}

void VulkanApp::syncWorld() {
    if (world == nullptr) {
        throw std::runtime_error("voxel world must be set before syncing world buffers");
    }

    for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
        syncWorldForLod(static_cast<WorldLod>(lodIndex));
    }

    syncWorldMetadata();
}

void VulkanApp::syncWorldForLod(WorldLod lod) {
    GpuLodResources& gpuLod = gpuLods.at(static_cast<size_t>(lod));
    const GpuWorldDiff worldDiff = buildGpuWorldDiff(*world, lod);
    const VkDeviceSize requiredChunkWindowIndexBufferSize = storageBufferSize(worldDiff.chunkWindowIndices.totalWordCount);
    const VkDeviceSize requiredChunkBrickMapBufferSize = storageBufferSize(worldDiff.chunkBrickMaps.totalWordCount);

    if (requiredChunkWindowIndexBufferSize > gpuLod.chunkWindowIndexBuffer.size ||
        requiredChunkBrickMapBufferSize > gpuLod.chunkBrickMapBuffer.size) {
        vkDeviceWaitIdle(instance.device());
        for (size_t i = 0; i < frameUploads.size(); i++) {
            cleanupUploads(i);
        }
        createWorldBuffersForLod(lod);
        createDescriptorSets(false);
        return;
    }

    if (worldDiff.empty()) {
        return;
    }

    brickResidency.rebuildTrackedState(*world, lod, worldDiff.chunkBrickMaps.data, worldDiff.chunkBrickMaps.regions);

    VulkanAppFrameUploads& uploads = frameUploads.at(currentFrame);
    queueBufferUpload(uploads, gpuLod.chunkWindowIndexBuffer, worldDiff.chunkWindowIndices.data, byteRegionsFromWordRegions(worldDiff.chunkWindowIndices.regions));
    queueBufferUpload(uploads, gpuLod.chunkBrickMapBuffer, worldDiff.chunkBrickMaps.data, byteRegionsFromWordRegions(worldDiff.chunkBrickMaps.regions));
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
    for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
        resetRequestBufferForLod(frameIndex, static_cast<WorldLod>(lodIndex));
    }
}

void VulkanApp::resetRequestBufferForLod(size_t frameIndex, WorldLod lod)
{
brickResidency.resetRequestBuffer(frameIndex);
}

void VulkanApp::ensureGpuBrickCapacity(uint32_t requiredCapacity)
{
    brickResidency.ensureGpuBrickCapacity(instance, commandPool, sharedBrickPoolBuffer, requiredCapacity, *this);
}

void VulkanApp::processBrickRequests(size_t frameIndex)
{
    if (world == nullptr) {
        return;
    }

    for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
        processBrickRequestsForLod(frameIndex, static_cast<WorldLod>(lodIndex));
    }
}

void VulkanApp::processBrickRequestsForLod(size_t frameIndex, WorldLod lod)
{
    GpuLodResources& gpuLod = gpuLods.at(static_cast<size_t>(lod));
    brickResidency.processBrickRequests(
        frameIndex,
        lod,
        *world,
        instance,
        commandPool,
        sharedBrickPoolBuffer,
        gpuLod.chunkBrickMapBuffer,
        frameUploads.at(frameIndex),
        *this
    );
}

void VulkanApp::createDescriptorSets(bool allocateSets) {
    std::vector<VkDescriptorImageInfo> imageInfos(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorBufferInfo> metadataInfos(MAX_FRAMES_IN_FLIGHT);
    std::array<std::vector<VkDescriptorBufferInfo>, WORLD_LOD_COUNT> chunkWindowIndexInfos{};
    std::array<std::vector<VkDescriptorBufferInfo>, WORLD_LOD_COUNT> chunkBrickMapInfos{};
    std::array<std::vector<VkDescriptorBufferInfo>, WORLD_LOD_COUNT> brickPoolInfos{};
    std::array<std::vector<VkDescriptorBufferInfo>, WORLD_LOD_COUNT> brickRequestInfos{};
    std::vector<std::vector<DescriptorWrite>> descriptorWrites(MAX_FRAMES_IN_FLIGHT);

    for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
        chunkWindowIndexInfos.at(lodIndex).resize(MAX_FRAMES_IN_FLIGHT);
        chunkBrickMapInfos.at(lodIndex).resize(MAX_FRAMES_IN_FLIGHT);
        brickPoolInfos.at(lodIndex).resize(MAX_FRAMES_IN_FLIGHT);
        brickRequestInfos.at(lodIndex).resize(MAX_FRAMES_IN_FLIGHT);
    }

    const auto bindingBaseForLod = [](size_t lodIndex) -> uint32_t {
        return 2u + static_cast<uint32_t>(lodIndex) * 4u;
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        imageInfos[i].sampler = VK_NULL_HANDLE;
        imageInfos[i].imageView = computeImages[i].getView();
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        descriptorWrites[i].push_back({0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &imageInfos[i]});

        metadataInfos.at(i).buffer = worldMetadataBuffer.buffer;
        metadataInfos.at(i).offset = 0;
        metadataInfos.at(i).range = worldMetadataBuffer.size;
        descriptorWrites[i].push_back({1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &metadataInfos.at(i), nullptr});

        for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
            const auto& gpuLod = gpuLods.at(lodIndex);
            const auto& requestBuffers = brickResidency.getRequestBuffers(static_cast<WorldLod>(lodIndex));
            const uint32_t bindingBase = bindingBaseForLod(lodIndex);

            chunkWindowIndexInfos.at(lodIndex).at(i).buffer = gpuLod.chunkWindowIndexBuffer.buffer;
            chunkWindowIndexInfos.at(lodIndex).at(i).offset = 0;
            chunkWindowIndexInfos.at(lodIndex).at(i).range = gpuLod.chunkWindowIndexBuffer.size;
            descriptorWrites[i].push_back({bindingBase + 0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &chunkWindowIndexInfos.at(lodIndex).at(i), nullptr});

            chunkBrickMapInfos.at(lodIndex).at(i).buffer = gpuLod.chunkBrickMapBuffer.buffer;
            chunkBrickMapInfos.at(lodIndex).at(i).offset = 0;
            chunkBrickMapInfos.at(lodIndex).at(i).range = gpuLod.chunkBrickMapBuffer.size;
            descriptorWrites[i].push_back({bindingBase + 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &chunkBrickMapInfos.at(lodIndex).at(i), nullptr});

            brickPoolInfos.at(lodIndex).at(i).buffer = sharedBrickPoolBuffer.buffer;
            brickPoolInfos.at(lodIndex).at(i).offset = 0;
            brickPoolInfos.at(lodIndex).at(i).range = sharedBrickPoolBuffer.size;
            descriptorWrites[i].push_back({bindingBase + 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &brickPoolInfos.at(lodIndex).at(i), nullptr});

            brickRequestInfos.at(lodIndex).at(i).buffer = requestBuffers.at(i).buffer.buffer;
            brickRequestInfos.at(lodIndex).at(i).offset = 0;
            brickRequestInfos.at(lodIndex).at(i).range = requestBuffers.at(i).buffer.size;
            descriptorWrites[i].push_back({bindingBase + 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &brickRequestInfos.at(lodIndex).at(i), nullptr});
        }
    }

    if (allocateSets) {
        descriptorManager.initSets(&instance, MAX_FRAMES_IN_FLIGHT, descriptorWrites);
    } else {
        descriptorManager.updateSets(&instance, descriptorWrites);
    }
}