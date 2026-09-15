#pragma once

#include "brick.hpp"
#include "materials.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

using ChunkBrickMapDirtyCallback = std::function<void(size_t, uint32_t)>;
using AllocateBrickCallback = std::function<uint32_t()>;
using ReleaseBrickCallback = std::function<void(uint32_t)>;
using BrickPoolDirtyCallback = std::function<void(uint32_t)>;

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class VoxelWorld;
enum class WorldLod : uint8_t;
struct TerrainBuildResult;
void buildTerrainChunk(VoxelWorld& world, WorldLod lod, uint32_t chunkSlotIndex, uint32_t terrainWorldHeight, TerrainBuildResult& outResult);

inline constexpr uint32_t BRICK_MAP_EMPTY = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t PACKED_BRICK_MAP_ENTRY_WORD_COUNT = 2u;
inline constexpr uint32_t GPU_BRICK_METADATA_MATERIAL_MASK = 0xffu;
inline constexpr uint32_t GPU_BRICK_METADATA_RESIDENT_BIT = 1u << 8u;
inline constexpr uint32_t GPU_BRICK_METADATA_REQUESTED_BIT = 1u << 9u;

struct BrickMapEntry {
    uint32_t index = BRICK_MAP_EMPTY;
    MaterialId materialId = AIR_MATERIAL;
};

class Chunk {
public:
    static constexpr uint32_t SIZE = 128;
    static constexpr uint32_t BRICKS_PER_AXIS = SIZE / BRICK_SIZE;
    static constexpr size_t BRICK_COUNT = static_cast<size_t>(BRICKS_PER_AXIS) * BRICKS_PER_AXIS * BRICKS_PER_AXIS;
    static constexpr size_t VOXEL_COUNT = static_cast<size_t>(SIZE) * SIZE * SIZE;

    static_assert(SIZE % BRICK_SIZE == 0, "Chunk::SIZE must be divisible by BRICK_SIZE");

    using EncodedBrickMap = std::array<BrickMapEntry, BRICK_COUNT>;

    explicit Chunk(glm::ivec3 chunkCoordinate = glm::ivec3(0), size_t chunkSlotIndex = 0);

    glm::ivec3 getChunkCoordinate() const { return chunkCoordinate; }
    const EncodedBrickMap& getBrickMap() const { return brickMap; }

    uint32_t get(uint32_t x, uint32_t y, uint32_t z) const;
    void set(uint32_t x, uint32_t y, uint32_t z, uint32_t value);
    void setBrickUniform(uint32_t brickX, uint32_t brickY, uint32_t brickZ, uint32_t materialId);
    void setBrick(uint32_t brickX, uint32_t brickY, uint32_t brickZ, uint32_t materialId, const Brick& brick);
    void clear();

    static void recomputeOccupancyMask(Brick& brick);
    static void fillBrick(Brick& brick, uint8_t value);

private:
    friend class VoxelWorld;
    friend void buildTerrainChunk(VoxelWorld& world, WorldLod lod, uint32_t chunkSlotIndex, uint32_t terrainWorldHeight, TerrainBuildResult& outResult);

    void setChunkCoordinate(glm::ivec3 inChunkCoordinate) { chunkCoordinate = inChunkCoordinate; }
    EncodedBrickMap& accessBrickMapForGeneration() { return brickMap; }
    void resetBrickMapToAirNoCallbacks();
    void bindStorage(
        std::vector<Brick>* inBrickPool,
        AllocateBrickCallback inAllocateBrickCallback,
        ReleaseBrickCallback inReleaseBrickCallback,
        ChunkBrickMapDirtyCallback inChunkBrickMapDirtyCallback,
        BrickPoolDirtyCallback inBrickPoolDirtyCallback
    );
    std::vector<Brick>& brickPool();
    const std::vector<Brick>& brickPool() const;
    uint32_t allocateBrick();
    void releaseBrick(uint32_t brickIndex);
    void markBrickMapDirty(uint32_t mapIndex);
    void markChunkDirty();
    void markBrickPoolDirty(uint32_t brickIndex);

    // World-space chunk coordinate.
    glm::ivec3 chunkCoordinate{};
    // Slot index within the world window.
    size_t chunkSlotIndex = 0;
    // CPU brick map for this chunk.
    EncodedBrickMap brickMap{};

    // Brick storage is owned by VoxelWorld.
    std::vector<Brick>* externalBrickPool = nullptr;

    // Allocation and dirty tracking are forwarded to VoxelWorld.
    AllocateBrickCallback allocateBrickCallback;
    ReleaseBrickCallback releaseBrickCallback;
    ChunkBrickMapDirtyCallback chunkBrickMapDirtyCallback;
    BrickPoolDirtyCallback brickPoolDirtyCallback;
};