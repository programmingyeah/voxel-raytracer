#include "terrain_builder.hpp"

#include "materials.hpp"
#include "terrain_noise.hpp"
#include "voxel_world.hpp"

namespace {
struct BrickClassification {
    bool isAllAir = true;
    bool isAllSolid = true;
};

size_t columnIndex(uint32_t x, uint32_t z) {
    return static_cast<size_t>(x) + static_cast<size_t>(Chunk::SIZE) * z;
}

size_t brickMapIndex(uint32_t brickX, uint32_t brickY, uint32_t brickZ) {
    return static_cast<size_t>(brickX) +
           static_cast<size_t>(brickY) * Chunk::BRICKS_PER_AXIS +
           static_cast<size_t>(brickZ) * Chunk::BRICKS_PER_AXIS * Chunk::BRICKS_PER_AXIS;
}

void sampleChunkColumnHeights(
    std::vector<uint32_t>& columnHeights,
    glm::ivec3 chunkCoordinate,
    const glm::uvec3& voxelDimensions
) {
    for (uint32_t localZ = 0; localZ < Chunk::SIZE; localZ++) {
        const int32_t worldZ = chunkCoordinate.z * static_cast<int32_t>(Chunk::SIZE) + static_cast<int32_t>(localZ);
        for (uint32_t localX = 0; localX < Chunk::SIZE; localX++) {
            const int32_t worldX = chunkCoordinate.x * static_cast<int32_t>(Chunk::SIZE) + static_cast<int32_t>(localX);
            columnHeights[columnIndex(localX, localZ)] = static_cast<uint32_t>(sampleTerrainHeight(
                glm::vec2(static_cast<float>(worldX), static_cast<float>(worldZ)), voxelDimensions
            ));
        }
    }
}

BrickClassification classifyBrick(
    const std::vector<uint32_t>& columnHeights,
    uint32_t brickBaseLocalX,
    uint32_t brickBaseLocalZ,
    uint32_t brickMinWorldY,
    uint32_t brickMaxWorldY
) {
    BrickClassification classification{};

    for (uint32_t localZ = 0; localZ < BRICK_SIZE; localZ++) {
        for (uint32_t localX = 0; localX < BRICK_SIZE; localX++) {
            const uint32_t maxSolidWorldY = columnHeights[columnIndex(brickBaseLocalX + localX, brickBaseLocalZ + localZ)];
            if (maxSolidWorldY >= brickMinWorldY) {
                classification.isAllAir = false;
            }
            if (maxSolidWorldY < brickMaxWorldY) {
                classification.isAllSolid = false;
            }
        }
    }

    return classification;
}

uint64_t buildMixedBrick(
    Brick& brick,
    const std::vector<uint32_t>& columnHeights,
    uint32_t brickBaseLocalX,
    uint32_t brickBaseLocalZ,
    uint32_t brickMinWorldY
) {
    uint64_t solidVoxelCount = 0;

    for (uint32_t localZ = 0; localZ < BRICK_SIZE; localZ++) {
        for (uint32_t localX = 0; localX < BRICK_SIZE; localX++) {
            const uint32_t maxSolidWorldY = columnHeights[columnIndex(brickBaseLocalX + localX, brickBaseLocalZ + localZ)];

            for (uint32_t localY = 0; localY < BRICK_SIZE; localY++) {
                const uint32_t worldY = brickMinWorldY + localY;
                const uint8_t voxelValue = worldY <= maxSolidWorldY ? BRICK_SOLID_VOXEL : BRICK_EMPTY_VOXEL;
                brick.voxels[localX][localY][localZ] = voxelValue;
                solidVoxelCount += voxelValue != BRICK_EMPTY_VOXEL;
            }
        }
    }

    Chunk::recomputeOccupancyMask(brick);
    return solidVoxelCount;
}
}

void buildTerrainChunk(VoxelWorld& world, uint32_t chunkSlotIndex, const glm::uvec3& voxelDimensions, TerrainBuildResult& outResult) {
    Chunk& chunk = world.slots.at(chunkSlotIndex).chunk;
    Chunk::EncodedBrickMap& brickMap = chunk.accessBrickMapForGeneration();
    std::vector<uint32_t> columnHeights(static_cast<size_t>(Chunk::SIZE) * Chunk::SIZE);

    outResult.solidVoxelCount = 0;
    outResult.allocatedBrickIndices.clear();
    outResult.allocatedBrickIndices.reserve(Chunk::BRICK_COUNT);
    chunk.resetBrickMapToAirNoCallbacks();

    const glm::ivec3 chunkCoordinate = chunk.getChunkCoordinate();
    sampleChunkColumnHeights(columnHeights, chunkCoordinate, voxelDimensions);

    const uint32_t chunkBaseWorldY = static_cast<uint32_t>(chunkCoordinate.y) * Chunk::SIZE;
    for (uint32_t brickZ = 0; brickZ < Chunk::BRICKS_PER_AXIS; brickZ++) {
        const uint32_t brickBaseLocalZ = brickZ * BRICK_SIZE;

        for (uint32_t brickY = 0; brickY < Chunk::BRICKS_PER_AXIS; brickY++) {
            const uint32_t brickBaseLocalY = brickY * BRICK_SIZE;
            const uint32_t brickMinWorldY = chunkBaseWorldY + brickBaseLocalY;
            const uint32_t brickMaxWorldY = brickMinWorldY + BRICK_SIZE - 1u;

            for (uint32_t brickX = 0; brickX < Chunk::BRICKS_PER_AXIS; brickX++) {
                const uint32_t brickBaseLocalX = brickX * BRICK_SIZE;
                const size_t mapIndex = brickMapIndex(brickX, brickY, brickZ);
                const BrickClassification classification = classifyBrick(
                    columnHeights,
                    brickBaseLocalX,
                    brickBaseLocalZ,
                    brickMinWorldY,
                    brickMaxWorldY
                );

                if (classification.isAllAir) {
                    brickMap[mapIndex] = BrickMapEntry{BRICK_MAP_EMPTY, AIR_MATERIAL};
                    continue;
                }

                if (classification.isAllSolid) {
                    brickMap[mapIndex] = BrickMapEntry{BRICK_MAP_EMPTY, STONE_MATERIAL};
                    outResult.solidVoxelCount += BRICK_VOXEL_COUNT;
                    continue;
                }

                Brick brick{};
                outResult.solidVoxelCount += buildMixedBrick(
                    brick,
                    columnHeights,
                    brickBaseLocalX,
                    brickBaseLocalZ,
                    brickMinWorldY
                );

                const uint32_t brickIndex = world.allocateBrick();
                world.brickPool.bricks.at(brickIndex) = brick;
                brickMap[mapIndex] = BrickMapEntry{brickIndex, STONE_MATERIAL};
                outResult.allocatedBrickIndices.push_back(brickIndex);
            }
        }
    }
}
