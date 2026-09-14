#pragma once

#include "chunk.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

//laaame forward declarations
class VoxelWorld;
struct TerrainBuildResult;
struct GpuVoxelBuffers;
struct GpuWorldDiff;
void buildTerrainChunk(VoxelWorld& world, uint32_t chunkSlotIndex, const glm::uvec3& voxelDimensions, TerrainBuildResult& outResult);
GpuVoxelBuffers buildGpuVoxelBuffers(const VoxelWorld& world);
GpuWorldDiff buildGpuWorldDiff(VoxelWorld& world);
void clearGpuUploadDirtyState(VoxelWorld& world);

//state for each chunk entry
enum class ChunkRuntimeState : uint8_t {
    Ungenerated = 0,
    Generating = 1,
    Generated = 2,
};

//main
class VoxelWorld {
public:
    explicit VoxelWorld(glm::uvec3 chunkCounts);
    VoxelWorld(const VoxelWorld&) = delete;
    VoxelWorld& operator=(const VoxelWorld&) = delete;
    VoxelWorld(VoxelWorld&&) = delete;
    VoxelWorld& operator=(VoxelWorld&&) = delete;

    glm::uvec3 getChunkCounts() const { return chunkCounts; }
    size_t getChunkCount() const { return window.size(); }
    glm::ivec3 getVoxelMin() const;
    glm::ivec3 getVoxelMax() const;
    glm::uvec3 getVoxelDimensions() const;

    Chunk& getChunk(uint32_t x, uint32_t y, uint32_t z);
    const Chunk& getChunk(uint32_t x, uint32_t y, uint32_t z) const;
    Chunk& getChunkByWindowIndex(size_t localWindowIndex);
    const Chunk& getChunkByWindowIndex(size_t localWindowIndex) const;
    const Chunk& getChunkBySlotIndex(size_t chunkSlotIndex) const;

    bool containsVoxel(int32_t x, int32_t y, int32_t z) const;
    uint32_t getVoxel(uint32_t x, uint32_t y, uint32_t z) const;
    void setVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t value);

    std::vector<uint32_t> shiftChunkWindow(glm::ivec3 deltaChunks);
    std::vector<uint32_t> centerChunkWindow(glm::ivec2 centerChunkXZ);
    std::vector<uint32_t> centerChunkWindow(glm::ivec3 centerChunk);

    ChunkRuntimeState getChunkStateByWindowIndex(size_t localWindowIndex) const;
    bool isChunkGeneratedByWindowIndex(size_t localWindowIndex) const;
    bool tryBeginChunkGeneration(size_t localWindowIndex, uint32_t& outChunkSlotIndex);
    void publishChunkGeneration(uint32_t chunkSlotIndex, uint64_t solidVoxelCount, const std::vector<uint32_t>& touchedBrickIndices);
    void abortChunkGeneration(uint32_t chunkSlotIndex, const std::vector<uint32_t>& allocatedBrickIndices);

    size_t getBrickCapacity() const { return brickPool.bricks.size(); }
    size_t getAllocatedBrickCount() const;
    const Brick& getBrickByIndex(uint32_t brickIndex) const;

    uint64_t getTotalSolidVoxelCount() const;
    size_t getGeneratedChunkCount() const;

private:
    friend void buildTerrainChunk(VoxelWorld& world, uint32_t chunkSlotIndex, const glm::uvec3& voxelDimensions, TerrainBuildResult& outResult);
    friend GpuVoxelBuffers buildGpuVoxelBuffers(const VoxelWorld& world);
    friend GpuWorldDiff buildGpuWorldDiff(VoxelWorld& world);
    friend void clearGpuUploadDirtyState(VoxelWorld& world);

    Chunk& getChunkByMutableSlotIndex(size_t chunkSlotIndex);
    size_t getChunkSlotIndexByWindowIndex(size_t localWindowIndex) const;
    size_t chunkIndex(uint32_t x, uint32_t y, uint32_t z) const;
    glm::uvec3 chunkCoordFromWindowIndex(size_t localWindowIndex) const;
    size_t chunkSlotIndexFromVoxel(uint32_t x, uint32_t y, uint32_t z, glm::uvec3& outLocalVoxel) const;
    ChunkRuntimeState chunkStateBySlotIndex(size_t chunkSlotIndex) const;
    bool isChunkSlotGenerated(size_t chunkSlotIndex) const;
    void releaseChunkSlotBricksNoDirty(size_t chunkSlotIndex);

    uint32_t allocateBrick();
    void releaseBrick(uint32_t brickIndex);
    void onChunkBrickMapDirty(size_t chunkIndex, uint32_t mapIndex);
    void onBrickPoolDirty(uint32_t brickIndex);

    struct ChunkSlot {
        Chunk chunk;
        uint64_t solidVoxels = 0;
        ChunkRuntimeState state = ChunkRuntimeState::Ungenerated;
    };

    struct BrickPool {
        std::vector<Brick> bricks;
        std::vector<uint32_t> free;
    };

    struct GpuDirtyState {
        std::vector<uint8_t> window;
        std::vector<uint8_t> brickMaps;
        std::vector<uint8_t> brickPool;
    };

    //chunk window dimensions
    glm::uvec3 chunkCounts{};
    glm::ivec3 chunkOrigin{0};
    //a pool where each slot is chunk data + non-chunk owned metadata
    std::vector<ChunkSlot> slots;
    //local position index (VoxelWorld::chunkIndex()) -> slotIndex, indirection buffer later
    std::vector<uint32_t> window;
    //dynamic array of bricks + free list
    BrickPool brickPool;
    //tracks dirty brickMaps/bricks/wnidow entries for speedy local updates (only a madman would reconstruct the entire buffer)
    GpuDirtyState dirty;

    //mutex for worldgen thread, makes sure no race condition oopsies
    mutable std::mutex brickPoolMutex, dirtyStateMutex, chunkStateMutex;
    //just diagnostics, can be changed both by player (main thread) and world gen (2nd thread), so atomic ops
    std::atomic<uint64_t> totalSolidVoxelCount{0};
    std::atomic<size_t> generatedChunkCount{0};
};
