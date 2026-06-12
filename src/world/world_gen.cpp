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
    glm::ivec3 chunkCoordinate,
    const glm::uvec3& voxelDimensions
) {
    for (uint32_t localZ = 0; localZ < Chunk::SIZE; localZ++) {
        const int32_t worldZ = chunkCoordinate.z * static_cast<int32_t>(Chunk::SIZE) + static_cast<int32_t>(localZ);

        for (uint32_t localX = 0; localX < Chunk::SIZE; localX++) {
            const int32_t worldX = chunkCoordinate.x * static_cast<int32_t>(Chunk::SIZE) + static_cast<int32_t>(localX);
            const glm::vec2 samplePosition(
                static_cast<float>(worldX),
                static_cast<float>(worldZ)
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

uint64_t generateChunkTerrain(Chunk& chunk, const glm::uvec3& voxelDimensions, std::vector<uint32_t>& columnHeights) {
    uint64_t solidVoxelCount = 0;
    chunk.clear();

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
                    solidVoxelCount += BRICK_VOXEL_COUNT;
                    continue;
                }

                Brick brick{};
                solidVoxelCount += buildMixedBrick(
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

    return solidVoxelCount;
}
}

WorldGenerationStats WorldGenerator::generateTerrain(VoxelWorld& world) const {
    std::vector<uint32_t> localChunkWindowIndices(world.getChunkCount());
    for (size_t i = 0; i < localChunkWindowIndices.size(); i++) {
        localChunkWindowIndices[i] = static_cast<uint32_t>(i);
    }

    return generateTerrain(world, localChunkWindowIndices);
}

WorldGenerationStats WorldGenerator::generateTerrain(VoxelWorld& world, const std::vector<uint32_t>& localChunkWindowIndices) const {
    WorldGenerationStats stats{};
    const glm::uvec3 voxelDimensions = world.getVoxelDimensions();
    const auto generationStart = std::chrono::steady_clock::now();
    std::vector<uint32_t> columnHeights(static_cast<size_t>(Chunk::SIZE) * Chunk::SIZE);

    for (uint32_t localWindowIndex : localChunkWindowIndices) {
        const auto chunkStart = std::chrono::steady_clock::now();
        Chunk& chunk = world.getChunkByWindowIndex(localWindowIndex);
        const uint64_t solidVoxelCount = generateChunkTerrain(chunk, voxelDimensions, columnHeights);
        world.setChunkSolidVoxelCountByWindowIndex(localWindowIndex, solidVoxelCount);

        stats.totalGenerationMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - chunkStart
        ).count();
    }

    if (!localChunkWindowIndices.empty()) {
        stats.averageChunkGenerationMs = stats.totalGenerationMs / static_cast<double>(localChunkWindowIndices.size());
    }

    stats.totalGenerationMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - generationStart
    ).count();
    stats.solidVoxelCount = world.getTotalSolidVoxelCount();
    return stats;
}
