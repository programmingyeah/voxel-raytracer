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

VoxelWorld::VoxelWorld(glm::uvec3 inChunkCounts) {
    if (inChunkCounts.x == 0 || inChunkCounts.y == 0 || inChunkCounts.z == 0) {
        throw std::runtime_error("voxel world must contain at least one chunk in every dimension");
    }

    const size_t totalChunkCount = static_cast<size_t>(inChunkCounts.x) * inChunkCounts.y * inChunkCounts.z;
    const size_t worstCaseBrickCapacity = totalChunkCount * Chunk::BRICK_COUNT * WORLD_LOD_COUNT;
    const size_t brickCapacity = scaledBrickCapacity(worstCaseBrickCapacity);

    brickPool.bricks.assign(brickCapacity, Brick{});
    brickPool.free.reserve(brickCapacity);
    for (size_t brickIndex = brickCapacity; brickIndex > 0; brickIndex--) {
        brickPool.free.push_back(static_cast<uint32_t>(brickIndex - 1u));
    }

    for (size_t lodValue = 0; lodValue < WORLD_LOD_COUNT; lodValue++) {
        WorldLodStorage& lodStorage = lods[lodValue];
        lodStorage.chunkCounts = inChunkCounts;
        lodStorage.slots.reserve(totalChunkCount);
        lodStorage.window.resize(totalChunkCount);
        std::iota(lodStorage.window.begin(), lodStorage.window.end(), 0u);

        for (uint32_t z = 0; z < lodStorage.chunkCounts.z; z++) {
            for (uint32_t y = 0; y < lodStorage.chunkCounts.y; y++) {
                for (uint32_t x = 0; x < lodStorage.chunkCounts.x; x++) {
                    const size_t currentChunkIndex = lodStorage.slots.size();
                    lodStorage.slots.push_back({
                        Chunk(
                            lodStorage.chunkOrigin + glm::ivec3(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)),
                            currentChunkIndex
                        )
                    });
                    lodStorage.slots.back().chunk.bindStorage(
                        &brickPool.bricks,
                        [this]() { return allocateBrick(); },
                        [this](uint32_t brickIndex) { releaseBrick(brickIndex); },
                        [this, lodValue](size_t dirtyChunkIndex, uint32_t mapIndex) {
                            onChunkBrickMapDirty(static_cast<WorldLod>(lodValue), dirtyChunkIndex, mapIndex);
                        },
                        [this](uint32_t brickIndex) { onBrickPoolDirty(brickIndex); }
                    );
                }
            }
        }

        lodStorage.dirty.window.assign(totalChunkCount, 0u);
        lodStorage.dirty.brickMaps.assign(lodStorage.slots.size(), 0u);
        lodStorage.dirty.brickPool.assign(brickPool.bricks.size(), 0u);
    }
}

glm::uvec3 VoxelWorld::getChunkCounts(WorldLod lod) const {
    return storage(lod).chunkCounts;
}

size_t VoxelWorld::getChunkCount(WorldLod lod) const {
    return storage(lod).window.size();
}

uint32_t VoxelWorld::getLodScale(WorldLod lod) const {
    const size_t index = static_cast<size_t>(lod);
    if (index >= WORLD_LOD_COUNT) {
        throw std::runtime_error("invalid world LOD");
    }

    return 1u << index;
}

uint32_t VoxelWorld::getChunkWorldSpan(WorldLod lod) const {
    return Chunk::SIZE * getLodScale(lod);
}

uint32_t VoxelWorld::getLocalCellWorldStep(WorldLod lod) const {
    return getLodScale(lod);
}

glm::ivec3 VoxelWorld::getChunkOrigin(WorldLod lod) const {
    return storage(lod).chunkOrigin;
}

glm::ivec3 VoxelWorld::getVoxelMin(WorldLod lod) const {
    return storage(lod).chunkOrigin * static_cast<int32_t>(getChunkWorldSpan(lod));
}

glm::ivec3 VoxelWorld::getVoxelMax(WorldLod lod) const {
    return getVoxelMin(lod) + glm::ivec3(getVoxelDimensions(lod));
}

glm::uvec3 VoxelWorld::getVoxelDimensions(WorldLod lod) const {
    return storage(lod).chunkCounts * getChunkWorldSpan(lod);
}

uint32_t VoxelWorld::getTerrainWorldHeight() const {
    return getChunkCounts(WorldLod::Lod0).y * Chunk::SIZE;
}

glm::ivec3 VoxelWorld::getChunkWorldMin(WorldLod lod, const glm::ivec3& chunkCoordinate) const {
    return chunkCoordinate * static_cast<int32_t>(getChunkWorldSpan(lod));
}

glm::ivec3 VoxelWorld::chunkCoordFromWorldPosition(WorldLod lod, const glm::vec3& position) const {
    const glm::vec3 chunkPosition = glm::floor(position / static_cast<float>(getChunkWorldSpan(lod)));
    return glm::ivec3(
        static_cast<int32_t>(chunkPosition.x),
        static_cast<int32_t>(chunkPosition.y),
        static_cast<int32_t>(chunkPosition.z)
    );
}

glm::ivec2 VoxelWorld::chunkXZFromWorldPosition(WorldLod lod, const glm::vec3& position) const {
    const glm::ivec3 chunkCoordinate = chunkCoordFromWorldPosition(lod, position);
    return glm::ivec2(chunkCoordinate.x, chunkCoordinate.z);
}

Chunk& VoxelWorld::getChunk(WorldLod lod, uint32_t x, uint32_t y, uint32_t z) {
    return getChunkByWindowIndex(lod, chunkIndex(lod, x, y, z));
}

const Chunk& VoxelWorld::getChunk(WorldLod lod, uint32_t x, uint32_t y, uint32_t z) const {
    return getChunkByWindowIndex(lod, chunkIndex(lod, x, y, z));
}

Chunk& VoxelWorld::getChunkByWindowIndex(WorldLod lod, size_t localWindowIndex) {
    WorldLodStorage& lodStorage = storage(lod);
    return lodStorage.slots.at(lodStorage.window.at(localWindowIndex)).chunk;
}

const Chunk& VoxelWorld::getChunkByWindowIndex(WorldLod lod, size_t localWindowIndex) const {
    const WorldLodStorage& lodStorage = storage(lod);
    return lodStorage.slots.at(lodStorage.window.at(localWindowIndex)).chunk;
}

Chunk& VoxelWorld::getChunkByMutableSlotIndex(WorldLod lod, size_t chunkSlotIndex) {
    return storage(lod).slots.at(chunkSlotIndex).chunk;
}

const Chunk& VoxelWorld::getChunkBySlotIndex(WorldLod lod, size_t chunkSlotIndex) const {
    return storage(lod).slots.at(chunkSlotIndex).chunk;
}

bool VoxelWorld::containsVoxel(WorldLod lod, int32_t x, int32_t y, int32_t z) const {
    const glm::ivec3 voxelMin = getVoxelMin(lod);
    const glm::ivec3 voxelMax = getVoxelMax(lod);
    return x >= voxelMin.x && y >= voxelMin.y && z >= voxelMin.z &&
           x < voxelMax.x && y < voxelMax.y && z < voxelMax.z;
}

uint32_t VoxelWorld::getVoxel(uint32_t x, uint32_t y, uint32_t z) const {
    glm::uvec3 localVoxel{};
    const size_t chunkSlotIndex = chunkSlotIndexFromVoxel(WorldLod::Lod0, x, y, z, localVoxel);
    return getChunkBySlotIndex(WorldLod::Lod0, chunkSlotIndex).get(
        localVoxel.x % Chunk::SIZE,
        localVoxel.y % Chunk::SIZE,
        localVoxel.z % Chunk::SIZE
    );
}

void VoxelWorld::setVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t value) {
    glm::uvec3 localVoxel{};
    const size_t chunkSlotIndex = chunkSlotIndexFromVoxel(WorldLod::Lod0, x, y, z, localVoxel);
    getChunkByMutableSlotIndex(WorldLod::Lod0, chunkSlotIndex).set(
        localVoxel.x % Chunk::SIZE,
        localVoxel.y % Chunk::SIZE,
        localVoxel.z % Chunk::SIZE,
        value
    );
}

std::vector<uint32_t> VoxelWorld::shiftChunkWindow(WorldLod lod, glm::ivec3 deltaChunks) {
    if (deltaChunks == glm::ivec3(0)) {
        return {};
    }

    WorldLodStorage& lodStorage = storage(lod);
    const glm::ivec3 newChunkOrigin = lodStorage.chunkOrigin + deltaChunks;
    const size_t windowChunkCount = lodStorage.window.size();
    std::vector<uint32_t> nextWindow(windowChunkCount, INVALID_CHUNK_SLOT);
    std::vector<uint32_t> enteringWindowIndices;
    std::vector<uint32_t> recycledChunkSlots;
    recycledChunkSlots.reserve(windowChunkCount);

    for (size_t localWindowIndex = 0; localWindowIndex < windowChunkCount; localWindowIndex++) {
        const glm::ivec3 nextChunkCoordinate = newChunkOrigin + glm::ivec3(chunkCoordFromWindowIndex(lod, localWindowIndex));
        const glm::ivec3 previousLocalCoordinate = nextChunkCoordinate - lodStorage.chunkOrigin;
        const bool overlapsPreviousWindow =
            previousLocalCoordinate.x >= 0 && previousLocalCoordinate.y >= 0 && previousLocalCoordinate.z >= 0 &&
            previousLocalCoordinate.x < static_cast<int32_t>(lodStorage.chunkCounts.x) &&
            previousLocalCoordinate.y < static_cast<int32_t>(lodStorage.chunkCounts.y) &&
            previousLocalCoordinate.z < static_cast<int32_t>(lodStorage.chunkCounts.z);

        if (!overlapsPreviousWindow) {
            continue;
        }

        nextWindow[localWindowIndex] = static_cast<uint32_t>(getChunkSlotIndexByWindowIndex(
            lod,
            chunkIndex(
                lod,
                static_cast<uint32_t>(previousLocalCoordinate.x),
                static_cast<uint32_t>(previousLocalCoordinate.y),
                static_cast<uint32_t>(previousLocalCoordinate.z)
            )
        ));
    }

    for (uint32_t chunkSlotIndex : lodStorage.window) {
        const bool stillReferenced = std::find(nextWindow.begin(), nextWindow.end(), chunkSlotIndex) != nextWindow.end();
        if (stillReferenced) {
            continue;
        }

        if (chunkStateBySlotIndex(lod, chunkSlotIndex) == ChunkRuntimeState::Generating) {
            return {};
        }

        recycledChunkSlots.push_back(chunkSlotIndex);
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

        if (isChunkSlotGenerated(lod, recycledChunkSlot)) {
            lodStorage.totalSolidVoxelCount.fetch_sub(lodStorage.slots[recycledChunkSlot].solidVoxels, std::memory_order_relaxed);
            lodStorage.generatedChunkCount.fetch_sub(1u, std::memory_order_relaxed);
        }

        releaseChunkSlotBricksNoDirty(lod, recycledChunkSlot);

        Chunk& chunk = lodStorage.slots[recycledChunkSlot].chunk;
        chunk.setChunkCoordinate(newChunkOrigin + glm::ivec3(chunkCoordFromWindowIndex(lod, localWindowIndex)));
        chunk.resetBrickMapToAirNoCallbacks();
        lodStorage.slots[recycledChunkSlot].solidVoxels = 0u;
        {
            std::lock_guard<std::mutex> stateLock(lodStorage.chunkStateMutex);
            lodStorage.slots[recycledChunkSlot].state = ChunkRuntimeState::Ungenerated;
        }

        recycledSlotsNeedingBrickMapUpload.push_back(recycledChunkSlot);
        enteringWindowIndices.push_back(static_cast<uint32_t>(localWindowIndex));
    }

    {
        std::lock_guard<std::mutex> dirtyLock(lodStorage.dirtyStateMutex);
        for (size_t localWindowIndex = 0; localWindowIndex < windowChunkCount; localWindowIndex++) {
            if (lodStorage.window[localWindowIndex] != nextWindow[localWindowIndex]) {
                lodStorage.dirty.window[localWindowIndex] = 1u;
            }
        }

        for (uint32_t recycledChunkSlot : recycledSlotsNeedingBrickMapUpload) {
            lodStorage.dirty.brickMaps[recycledChunkSlot] = 1u;
        }
    }

    lodStorage.window = std::move(nextWindow);
    lodStorage.chunkOrigin = newChunkOrigin;
    return enteringWindowIndices;
}

std::vector<uint32_t> VoxelWorld::centerChunkWindow(WorldLod lod, glm::ivec3 centerChunk) {
    const WorldLodStorage& lodStorage = storage(lod);
    const glm::ivec3 targetChunkOrigin(
        centerChunk.x - static_cast<int32_t>(lodStorage.chunkCounts.x / 2u),
        centerChunk.y - static_cast<int32_t>(lodStorage.chunkCounts.y / 2u),
        centerChunk.z - static_cast<int32_t>(lodStorage.chunkCounts.z / 2u)
    );

    return shiftChunkWindow(lod, targetChunkOrigin - lodStorage.chunkOrigin);
}

std::vector<uint32_t> VoxelWorld::centerChunkWindow(WorldLod lod, glm::ivec2 centerChunkXZ) {
    const WorldLodStorage& lodStorage = storage(lod);
    const glm::ivec3 targetChunkOrigin(
        centerChunkXZ.x - static_cast<int32_t>(lodStorage.chunkCounts.x / 2u),
        lodStorage.chunkOrigin.y,
        centerChunkXZ.y - static_cast<int32_t>(lodStorage.chunkCounts.z / 2u)
    );

    return shiftChunkWindow(lod, targetChunkOrigin - lodStorage.chunkOrigin);
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

void VoxelWorld::releaseChunkSlotBricksNoDirty(WorldLod lod, size_t chunkSlotIndex) {
    const Chunk::EncodedBrickMap& brickMap = storage(lod).slots.at(chunkSlotIndex).chunk.getBrickMap();

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

void VoxelWorld::onChunkBrickMapDirty(WorldLod lod, size_t dirtyChunkIndex, uint32_t mapIndex) {
    (void)mapIndex;
    WorldLodStorage& lodStorage = storage(lod);
    if (dirtyChunkIndex >= lodStorage.dirty.brickMaps.size()) {
        return;
    }

    std::lock_guard<std::mutex> dirtyLock(lodStorage.dirtyStateMutex);
    lodStorage.dirty.brickMaps[dirtyChunkIndex] = 1u;
}

void VoxelWorld::onBrickPoolDirty(uint32_t brickIndex) {
    for (WorldLodStorage& lodStorage : lods) {
        if (brickIndex >= lodStorage.dirty.brickPool.size()) {
            continue;
        }

        std::lock_guard<std::mutex> dirtyLock(lodStorage.dirtyStateMutex);
        lodStorage.dirty.brickPool[brickIndex] = 1u;
    }
}

size_t VoxelWorld::getChunkSlotIndexByWindowIndex(WorldLod lod, size_t localWindowIndex) const {
    return storage(lod).window.at(localWindowIndex);
}

ChunkRuntimeState VoxelWorld::chunkStateBySlotIndex(WorldLod lod, size_t chunkSlotIndex) const {
    const WorldLodStorage& lodStorage = storage(lod);
    std::lock_guard<std::mutex> stateLock(lodStorage.chunkStateMutex);
    return lodStorage.slots.at(chunkSlotIndex).state;
}

ChunkRuntimeState VoxelWorld::getChunkStateByWindowIndex(WorldLod lod, size_t localWindowIndex) const {
    return chunkStateBySlotIndex(lod, getChunkSlotIndexByWindowIndex(lod, localWindowIndex));
}

bool VoxelWorld::isChunkSlotGenerated(WorldLod lod, size_t chunkSlotIndex) const {
    return chunkStateBySlotIndex(lod, chunkSlotIndex) == ChunkRuntimeState::Generated;
}

bool VoxelWorld::isChunkGeneratedByWindowIndex(WorldLod lod, size_t localWindowIndex) const {
    return isChunkSlotGenerated(lod, getChunkSlotIndexByWindowIndex(lod, localWindowIndex));
}

bool VoxelWorld::tryBeginChunkGeneration(WorldLod lod, size_t localWindowIndex, uint32_t& outChunkSlotIndex) {
    WorldLodStorage& lodStorage = storage(lod);
    outChunkSlotIndex = static_cast<uint32_t>(getChunkSlotIndexByWindowIndex(lod, localWindowIndex));
    std::lock_guard<std::mutex> stateLock(lodStorage.chunkStateMutex);
    if (lodStorage.slots[outChunkSlotIndex].state != ChunkRuntimeState::Ungenerated) {
        return false;
    }

    lodStorage.slots[outChunkSlotIndex].state = ChunkRuntimeState::Generating;
    return true;
}

void VoxelWorld::publishChunkGeneration(WorldLod lod, uint32_t chunkSlotIndex, uint64_t solidVoxelCount, const std::vector<uint32_t>& touchedBrickIndices) {
    WorldLodStorage& lodStorage = storage(lod);
    lodStorage.slots[chunkSlotIndex].solidVoxels = solidVoxelCount;

    std::lock_guard<std::mutex> dirtyLock(lodStorage.dirtyStateMutex);
    std::lock_guard<std::mutex> stateLock(lodStorage.chunkStateMutex);

    lodStorage.dirty.brickMaps[chunkSlotIndex] = 1u;

    for (uint32_t brickIndex : touchedBrickIndices) {
        if (brickIndex < lodStorage.dirty.brickPool.size()) {
            lodStorage.dirty.brickPool[brickIndex] = 1u;
        }
    }

    lodStorage.slots[chunkSlotIndex].state = ChunkRuntimeState::Generated;
    lodStorage.totalSolidVoxelCount.fetch_add(solidVoxelCount, std::memory_order_relaxed);
    lodStorage.generatedChunkCount.fetch_add(1u, std::memory_order_relaxed);
}

void VoxelWorld::abortChunkGeneration(WorldLod lod, uint32_t chunkSlotIndex, const std::vector<uint32_t>& allocatedBrickIndices) {
    {
        std::lock_guard<std::mutex> brickPoolLock(brickPoolMutex);
        for (uint32_t brickIndex : allocatedBrickIndices) {
            if (brickIndex >= brickPool.bricks.size()) {
                continue;
            }

            brickPool.free.push_back(brickIndex);
        }
    }

    WorldLodStorage& lodStorage = storage(lod);
    lodStorage.slots[chunkSlotIndex].chunk.resetBrickMapToAirNoCallbacks();
    lodStorage.slots[chunkSlotIndex].solidVoxels = 0u;
    {
        std::lock_guard<std::mutex> stateLock(lodStorage.chunkStateMutex);
        lodStorage.slots[chunkSlotIndex].state = ChunkRuntimeState::Ungenerated;
    }
}

uint64_t VoxelWorld::getTotalSolidVoxelCount(WorldLod lod) const {
    return storage(lod).totalSolidVoxelCount.load(std::memory_order_relaxed);
}

size_t VoxelWorld::getGeneratedChunkCount(WorldLod lod) const {
    return storage(lod).generatedChunkCount.load(std::memory_order_relaxed);
}

size_t VoxelWorld::getAllocatedBrickCount() const {
    std::lock_guard<std::mutex> brickPoolLock(brickPoolMutex);
    return brickPool.bricks.size() - brickPool.free.size();
}

const Brick& VoxelWorld::getBrickByIndex(uint32_t brickIndex) const {
    return brickPool.bricks.at(brickIndex);
}

size_t VoxelWorld::chunkIndex(WorldLod lod, uint32_t x, uint32_t y, uint32_t z) const {
    const glm::uvec3 chunkCounts = getChunkCounts(lod);
    if (x >= chunkCounts.x || y >= chunkCounts.y || z >= chunkCounts.z) {
        throw std::out_of_range("voxel chunk index out of range");
    }

    return static_cast<size_t>(x) +
           static_cast<size_t>(chunkCounts.x) *
               (static_cast<size_t>(y) + static_cast<size_t>(chunkCounts.y) * static_cast<size_t>(z));
}

glm::uvec3 VoxelWorld::chunkCoordFromWindowIndex(WorldLod lod, size_t localWindowIndex) const {
    const glm::uvec3 chunkCounts = getChunkCounts(lod);
    const size_t layerSize = static_cast<size_t>(chunkCounts.x) * chunkCounts.y;
    const uint32_t z = static_cast<uint32_t>(localWindowIndex / layerSize);
    const size_t remainderAfterZ = localWindowIndex % layerSize;
    return glm::uvec3(
        static_cast<uint32_t>(remainderAfterZ % chunkCounts.x),
        static_cast<uint32_t>(remainderAfterZ / chunkCounts.x),
        z
    );
}

size_t VoxelWorld::chunkSlotIndexFromVoxel(WorldLod lod, uint32_t x, uint32_t y, uint32_t z, glm::uvec3& outLocalVoxel) const {
    const glm::ivec3 voxelMin = getVoxelMin(lod);
    outLocalVoxel = glm::uvec3(
        static_cast<uint32_t>(static_cast<int32_t>(x) - voxelMin.x),
        static_cast<uint32_t>(static_cast<int32_t>(y) - voxelMin.y),
        static_cast<uint32_t>(static_cast<int32_t>(z) - voxelMin.z)
    );
    return getChunkSlotIndexByWindowIndex(lod, chunkIndex(
        lod,
        outLocalVoxel.x / Chunk::SIZE,
        outLocalVoxel.y / Chunk::SIZE,
        outLocalVoxel.z / Chunk::SIZE
    ));
}