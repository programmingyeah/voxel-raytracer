#include "world_gen.hpp"

#include "materials.hpp"
#include "terrain_noise.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace {
struct BrickClassification {
    bool isAllAir = true;
    bool isAllSolid = true;
};

size_t columnIndex(uint32_t x, uint32_t z) {
    return static_cast<size_t>(x) + static_cast<size_t>(Chunk::SIZE) * z;
}

void sampleChunkColumnHeights(
    std::vector<uint32_t>& columnHeights,
    uint32_t chunkX,
    uint32_t chunkZ,
    const glm::vec2& horizontalCenter,
    const glm::uvec3& voxelDimensions
) {
    for (uint32_t localZ = 0; localZ < Chunk::SIZE; localZ++) {
        const uint32_t worldZ = chunkZ * Chunk::SIZE + localZ;

        for (uint32_t localX = 0; localX < Chunk::SIZE; localX++) {
            const uint32_t worldX = chunkX * Chunk::SIZE + localX;
            const glm::vec2 samplePosition(
                static_cast<float>(worldX) - horizontalCenter.x,
                static_cast<float>(worldZ) - horizontalCenter.y
            );

            columnHeights[columnIndex(localX, localZ)] = static_cast<uint32_t>(
                sampleTerrainHeight(samplePosition, voxelDimensions)
            );
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
            const uint32_t maxSolidWorldY = columnHeights[columnIndex(
                brickBaseLocalX + localX,
                brickBaseLocalZ + localZ
            )];

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
            const uint32_t maxSolidWorldY = columnHeights[columnIndex(
                brickBaseLocalX + localX,
                brickBaseLocalZ + localZ
            )];

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

WorldGenerationStats WorldGenerator::generateTerrain(VoxelWorld& world) const {
    WorldGenerationStats stats{};

    const glm::uvec3 chunkCounts = world.getChunkCounts();
    const uint64_t chunkCount = static_cast<uint64_t>(chunkCounts.x) *
                                static_cast<uint64_t>(chunkCounts.y) *
                                static_cast<uint64_t>(chunkCounts.z);
    const glm::uvec3 voxelDimensions = world.getVoxelDimensions();
    const glm::vec2 horizontalCenter(
        (static_cast<float>(voxelDimensions.x) - 1.0f) * 0.5f,
        (static_cast<float>(voxelDimensions.z) - 1.0f) * 0.5f
    );
    const auto generationStart = std::chrono::steady_clock::now();

    Chunk::resetBrickPool();
    Chunk::reserveBrickPool(static_cast<size_t>(chunkCount) * Chunk::BRICK_COUNT);

    std::vector<uint32_t> columnHeights(static_cast<size_t>(Chunk::SIZE) * Chunk::SIZE);

    for (uint32_t chunkZ = 0; chunkZ < chunkCounts.z; chunkZ++) {
        for (uint32_t chunkY = 0; chunkY < chunkCounts.y; chunkY++) {
            for (uint32_t chunkX = 0; chunkX < chunkCounts.x; chunkX++) {
                const auto chunkStart = std::chrono::steady_clock::now();
                Chunk& chunk = world.getChunk(chunkX, chunkY, chunkZ);
                chunk.clear();

                sampleChunkColumnHeights(columnHeights, chunkX, chunkZ, horizontalCenter, voxelDimensions);

                const uint32_t chunkBaseWorldY = chunkY * Chunk::SIZE;

                for (uint32_t brickZ = 0; brickZ < Chunk::BRICKS_PER_AXIS; brickZ++) {
                    const uint32_t brickBaseLocalZ = brickZ * BRICK_SIZE;

                    for (uint32_t brickY = 0; brickY < Chunk::BRICKS_PER_AXIS; brickY++) {
                        const uint32_t brickBaseLocalY = brickY * BRICK_SIZE;
                        const uint32_t brickMinWorldY = chunkBaseWorldY + brickBaseLocalY;
                        const uint32_t brickMaxWorldY = brickMinWorldY + BRICK_SIZE - 1u;

                        for (uint32_t brickX = 0; brickX < Chunk::BRICKS_PER_AXIS; brickX++) {
                            const uint32_t brickBaseLocalX = brickX * BRICK_SIZE;
                            const BrickClassification classification = classifyBrick(
                                columnHeights,
                                brickBaseLocalX,
                                brickBaseLocalZ,
                                brickMinWorldY,
                                brickMaxWorldY
                            );

                            if (classification.isAllAir) {
                                chunk.setBrickUniform(brickX, brickY, brickZ, AIR_MATERIAL);
                                continue;
                            }

                            if (classification.isAllSolid) {
                                chunk.setBrickUniform(brickX, brickY, brickZ, STONE_MATERIAL);
                                stats.solidVoxelCount += BRICK_VOXEL_COUNT;
                                continue;
                            }

                            Brick brick{};
                            stats.solidVoxelCount += buildMixedBrick(
                                brick,
                                columnHeights,
                                brickBaseLocalX,
                                brickBaseLocalZ,
                                brickMinWorldY
                            );

                            chunk.setBrickExplicit(brickX, brickY, brickZ, STONE_MATERIAL, brick);
                        }
                    }
                }

                stats.totalGenerationMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - chunkStart
                ).count();
            }
        }
    }

    if (chunkCount > 0) {
        stats.averageChunkGenerationMs = stats.totalGenerationMs / static_cast<double>(chunkCount);
    }

    stats.totalGenerationMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - generationStart
    ).count();

    return stats;
}
