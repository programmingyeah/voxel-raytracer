#include "world_gen.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>

#include <glm/gtc/constants.hpp>

namespace {
float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

uint32_t hash(uint32_t x, uint32_t z) {
    uint32_t h = x * 374761393u + z * 668265263u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

glm::vec2 gradient(int x, int z) {
    const uint32_t h = hash(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
    const float angle = static_cast<float>(h & 1023u) * (glm::two_pi<float>() / 1024.0f);
    return glm::vec2(std::cos(angle), std::sin(angle));
}

float perlinNoise(const glm::vec2& position) {
    const int x0 = static_cast<int>(std::floor(position.x));
    const int z0 = static_cast<int>(std::floor(position.y));
    const int x1 = x0 + 1;
    const int z1 = z0 + 1;

    const glm::vec2 local(
        position.x - static_cast<float>(x0),
        position.y - static_cast<float>(z0)
    );

    const float n00 = glm::dot(gradient(x0, z0), local - glm::vec2(0.0f, 0.0f));
    const float n10 = glm::dot(gradient(x1, z0), local - glm::vec2(1.0f, 0.0f));
    const float n01 = glm::dot(gradient(x0, z1), local - glm::vec2(0.0f, 1.0f));
    const float n11 = glm::dot(gradient(x1, z1), local - glm::vec2(1.0f, 1.0f));

    const float u = fade(local.x);
    const float v = fade(local.y);

    return lerp(lerp(n00, n10, u), lerp(n01, n11, u), v);
}

float fractalNoise(const glm::vec2& position) {
    float frequency = 3.0f;
    float amplitude = 1.0f;
    float value = 0.0f;
    float amplitudeSum = 0.0f;

    for (int octave = 0; octave < 5; octave++) {
        value += perlinNoise(position * frequency) * amplitude;
        amplitudeSum += amplitude;
        frequency *= 2.0f;
        amplitude *= 0.5f;
    }

    return value / amplitudeSum;
}

float sampleHeight(const glm::vec2& position, const glm::uvec3& voxelDimensions) {
    const float broadShape = fractalNoise(position * 0.0025f);
    const float detailShape = fractalNoise(position * 0.01f + glm::vec2(37.0f, -19.0f));
    const float halfWorldHeight = (static_cast<float>(voxelDimensions.y) - 1.0f) * 0.5f;
    const float baseHeight = halfWorldHeight - 24.0f;
    const float terrainHeight = baseHeight + 2*broadShape * 42.0f + 2*detailShape * 18.0f;

    return glm::clamp(terrainHeight, 1.0f, static_cast<float>(voxelDimensions.y - 1));
}
}

WorldGenerationStats WorldGenerator::generateTerrain(VoxelWorld& world) const {
    WorldGenerationStats stats{};

    const glm::uvec3 voxelDimensions = world.getVoxelDimensions();
    const glm::vec2 horizontalCenter(
        (static_cast<float>(voxelDimensions.x) - 1.0f) * 0.5f,
        (static_cast<float>(voxelDimensions.z) - 1.0f) * 0.5f
    );
    const auto generationStart = std::chrono::steady_clock::now();

    for (uint32_t chunkZ = 0; chunkZ < world.getChunkCounts().z; chunkZ++) {
        for (uint32_t chunkY = 0; chunkY < world.getChunkCounts().y; chunkY++) {
            for (uint32_t chunkX = 0; chunkX < world.getChunkCounts().x; chunkX++) {
                const auto chunkStart = std::chrono::steady_clock::now();
                Chunk& chunk = world.getChunk(chunkX, chunkY, chunkZ);
                chunk.clear();

                for (uint32_t localZ = 0; localZ < Chunk::SIZE; localZ++) {
                    const uint32_t worldZ = chunkZ * Chunk::SIZE + localZ;

                    for (uint32_t localX = 0; localX < Chunk::SIZE; localX++) {
                        const uint32_t worldX = chunkX * Chunk::SIZE + localX;
                        const glm::vec2 samplePosition(
                            static_cast<float>(worldX) - horizontalCenter.x,
                            static_cast<float>(worldZ) - horizontalCenter.y
                        );
                        const float terrainHeight = sampleHeight(samplePosition, voxelDimensions);

                        for (uint32_t localY = 0; localY < Chunk::SIZE; localY++) {
                            const uint32_t worldY = chunkY * Chunk::SIZE + localY;
                            if (static_cast<float>(worldY) <= terrainHeight) {
                                chunk.set(localX, localY, localZ, 1);
                                stats.solidVoxelCount++;
                            }
                        }
                    }
                }

                stats.totalGenerationMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - chunkStart
                ).count();
            }
        }
    }

    const uint64_t chunkCount = static_cast<uint64_t>(world.getChunkCounts().x) *
                                static_cast<uint64_t>(world.getChunkCounts().y) *
                                static_cast<uint64_t>(world.getChunkCounts().z);
    if (chunkCount > 0) {
        stats.averageChunkGenerationMs = stats.totalGenerationMs / static_cast<double>(chunkCount);
    }

    stats.totalGenerationMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - generationStart
    ).count();

    return stats;
}

