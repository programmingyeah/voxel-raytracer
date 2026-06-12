#include "voxel_world.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace {
constexpr uint32_t INVALID_CHUNK_SLOT = std::numeric_limits<uint32_t>::max();

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
    Chunk::initializeBrickPool(totalChunkCount);
    chunks.reserve(totalChunkCount);
    chunkWindowIndices.resize(totalChunkCount);
    std::iota(chunkWindowIndices.begin(), chunkWindowIndices.end(), 0u);
    chunkSlotSolidVoxelCounts.assign(totalChunkCount, 0u);

    for (uint32_t z = 0; z < chunkCounts.z; z++) {
        for (uint32_t y = 0; y < chunkCounts.y; y++) {
            for (uint32_t x = 0; x < chunkCounts.x; x++) {
                const size_t currentChunkIndex = chunks.size();
                chunks.emplace_back(
                    chunkOrigin + glm::ivec3(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)),
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

    dirtyChunkWindowIndices.assign(chunkWindowIndices.size(), 0u);
    dirtyChunkBrickMapEntries.assign(chunks.size() * Chunk::BRICK_COUNT, 0u);
    dirtyBrickPoolEntries.assign(Chunk::getBrickPool().size(), 0u);
}

Chunk& VoxelWorld::getChunk(uint32_t x, uint32_t y, uint32_t z) {
    return getChunkByWindowIndex(chunkIndex(x, y, z));
}

const Chunk& VoxelWorld::getChunk(uint32_t x, uint32_t y, uint32_t z) const {
    return getChunkByWindowIndex(chunkIndex(x, y, z));
}

Chunk& VoxelWorld::getChunkByWindowIndex(size_t localWindowIndex) {
    return chunks.at(chunkWindowIndices.at(localWindowIndex));
}

const Chunk& VoxelWorld::getChunkByWindowIndex(size_t localWindowIndex) const {
    return chunks.at(chunkWindowIndices.at(localWindowIndex));
}

bool VoxelWorld::containsVoxel(int32_t x, int32_t y, int32_t z) const {
    const glm::ivec3 voxelMin = getVoxelMin();
    const glm::uvec3 dimensions = getVoxelDimensions();
    return x >= voxelMin.x &&
           y >= voxelMin.y &&
           z >= voxelMin.z &&
           x < voxelMin.x + static_cast<int32_t>(dimensions.x) &&
           y < voxelMin.y + static_cast<int32_t>(dimensions.y) &&
           z < voxelMin.z + static_cast<int32_t>(dimensions.z);
}

uint32_t VoxelWorld::getVoxel(uint32_t x, uint32_t y, uint32_t z) const {
    const glm::ivec3 voxelMin = getVoxelMin();
    const uint32_t localX = static_cast<uint32_t>(static_cast<int32_t>(x) - voxelMin.x);
    const uint32_t localY = static_cast<uint32_t>(static_cast<int32_t>(y) - voxelMin.y);
    const uint32_t localZ = static_cast<uint32_t>(static_cast<int32_t>(z) - voxelMin.z);
    const uint32_t chunkX = localX / Chunk::SIZE;
    const uint32_t chunkY = localY / Chunk::SIZE;
    const uint32_t chunkZ = localZ / Chunk::SIZE;

    return getChunk(chunkX, chunkY, chunkZ).get(
        localX % Chunk::SIZE,
        localY % Chunk::SIZE,
        localZ % Chunk::SIZE
    );
}

void VoxelWorld::setVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t value) {
    const glm::ivec3 voxelMin = getVoxelMin();
    const uint32_t localX = static_cast<uint32_t>(static_cast<int32_t>(x) - voxelMin.x);
    const uint32_t localY = static_cast<uint32_t>(static_cast<int32_t>(y) - voxelMin.y);
    const uint32_t localZ = static_cast<uint32_t>(static_cast<int32_t>(z) - voxelMin.z);
    const uint32_t chunkX = localX / Chunk::SIZE;
    const uint32_t chunkY = localY / Chunk::SIZE;
    const uint32_t chunkZ = localZ / Chunk::SIZE;

    getChunk(chunkX, chunkY, chunkZ).set(
        localX % Chunk::SIZE,
        localY % Chunk::SIZE,
        localZ % Chunk::SIZE,
        value
    );
}

std::vector<uint32_t> VoxelWorld::shiftChunkWindow(glm::ivec3 deltaChunks) {
    if (deltaChunks == glm::ivec3(0)) {
        return {};
    }

    const glm::ivec3 newChunkOrigin = chunkOrigin + deltaChunks;
    std::vector<uint32_t> nextChunkWindowIndices(chunkWindowIndices.size(), INVALID_CHUNK_SLOT);
    std::vector<uint32_t> recycledChunkSlots;
    std::vector<uint32_t> enteringWindowIndices;

    recycledChunkSlots.reserve(chunkWindowIndices.size());
    enteringWindowIndices.reserve(chunkWindowIndices.size());

    for (size_t localWindowIndex = 0; localWindowIndex < chunkWindowIndices.size(); localWindowIndex++) {
        const glm::uvec3 localChunkCoord = chunkCoordFromWindowIndex(localWindowIndex);
        const glm::ivec3 worldChunkCoord = chunkOrigin + glm::ivec3(localChunkCoord);
        const glm::ivec3 nextLocalChunkCoord = worldChunkCoord - newChunkOrigin;

        if (nextLocalChunkCoord.x >= 0 && nextLocalChunkCoord.y >= 0 && nextLocalChunkCoord.z >= 0 &&
            nextLocalChunkCoord.x < static_cast<int32_t>(chunkCounts.x) &&
            nextLocalChunkCoord.y < static_cast<int32_t>(chunkCounts.y) &&
            nextLocalChunkCoord.z < static_cast<int32_t>(chunkCounts.z)) {
            const size_t nextLocalWindowIndex = chunkIndex(
                static_cast<uint32_t>(nextLocalChunkCoord.x),
                static_cast<uint32_t>(nextLocalChunkCoord.y),
                static_cast<uint32_t>(nextLocalChunkCoord.z)
            );
            nextChunkWindowIndices[nextLocalWindowIndex] = chunkWindowIndices[localWindowIndex];
        } else {
            recycledChunkSlots.push_back(chunkWindowIndices[localWindowIndex]);
        }
    }

    for (size_t localWindowIndex = 0; localWindowIndex < nextChunkWindowIndices.size(); localWindowIndex++) {
        if (nextChunkWindowIndices[localWindowIndex] != INVALID_CHUNK_SLOT) {
            continue;
        }

        if (recycledChunkSlots.empty()) {
            throw std::runtime_error("chunk window shift ran out of recyclable chunk slots");
        }

        const uint32_t recycledChunkSlot = recycledChunkSlots.back();
        recycledChunkSlots.pop_back();
        nextChunkWindowIndices[localWindowIndex] = recycledChunkSlot;
        chunks[recycledChunkSlot].setChunkCoordinate(newChunkOrigin + glm::ivec3(chunkCoordFromWindowIndex(localWindowIndex)));
        enteringWindowIndices.push_back(static_cast<uint32_t>(localWindowIndex));
    }

    for (size_t localWindowIndex = 0; localWindowIndex < chunkWindowIndices.size(); localWindowIndex++) {
        if (chunkWindowIndices[localWindowIndex] != nextChunkWindowIndices[localWindowIndex]) {
            dirtyChunkWindowIndices[localWindowIndex] = 1u;
        }
    }

    chunkWindowIndices = std::move(nextChunkWindowIndices);
    chunkOrigin = newChunkOrigin;
    return enteringWindowIndices;
}

glm::ivec3 VoxelWorld::getVoxelMin() const {
    return chunkOrigin * static_cast<int32_t>(Chunk::SIZE);
}

glm::ivec3 VoxelWorld::getVoxelMax() const {
    return getVoxelMin() + glm::ivec3(getVoxelDimensions());
}

glm::uvec3 VoxelWorld::getVoxelDimensions() const {
    return chunkCounts * Chunk::SIZE;
}

GpuVoxelBuffers VoxelWorld::buildGpuBuffers() const {
    GpuVoxelBuffers gpuBuffers;

    gpuBuffers.chunkWindowIndices = chunkWindowIndices;
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
    worldDiff.chunkWindowIndices.totalWordCount = chunkWindowIndices.size();
    worldDiff.chunkBrickMaps.totalWordCount = chunks.size() * Chunk::BRICK_COUNT * PACKED_BRICK_MAP_ENTRY_WORD_COUNT;
    worldDiff.brickData.totalWordCount = Chunk::getBrickPool().size() * PACKED_BRICK_WORD_COUNT;

    const std::vector<std::pair<size_t, size_t>> dirtyChunkWindowSpans = buildDirtySpans(dirtyChunkWindowIndices);
    for (const auto& span : dirtyChunkWindowSpans) {
        const size_t spanStartIndex = span.first;
        const size_t spanIndexCount = span.second;
        const size_t srcWordOffset = worldDiff.chunkWindowIndices.data.size();

        worldDiff.chunkWindowIndices.data.insert(
            worldDiff.chunkWindowIndices.data.end(),
            chunkWindowIndices.begin() + static_cast<std::ptrdiff_t>(spanStartIndex),
            chunkWindowIndices.begin() + static_cast<std::ptrdiff_t>(spanStartIndex + spanIndexCount)
        );
        worldDiff.chunkWindowIndices.regions.push_back({srcWordOffset, spanStartIndex, spanIndexCount});
    }

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

void VoxelWorld::clearDirtyState() {
    std::fill(dirtyChunkWindowIndices.begin(), dirtyChunkWindowIndices.end(), 0u);
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
        return;
    }

    dirtyBrickPoolEntries[brickIndex] = 1u;
}

void VoxelWorld::setChunkSolidVoxelCountByWindowIndex(size_t localWindowIndex, uint64_t solidVoxelCount) {
    chunkSlotSolidVoxelCounts.at(chunkWindowIndices.at(localWindowIndex)) = solidVoxelCount;
}

uint64_t VoxelWorld::getTotalSolidVoxelCount() const {
    return std::accumulate(chunkSlotSolidVoxelCounts.begin(), chunkSlotSolidVoxelCounts.end(), uint64_t{0});
}

size_t VoxelWorld::chunkIndex(uint32_t x, uint32_t y, uint32_t z) const {
    if (x >= chunkCounts.x || y >= chunkCounts.y || z >= chunkCounts.z) {
        throw std::out_of_range("voxel chunk index out of range");
    }

    return static_cast<size_t>(x) +
           static_cast<size_t>(chunkCounts.x) *
               (static_cast<size_t>(y) + static_cast<size_t>(chunkCounts.y) * static_cast<size_t>(z));
}

glm::uvec3 VoxelWorld::chunkCoordFromWindowIndex(size_t localWindowIndex) const {
    const size_t layerSize = static_cast<size_t>(chunkCounts.x) * chunkCounts.y;
    const uint32_t z = static_cast<uint32_t>(localWindowIndex / layerSize);
    const size_t remainderAfterZ = localWindowIndex % layerSize;
    const uint32_t y = static_cast<uint32_t>(remainderAfterZ / chunkCounts.x);
    const uint32_t x = static_cast<uint32_t>(remainderAfterZ % chunkCounts.x);
    return glm::uvec3(x, y, z);
}
