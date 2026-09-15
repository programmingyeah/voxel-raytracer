#pragma once

#include "chunk.hpp"

#include <array>
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

enum class WorldLod : uint8_t {
    Lod0 = 0,
    Lod1 = 1,
    Lod2 = 2,
    Lod3 = 3,
    Lod4 = 4,
    Lod5 = 5,
    Lod6 = 6,
    Lod7 = 7,
};

inline constexpr size_t WORLD_LOD_COUNT = 8u;

void buildTerrainChunk(VoxelWorld& world, WorldLod lod, uint32_t chunkSlotIndex, uint32_t terrainWorldHeight, TerrainBuildResult& outResult);
GpuVoxelBuffers buildGpuVoxelBuffers(const VoxelWorld& world, WorldLod lod);
GpuWorldDiff buildGpuWorldDiff(VoxelWorld& world, WorldLod lod);
void clearGpuUploadDirtyState(VoxelWorld& world, WorldLod lod);

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

    glm::uvec3 getChunkCounts() const { return getChunkCounts(WorldLod::Lod0); }
    glm::uvec3 getChunkCounts(WorldLod lod) const;
    size_t getChunkCount() const { return getChunkCount(WorldLod::Lod0); }
    size_t getChunkCount(WorldLod lod) const;
    uint32_t getLodScale(WorldLod lod) const;
    uint32_t getChunkWorldSpan(WorldLod lod) const;
    uint32_t getLocalCellWorldStep(WorldLod lod) const;
    glm::ivec3 getChunkOrigin(WorldLod lod) const;
    glm::ivec3 getVoxelMin() const { return getVoxelMin(WorldLod::Lod0); }
    glm::ivec3 getVoxelMin(WorldLod lod) const;
    glm::ivec3 getVoxelMax() const { return getVoxelMax(WorldLod::Lod0); }
    glm::ivec3 getVoxelMax(WorldLod lod) const;
    glm::uvec3 getVoxelDimensions() const { return getVoxelDimensions(WorldLod::Lod0); }
    glm::uvec3 getVoxelDimensions(WorldLod lod) const;
    uint32_t getTerrainWorldHeight() const;
    glm::ivec3 getChunkWorldMin(WorldLod lod, const glm::ivec3& chunkCoordinate) const;
    glm::ivec3 chunkCoordFromWorldPosition(WorldLod lod, const glm::vec3& position) const;
    glm::ivec2 chunkXZFromWorldPosition(WorldLod lod, const glm::vec3& position) const;

    Chunk& getChunk(uint32_t x, uint32_t y, uint32_t z) { return getChunk(WorldLod::Lod0, x, y, z); }
    const Chunk& getChunk(uint32_t x, uint32_t y, uint32_t z) const { return getChunk(WorldLod::Lod0, x, y, z); }
    Chunk& getChunk(WorldLod lod, uint32_t x, uint32_t y, uint32_t z);
    const Chunk& getChunk(WorldLod lod, uint32_t x, uint32_t y, uint32_t z) const;
    Chunk& getChunkByWindowIndex(size_t localWindowIndex) { return getChunkByWindowIndex(WorldLod::Lod0, localWindowIndex); }
    const Chunk& getChunkByWindowIndex(size_t localWindowIndex) const { return getChunkByWindowIndex(WorldLod::Lod0, localWindowIndex); }
    Chunk& getChunkByWindowIndex(WorldLod lod, size_t localWindowIndex);
    const Chunk& getChunkByWindowIndex(WorldLod lod, size_t localWindowIndex) const;
    const Chunk& getChunkBySlotIndex(size_t chunkSlotIndex) const { return getChunkBySlotIndex(WorldLod::Lod0, chunkSlotIndex); }
    const Chunk& getChunkBySlotIndex(WorldLod lod, size_t chunkSlotIndex) const;

    bool containsVoxel(int32_t x, int32_t y, int32_t z) const { return containsVoxel(WorldLod::Lod0, x, y, z); }
    bool containsVoxel(WorldLod lod, int32_t x, int32_t y, int32_t z) const;
    uint32_t getVoxel(uint32_t x, uint32_t y, uint32_t z) const;
    void setVoxel(uint32_t x, uint32_t y, uint32_t z, uint32_t value);

    std::vector<uint32_t> shiftChunkWindow(glm::ivec3 deltaChunks) { return shiftChunkWindow(WorldLod::Lod0, deltaChunks); }
    std::vector<uint32_t> shiftChunkWindow(WorldLod lod, glm::ivec3 deltaChunks);
    std::vector<uint32_t> centerChunkWindow(glm::ivec2 centerChunkXZ) { return centerChunkWindow(WorldLod::Lod0, centerChunkXZ); }
    std::vector<uint32_t> centerChunkWindow(WorldLod lod, glm::ivec2 centerChunkXZ);
    std::vector<uint32_t> centerChunkWindow(glm::ivec3 centerChunk) { return centerChunkWindow(WorldLod::Lod0, centerChunk); }
    std::vector<uint32_t> centerChunkWindow(WorldLod lod, glm::ivec3 centerChunk);

    ChunkRuntimeState getChunkStateByWindowIndex(size_t localWindowIndex) const { return getChunkStateByWindowIndex(WorldLod::Lod0, localWindowIndex); }
    ChunkRuntimeState getChunkStateByWindowIndex(WorldLod lod, size_t localWindowIndex) const;
    bool isChunkGeneratedByWindowIndex(size_t localWindowIndex) const { return isChunkGeneratedByWindowIndex(WorldLod::Lod0, localWindowIndex); }
    bool isChunkGeneratedByWindowIndex(WorldLod lod, size_t localWindowIndex) const;
    bool tryBeginChunkGeneration(size_t localWindowIndex, uint32_t& outChunkSlotIndex) { return tryBeginChunkGeneration(WorldLod::Lod0, localWindowIndex, outChunkSlotIndex); }
    bool tryBeginChunkGeneration(WorldLod lod, size_t localWindowIndex, uint32_t& outChunkSlotIndex);
    void publishChunkGeneration(uint32_t chunkSlotIndex, uint64_t solidVoxelCount, const std::vector<uint32_t>& touchedBrickIndices) { publishChunkGeneration(WorldLod::Lod0, chunkSlotIndex, solidVoxelCount, touchedBrickIndices); }
    void publishChunkGeneration(WorldLod lod, uint32_t chunkSlotIndex, uint64_t solidVoxelCount, const std::vector<uint32_t>& touchedBrickIndices);
    void abortChunkGeneration(uint32_t chunkSlotIndex, const std::vector<uint32_t>& allocatedBrickIndices) { abortChunkGeneration(WorldLod::Lod0, chunkSlotIndex, allocatedBrickIndices); }
    void abortChunkGeneration(WorldLod lod, uint32_t chunkSlotIndex, const std::vector<uint32_t>& allocatedBrickIndices);

    size_t getBrickCapacity() const { return brickPool.bricks.size(); }
    size_t getAllocatedBrickCount() const;
    const Brick& getBrickByIndex(uint32_t brickIndex) const;

    uint64_t getTotalSolidVoxelCount() const { return getTotalSolidVoxelCount(WorldLod::Lod0); }
    uint64_t getTotalSolidVoxelCount(WorldLod lod) const;
    size_t getGeneratedChunkCount() const { return getGeneratedChunkCount(WorldLod::Lod0); }
    size_t getGeneratedChunkCount(WorldLod lod) const;

private:
    friend void buildTerrainChunk(VoxelWorld& world, WorldLod lod, uint32_t chunkSlotIndex, uint32_t terrainWorldHeight, TerrainBuildResult& outResult);
    friend GpuVoxelBuffers buildGpuVoxelBuffers(const VoxelWorld& world, WorldLod lod);
    friend GpuWorldDiff buildGpuWorldDiff(VoxelWorld& world, WorldLod lod);
    friend void clearGpuUploadDirtyState(VoxelWorld& world, WorldLod lod);

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

    struct WorldLodStorage {
        glm::uvec3 chunkCounts{};
        glm::ivec3 chunkOrigin{0};
        std::vector<ChunkSlot> slots;
        std::vector<uint32_t> window;
        GpuDirtyState dirty;
        mutable std::mutex dirtyStateMutex;
        mutable std::mutex chunkStateMutex;
        std::atomic<uint64_t> totalSolidVoxelCount{0};
        std::atomic<size_t> generatedChunkCount{0};
    };

    static size_t lodIndex(WorldLod lod) { return static_cast<size_t>(lod); }
    WorldLodStorage& storage(WorldLod lod) { return lods.at(lodIndex(lod)); }
    const WorldLodStorage& storage(WorldLod lod) const { return lods.at(lodIndex(lod)); }

    Chunk& getChunkByMutableSlotIndex(WorldLod lod, size_t chunkSlotIndex);
    size_t getChunkSlotIndexByWindowIndex(WorldLod lod, size_t localWindowIndex) const;
    size_t chunkIndex(WorldLod lod, uint32_t x, uint32_t y, uint32_t z) const;
    glm::uvec3 chunkCoordFromWindowIndex(WorldLod lod, size_t localWindowIndex) const;
    size_t chunkSlotIndexFromVoxel(WorldLod lod, uint32_t x, uint32_t y, uint32_t z, glm::uvec3& outLocalVoxel) const;
    ChunkRuntimeState chunkStateBySlotIndex(WorldLod lod, size_t chunkSlotIndex) const;
    bool isChunkSlotGenerated(WorldLod lod, size_t chunkSlotIndex) const;
    void releaseChunkSlotBricksNoDirty(WorldLod lod, size_t chunkSlotIndex);

    uint32_t allocateBrick();
    void releaseBrick(uint32_t brickIndex);
    void onChunkBrickMapDirty(WorldLod lod, size_t chunkIndex, uint32_t mapIndex);
    void onBrickPoolDirty(uint32_t brickIndex);

    std::array<WorldLodStorage, WORLD_LOD_COUNT> lods{};
    BrickPool brickPool;
    mutable std::mutex brickPoolMutex;
};