#include "voxel_world.hpp"
#include "materials.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
size_t voxelIndex(const glm::uvec3& dimensions, uint32_t x, uint32_t y, uint32_t z) {
    return static_cast<size_t>(x) +
           static_cast<size_t>(dimensions.x) *
               (static_cast<size_t>(y) + static_cast<size_t>(dimensions.y) * static_cast<size_t>(z));
}

void packBrick(std::vector<uint32_t>& brickData, size_t brickIndex, const Brick& brick) {
    const size_t brickBaseIndex = brickIndex * PACKED_BRICK_WORD_COUNT;

    for (uint32_t maskWordIndex = 0; maskWordIndex < OCCUPANCY_MASK_WORD_COUNT; maskWordIndex++) {
        brickData[brickBaseIndex + maskWordIndex] = brick.occupancyMaskWords[maskWordIndex];
    }

    for (uint32_t z = 0; z < BRICK_SIZE; z++) {
        for (uint32_t y = 0; y < BRICK_SIZE; y++) {
            for (uint32_t x = 0; x < BRICK_SIZE; x++) {
                const uint32_t flatIndex = x + BRICK_SIZE * (y + BRICK_SIZE * z);
                const size_t packedWordIndex = brickBaseIndex + OCCUPANCY_MASK_WORD_COUNT + flatIndex / 4u;
                const uint32_t bitShift = 8u * (flatIndex % 4u);
                brickData[packedWordIndex] |= static_cast<uint32_t>(brick.voxels[x][y][z]) << bitShift;
            }
        }
    }
}

void packBrickMapEntry(std::vector<uint32_t>& chunkBrickMaps, size_t packedEntryIndex, const BrickMapEntry& entry) {
    chunkBrickMaps[packedEntryIndex] = entry.index;
    chunkBrickMaps[packedEntryIndex + 1u] = entry.materialId;
}

std::vector<std::pair<size_t, size_t>> buildDirtySpans(const std::vector<uint8_t>& dirtyFlags) {
    std::vector<std::pair<size_t, size_t>> spans;

    size_t spanStart = 0;
    size_t spanLength = 0;
    for (size_t i = 0; i < dirtyFlags.size(); i++) {
        if (dirtyFlags[i] != 0u) {
            if (spanLength == 0) {
                spanStart = i;
            }
            spanLength++;
            continue;
        }

        if (spanLength > 0) {
            spans.emplace_back(spanStart, spanLength);
            spanLength = 0;
        }
    }

    if (spanLength > 0) {
        spans.emplace_back(spanStart, spanLength);
    }

    return spans;
}
}

VoxelWorld::VoxelWorld(glm::uvec3 inChunkCounts) : chunkCounts(inChunkCounts) {
    if (chunkCounts.x == 0 || chunkCounts.y == 0 || chunkCounts.z == 0) {
        throw std::runtime_error("voxel world must contain at least one chunk in every dimension");
    }

    const size_t totalChunkCount = static_cast<size_t>(chunkCounts.x) * chunkCounts.y * chunkCounts.z;
    chunks.reserve(totalChunkCount);

    for (uint32_t z = 0; z < chunkCounts.z; z++) {
        for (uint32_t y = 0; y < chunkCounts.y; y++) {
            for (uint32_t x = 0; x < chunkCounts.x; x++) {
                const size_t currentChunkIndex = chunks.size();
                chunks.emplace_back(
                    glm::ivec3(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)),
                    currentChunkIndex
                );
                chunks.back().setDirtyCallbacks(
                    [this](size_t dirtyChunkIndex, uint32_t mapIndex) {
                        onChunkBrickMapDirty(dirtyChunkIndex, mapIndex);
                    },
                    [this](uint32_t brickIndex) {
                        onBrickPoolDirty(brickIndex);
                    }
                );
            }
        }
    }

    dirtyChunkBrickMapEntries.assign(chunks.size() * Chunk::BRICK_COUNT, 0u);
}

Chunk& VoxelWorld::getChunk(uint32_t x, uint32_t y, uint32_t z) {
    return chunks.at(chunkIndex(x, y, z));
}

const Chunk& VoxelWorld::getChunk(uint32_t x, uint32_t y, uint32_t z) const {
    return chunks.at(chunkIndex(x, y, z));
}

bool VoxelWorld::containsVoxel(int32_t x, int32_t y, int32_t z) const {
    const glm::uvec3 dimensions = getVoxelDimensions();
    return x >= 0 && y >= 0 && z >= 0 &&
           static_cast<uint32_t>(x) < dimensions.x &&
           static_cast<uint32_t>(y) < dimensions.y &&
           static_cast<uint32_t>(z) < dimensions.z;
}

uint32_t VoxelWorld::getVoxel(uint32_t x, uint32_t y, uint32_t z) const {
    const uint32_t chunkX = x / Chunk::SIZE;
    const uint32_t chunkY = y / Chunk::SIZE;
    const uint32_t chunkZ = z / Chunk::SIZE;

    return getChunk(chunkX, chunkY, chunkZ).get(
        x % Chunk::SIZE,
        y % Chunk::SIZE,
        z % Chunk::SIZE
    );
}

void VoxelWorld::setVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t value) {
    const uint32_t chunkX = x / Chunk::SIZE;
    const uint32_t chunkY = y / Chunk::SIZE;
    const uint32_t chunkZ = z / Chunk::SIZE;

    getChunk(chunkX, chunkY, chunkZ).set(
        x % Chunk::SIZE,
        y % Chunk::SIZE,
        z % Chunk::SIZE,
        value
    );
}

glm::uvec3 VoxelWorld::getVoxelDimensions() const {
    return chunkCounts * Chunk::SIZE;
}

GpuVoxelBuffers VoxelWorld::buildGpuBuffers() const {
    GpuVoxelBuffers gpuBuffers;

    gpuBuffers.chunkBrickMaps.resize(chunks.size() * Chunk::BRICK_COUNT * PACKED_BRICK_MAP_ENTRY_WORD_COUNT);
    for (size_t chunkIndex = 0; chunkIndex < chunks.size(); chunkIndex++) {
        const auto& brickMap = chunks[chunkIndex].getBrickMap();
        const size_t chunkBaseIndex = chunkIndex * Chunk::BRICK_COUNT * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;

        for (size_t brickIndex = 0; brickIndex < Chunk::BRICK_COUNT; brickIndex++) {
            const size_t entryBaseIndex = chunkBaseIndex + brickIndex * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
            packBrickMapEntry(gpuBuffers.chunkBrickMaps, entryBaseIndex, brickMap[brickIndex]);
        }
    }

    const std::vector<Brick>& brickPool = Chunk::getBrickPool();
    gpuBuffers.brickData.assign(brickPool.size() * PACKED_BRICK_WORD_COUNT, 0u);

    for (size_t brickIndex = 0; brickIndex < brickPool.size(); brickIndex++) {
        packBrick(gpuBuffers.brickData, brickIndex, brickPool[brickIndex]);
    }

    return gpuBuffers;
}

GpuWorldDiff VoxelWorld::buildGpuBufferDiffs() {
    GpuWorldDiff worldDiff{};
    worldDiff.chunkBrickMaps.totalWordCount = chunks.size() * Chunk::BRICK_COUNT * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
    worldDiff.brickData.totalWordCount = Chunk::getBrickPool().size() * PACKED_BRICK_WORD_COUNT;

    const std::vector<std::pair<size_t, size_t>> dirtyChunkSpans = buildDirtySpans(dirtyChunkBrickMapEntries);
    for (const auto& span : dirtyChunkSpans) {
        const size_t spanStartEntry = span.first;
        const size_t spanEntryCount = span.second;
        const size_t dstWordOffset = spanStartEntry * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
        const size_t wordCount = spanEntryCount * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
        const size_t srcWordOffset = worldDiff.chunkBrickMaps.data.size();

        worldDiff.chunkBrickMaps.data.resize(srcWordOffset + wordCount, 0u);
        worldDiff.chunkBrickMaps.regions.push_back({srcWordOffset, dstWordOffset, wordCount});

        for (size_t entryOffset = 0; entryOffset < spanEntryCount; entryOffset++) {
            const size_t globalEntryIndex = spanStartEntry + entryOffset;
            const size_t dirtyChunkIndex = globalEntryIndex / Chunk::BRICK_COUNT;
            const size_t brickIndex = globalEntryIndex % Chunk::BRICK_COUNT;
            const size_t packedEntryIndex = srcWordOffset + entryOffset * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
            packBrickMapEntry(
                worldDiff.chunkBrickMaps.data,
                packedEntryIndex,
                chunks[dirtyChunkIndex].getBrickMap()[brickIndex]
            );
        }
    }

    const std::vector<std::pair<size_t, size_t>> dirtyBrickSpans = buildDirtySpans(dirtyBrickPoolEntries);
    const std::vector<Brick>& brickPool = Chunk::getBrickPool();
    for (const auto& span : dirtyBrickSpans) {
        const size_t spanStartBrick = span.first;
        const size_t spanBrickCount = span.second;
        const size_t dstWordOffset = spanStartBrick * PACKED_BRICK_WORD_COUNT;
        const size_t wordCount = spanBrickCount * PACKED_BRICK_WORD_COUNT;
        const size_t srcWordOffset = worldDiff.brickData.data.size();

        worldDiff.brickData.data.resize(srcWordOffset + wordCount, 0u);
        worldDiff.brickData.regions.push_back({srcWordOffset, dstWordOffset, wordCount});

        for (size_t brickOffset = 0; brickOffset < spanBrickCount; brickOffset++) {
            packBrick(worldDiff.brickData.data, (srcWordOffset / PACKED_BRICK_WORD_COUNT) + brickOffset, brickPool[spanStartBrick + brickOffset]);
        }
    }

    clearDirtyState();
    return worldDiff;
}

std::vector<uint32_t> VoxelWorld::buildVoxelBuffer() const {
    const glm::uvec3 voxelDimensions = getVoxelDimensions();
    std::vector<uint32_t> voxels(
        static_cast<size_t>(voxelDimensions.x) * voxelDimensions.y * voxelDimensions.z,
        AIR_MATERIAL
    );

    for (uint32_t chunkZ = 0; chunkZ < chunkCounts.z; chunkZ++) {
        for (uint32_t chunkY = 0; chunkY < chunkCounts.y; chunkY++) {
            for (uint32_t chunkX = 0; chunkX < chunkCounts.x; chunkX++) {
                const Chunk& chunk = getChunk(chunkX, chunkY, chunkZ);

                for (uint32_t localZ = 0; localZ < Chunk::SIZE; localZ++) {
                    for (uint32_t localY = 0; localY < Chunk::SIZE; localY++) {
                        for (uint32_t localX = 0; localX < Chunk::SIZE; localX++) {
                            const glm::uvec3 worldPos(
                                chunkX * Chunk::SIZE + localX,
                                chunkY * Chunk::SIZE + localY,
                                chunkZ * Chunk::SIZE + localZ
                            );

                            voxels[voxelIndex(voxelDimensions, worldPos.x, worldPos.y, worldPos.z)] =
                                chunk.get(localX, localY, localZ);
                        }
                    }
                }
            }
        }
    }

    return voxels;
}

void VoxelWorld::clearDirtyState() {
    std::fill(dirtyChunkBrickMapEntries.begin(), dirtyChunkBrickMapEntries.end(), 0u);
    std::fill(dirtyBrickPoolEntries.begin(), dirtyBrickPoolEntries.end(), 0u);
}

void VoxelWorld::onChunkBrickMapDirty(size_t dirtyChunkIndex, uint32_t mapIndex) {
    const size_t globalEntryIndex = dirtyChunkIndex * Chunk::BRICK_COUNT + mapIndex;
    if (globalEntryIndex >= dirtyChunkBrickMapEntries.size()) {
        return;
    }

    dirtyChunkBrickMapEntries[globalEntryIndex] = 1u;
}

void VoxelWorld::onBrickPoolDirty(uint32_t brickIndex) {
    if (brickIndex >= dirtyBrickPoolEntries.size()) {
        dirtyBrickPoolEntries.resize(static_cast<size_t>(brickIndex) + 1u, 0u);
    }

    dirtyBrickPoolEntries[brickIndex] = 1u;
}

size_t VoxelWorld::chunkIndex(uint32_t x, uint32_t y, uint32_t z) const {
    if (x >= chunkCounts.x || y >= chunkCounts.y || z >= chunkCounts.z) {
        throw std::out_of_range("voxel chunk index out of range");
    }

    return static_cast<size_t>(x) +
           static_cast<size_t>(chunkCounts.x) *
               (static_cast<size_t>(y) + static_cast<size_t>(chunkCounts.y) * static_cast<size_t>(z));
}
