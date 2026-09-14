#include "voxel_world.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace {
constexpr uint32_t INVALID_CHUNK_SLOT = std::numeric_limits<uint32_t>::max(); 
//test knob: 1.0 = worst case, 0.0 is none (dont choose that obviously)
constexpr double BRICK_BUFFER_SCALE = 0.05; //empirical, world gen currently requires at least 0.03

size_t scaledBrickCapacity(size_t worstCaseCapacity) {
    const double scaledCapacity = static_cast<double>(worstCaseCapacity) * BRICK_BUFFER_SCALE;
    return std::clamp(static_cast<size_t>(scaledCapacity), size_t{1}, worstCaseCapacity);
}
}

VoxelWorld::VoxelWorld(glm::uvec3 inChunkCounts) : chunkCounts(inChunkCounts) {
    if (chunkCounts.x == 0 || chunkCounts.y == 0 || chunkCounts.z == 0) {
        throw std::runtime_error("voxel world must contain at least one chunk in every dimension");
    }

    const size_t totalChunkCount = static_cast<size_t>(chunkCounts.x) * chunkCounts.y * chunkCounts.z;
    const size_t worstCaseBrickCapacity = totalChunkCount * Chunk::BRICK_COUNT;
    const size_t brickCapacity = scaledBrickCapacity(worstCaseBrickCapacity);

    slots.reserve(totalChunkCount);
    window.resize(totalChunkCount);
    std::iota(window.begin(), window.end(), 0u);

    brickPool.bricks.assign(brickCapacity, Brick{});
    brickPool.free.reserve(brickCapacity);
    for (size_t brickIndex = brickCapacity; brickIndex > 0; brickIndex--) {
        brickPool.free.push_back(static_cast<uint32_t>(brickIndex - 1u));
    }

    for (uint32_t z = 0; z < chunkCounts.z; z++) {
        for (uint32_t y = 0; y < chunkCounts.y; y++) {
            for (uint32_t x = 0; x < chunkCounts.x; x++) {
                const size_t currentChunkIndex = slots.size();
                slots.push_back({
                    Chunk(
                        chunkOrigin + glm::ivec3(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)),
                        currentChunkIndex
                    )
                });
                slots.back().chunk.bindStorage(
                    &brickPool.bricks,
                    [this]() { return allocateBrick(); },
                    [this](uint32_t brickIndex) { releaseBrick(brickIndex); },
                    [this](size_t dirtyChunkIndex, uint32_t mapIndex) { onChunkBrickMapDirty(dirtyChunkIndex, mapIndex); },
                    [this](uint32_t brickIndex) { onBrickPoolDirty(brickIndex); }
                );
            }
        }
    }

    dirty.window.assign(totalChunkCount, 0u);
    dirty.brickMaps.assign(slots.size(), 0u);
    dirty.brickPool.assign(brickPool.bricks.size(), 0u);
}

Chunk& VoxelWorld::getChunk(uint32_t x, uint32_t y, uint32_t z) {
    return getChunkByWindowIndex(chunkIndex(x, y, z));
}

const Chunk& VoxelWorld::getChunk(uint32_t x, uint32_t y, uint32_t z) const {
    return getChunkByWindowIndex(chunkIndex(x, y, z));
}

Chunk& VoxelWorld::getChunkByWindowIndex(size_t localWindowIndex) {
    return slots.at(window.at(localWindowIndex)).chunk;
}

const Chunk& VoxelWorld::getChunkByWindowIndex(size_t localWindowIndex) const {
    return slots.at(window.at(localWindowIndex)).chunk;
}

Chunk& VoxelWorld::getChunkByMutableSlotIndex(size_t chunkSlotIndex) {
    return slots.at(chunkSlotIndex).chunk;
}

const Chunk& VoxelWorld::getChunkBySlotIndex(size_t chunkSlotIndex) const {
    return slots.at(chunkSlotIndex).chunk;
}

bool VoxelWorld::containsVoxel(int32_t x, int32_t y, int32_t z) const {
    const glm::ivec3 voxelMin = getVoxelMin(), voxelMax = getVoxelMax();
    return x >= voxelMin.x && y >= voxelMin.y && z >= voxelMin.z &&
           x < voxelMax.x && y < voxelMax.y && z < voxelMax.z;
}

uint32_t VoxelWorld::getVoxel(uint32_t x, uint32_t y, uint32_t z) const {
    glm::uvec3 localVoxel{};
    const size_t chunkSlotIndex = chunkSlotIndexFromVoxel(x, y, z, localVoxel);
    return isChunkSlotGenerated(chunkSlotIndex)
        ? slots[chunkSlotIndex].chunk.get(localVoxel.x % Chunk::SIZE, localVoxel.y % Chunk::SIZE, localVoxel.z % Chunk::SIZE)
        : AIR_MATERIAL;
}

void VoxelWorld::setVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t value) {
    glm::uvec3 localVoxel{};
    const size_t chunkSlotIndex = chunkSlotIndexFromVoxel(x, y, z, localVoxel);
    if (isChunkSlotGenerated(chunkSlotIndex)) {
        slots[chunkSlotIndex].chunk.set(localVoxel.x % Chunk::SIZE, localVoxel.y % Chunk::SIZE, localVoxel.z % Chunk::SIZE, value);
    }
}

std::vector<uint32_t> VoxelWorld::shiftChunkWindow(glm::ivec3 deltaChunks) {
    if (deltaChunks == glm::ivec3(0)) {
        return {};
    }

    const size_t windowChunkCount = window.size();
    const glm::ivec3 newChunkOrigin = chunkOrigin + deltaChunks;
    std::vector<uint32_t> nextWindow(windowChunkCount, INVALID_CHUNK_SLOT);
    std::vector<uint32_t> recycledChunkSlots;
    std::vector<uint32_t> enteringWindowIndices;

    recycledChunkSlots.reserve(windowChunkCount);
    enteringWindowIndices.reserve(windowChunkCount);

    for (size_t localWindowIndex = 0; localWindowIndex < windowChunkCount; localWindowIndex++) {
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
            nextWindow[nextLocalWindowIndex] = window[localWindowIndex];
            continue;
        }

        const uint32_t recycledChunkSlot = window[localWindowIndex];
        if (chunkStateBySlotIndex(recycledChunkSlot) == ChunkRuntimeState::Generating) {
            return {};
        }

        recycledChunkSlots.push_back(recycledChunkSlot);
    }

    std::vector<uint32_t> recycledSlotsNeedingBrickMapUpload;
    recycledSlotsNeedingBrickMapUpload.reserve(recycledChunkSlots.size());

    for (size_t localWindowIndex = 0; localWindowIndex < windowChunkCount; localWindowIndex++) {
        if (nextWindow[localWindowIndex] != INVALID_CHUNK_SLOT) {
            continue;
        }

        if (recycledChunkSlots.empty()) {
            throw std::runtime_error("chunk window shift ran out of recyclable chunk slots");
        }

        const uint32_t recycledChunkSlot = recycledChunkSlots.back();
        recycledChunkSlots.pop_back();
        nextWindow[localWindowIndex] = recycledChunkSlot;

        if (isChunkSlotGenerated(recycledChunkSlot)) {
            totalSolidVoxelCount.fetch_sub(slots[recycledChunkSlot].solidVoxels, std::memory_order_relaxed);
            generatedChunkCount.fetch_sub(1u, std::memory_order_relaxed);
        }

        releaseChunkSlotBricksNoDirty(recycledChunkSlot);

        Chunk& chunk = slots[recycledChunkSlot].chunk;
        chunk.setChunkCoordinate(newChunkOrigin + glm::ivec3(chunkCoordFromWindowIndex(localWindowIndex)));
        chunk.resetBrickMapToAirNoCallbacks();
        slots[recycledChunkSlot].solidVoxels = 0u;
        {
            std::lock_guard<std::mutex> stateLock(chunkStateMutex);
            slots[recycledChunkSlot].state = ChunkRuntimeState::Ungenerated;
        }

        recycledSlotsNeedingBrickMapUpload.push_back(recycledChunkSlot);
        enteringWindowIndices.push_back(static_cast<uint32_t>(localWindowIndex));
    }

    {
        std::lock_guard<std::mutex> dirtyLock(dirtyStateMutex);
        for (size_t localWindowIndex = 0; localWindowIndex < windowChunkCount; localWindowIndex++) {
            if (window[localWindowIndex] != nextWindow[localWindowIndex]) {
                dirty.window[localWindowIndex] = 1u;
            }
        }

        for (uint32_t recycledChunkSlot : recycledSlotsNeedingBrickMapUpload) {
            dirty.brickMaps[recycledChunkSlot] = 1u;
        }
    }

    window = std::move(nextWindow);
    chunkOrigin = newChunkOrigin;
    return enteringWindowIndices;
}

std::vector<uint32_t> VoxelWorld::centerChunkWindow(glm::ivec3 centerChunk) {
    const glm::ivec3 targetChunkOrigin(
        centerChunk.x - static_cast<int32_t>(chunkCounts.x / 2u),
        centerChunk.y - static_cast<int32_t>(chunkCounts.y / 2u),
        centerChunk.z - static_cast<int32_t>(chunkCounts.z / 2u)
    );

    return shiftChunkWindow(targetChunkOrigin - chunkOrigin);
}

std::vector<uint32_t> VoxelWorld::centerChunkWindow(glm::ivec2 centerChunkXZ) {
    const glm::ivec3 targetChunkOrigin(
        centerChunkXZ.x - static_cast<int32_t>(chunkCounts.x / 2u),
        chunkOrigin.y,
        centerChunkXZ.y - static_cast<int32_t>(chunkCounts.z / 2u)
    );

    return shiftChunkWindow(targetChunkOrigin - chunkOrigin);
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

uint32_t VoxelWorld::allocateBrick() {
    std::lock_guard<std::mutex> brickPoolLock(brickPoolMutex);

    if (brickPool.free.empty()) {
        throw std::runtime_error("brick pool exhausted");
    }

    const uint32_t brickIndex = brickPool.free.back();
    brickPool.free.pop_back();
    return brickIndex;
}

void VoxelWorld::releaseChunkSlotBricksNoDirty(size_t chunkSlotIndex) {
    const Chunk::EncodedBrickMap& brickMap = slots.at(chunkSlotIndex).chunk.getBrickMap();

    std::lock_guard<std::mutex> brickPoolLock(brickPoolMutex);
    for (const BrickMapEntry& entry : brickMap) {
        if (entry.index == BRICK_MAP_EMPTY || entry.index >= brickPool.bricks.size()) {
            continue;
        }

        brickPool.free.push_back(entry.index);
    }
}

void VoxelWorld::releaseBrick(uint32_t brickIndex) {
    if (brickIndex >= brickPool.bricks.size()) {
        return;
    }

    std::lock_guard<std::mutex> brickPoolLock(brickPoolMutex);
    brickPool.free.push_back(brickIndex);
}

void VoxelWorld::onChunkBrickMapDirty(size_t dirtyChunkIndex, uint32_t mapIndex) {
    (void)mapIndex;
    if (dirtyChunkIndex >= dirty.brickMaps.size()) {
        return;
    }

    std::lock_guard<std::mutex> dirtyLock(dirtyStateMutex);
    dirty.brickMaps[dirtyChunkIndex] = 1u;
}

void VoxelWorld::onBrickPoolDirty(uint32_t brickIndex) {
    if (brickIndex >= dirty.brickPool.size()) {
        return;
    }

    std::lock_guard<std::mutex> dirtyLock(dirtyStateMutex);
    dirty.brickPool[brickIndex] = 1u;
}

size_t VoxelWorld::getChunkSlotIndexByWindowIndex(size_t localWindowIndex) const {
    return window.at(localWindowIndex);
}

ChunkRuntimeState VoxelWorld::chunkStateBySlotIndex(size_t chunkSlotIndex) const {
    std::lock_guard<std::mutex> stateLock(chunkStateMutex);
    return slots.at(chunkSlotIndex).state;
}

ChunkRuntimeState VoxelWorld::getChunkStateByWindowIndex(size_t localWindowIndex) const {
    return chunkStateBySlotIndex(getChunkSlotIndexByWindowIndex(localWindowIndex));
}

bool VoxelWorld::isChunkSlotGenerated(size_t chunkSlotIndex) const {
    return chunkStateBySlotIndex(chunkSlotIndex) == ChunkRuntimeState::Generated;
}

bool VoxelWorld::isChunkGeneratedByWindowIndex(size_t localWindowIndex) const {
    return isChunkSlotGenerated(getChunkSlotIndexByWindowIndex(localWindowIndex));
}

bool VoxelWorld::tryBeginChunkGeneration(size_t localWindowIndex, uint32_t& outChunkSlotIndex) {
    outChunkSlotIndex = static_cast<uint32_t>(getChunkSlotIndexByWindowIndex(localWindowIndex));
    std::lock_guard<std::mutex> stateLock(chunkStateMutex);
    if (slots[outChunkSlotIndex].state != ChunkRuntimeState::Ungenerated) {
        return false;
    }

    slots[outChunkSlotIndex].state = ChunkRuntimeState::Generating;
    return true;
}

void VoxelWorld::publishChunkGeneration(uint32_t chunkSlotIndex, uint64_t solidVoxelCount, const std::vector<uint32_t>& touchedBrickIndices) {
    slots[chunkSlotIndex].solidVoxels = solidVoxelCount;

    std::lock_guard<std::mutex> dirtyLock(dirtyStateMutex);
    std::lock_guard<std::mutex> stateLock(chunkStateMutex);

    dirty.brickMaps[chunkSlotIndex] = 1u;

    for (uint32_t brickIndex : touchedBrickIndices) {
        if (brickIndex < dirty.brickPool.size()) {
            dirty.brickPool[brickIndex] = 1u;
        }
    }

    slots[chunkSlotIndex].state = ChunkRuntimeState::Generated;
    totalSolidVoxelCount.fetch_add(solidVoxelCount, std::memory_order_relaxed);
    generatedChunkCount.fetch_add(1u, std::memory_order_relaxed);
}

void VoxelWorld::abortChunkGeneration(uint32_t chunkSlotIndex, const std::vector<uint32_t>& allocatedBrickIndices) {
    {
        std::lock_guard<std::mutex> brickPoolLock(brickPoolMutex);
        for (uint32_t brickIndex : allocatedBrickIndices) {
            if (brickIndex >= brickPool.bricks.size()) {
                continue;
            }

            brickPool.free.push_back(brickIndex);
        }
    }

    slots[chunkSlotIndex].chunk.resetBrickMapToAirNoCallbacks();
    slots[chunkSlotIndex].solidVoxels = 0u;
    {
        std::lock_guard<std::mutex> stateLock(chunkStateMutex);
        slots[chunkSlotIndex].state = ChunkRuntimeState::Ungenerated;
    }
}

uint64_t VoxelWorld::getTotalSolidVoxelCount() const {
    return totalSolidVoxelCount.load(std::memory_order_relaxed);
}

size_t VoxelWorld::getGeneratedChunkCount() const {
    return generatedChunkCount.load(std::memory_order_relaxed);
}

size_t VoxelWorld::getAllocatedBrickCount() const {
    std::lock_guard<std::mutex> brickPoolLock(brickPoolMutex);
    return brickPool.bricks.size() - brickPool.free.size();
}

const Brick& VoxelWorld::getBrickByIndex(uint32_t brickIndex) const {
    return brickPool.bricks.at(brickIndex);
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
    return glm::uvec3(
        static_cast<uint32_t>(remainderAfterZ % chunkCounts.x),
        static_cast<uint32_t>(remainderAfterZ / chunkCounts.x),
        z
    );
}

size_t VoxelWorld::chunkSlotIndexFromVoxel(uint32_t x, uint32_t y, uint32_t z, glm::uvec3& outLocalVoxel) const {
    const glm::ivec3 voxelMin = getVoxelMin();
    outLocalVoxel = glm::uvec3(
        static_cast<uint32_t>(static_cast<int32_t>(x) - voxelMin.x),
        static_cast<uint32_t>(static_cast<int32_t>(y) - voxelMin.y),
        static_cast<uint32_t>(static_cast<int32_t>(z) - voxelMin.z)
    );
    return getChunkSlotIndexByWindowIndex(chunkIndex(
        outLocalVoxel.x / Chunk::SIZE,
        outLocalVoxel.y / Chunk::SIZE,
        outLocalVoxel.z / Chunk::SIZE
    ));
}
