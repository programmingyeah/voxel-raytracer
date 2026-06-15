#include "terrain_noise.hpp"

#include <array>
#include <cmath>
#include <cstdint>

#include <glm/common.hpp>

namespace {
float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

uint32_t hash2D(uint32_t x, uint32_t z) {
    uint32_t hash = x * 374761393u + z * 668265263u;
    hash = (hash ^ (hash >> 13u)) * 1274126177u;
    return hash ^ (hash >> 16u);
}

glm::vec2 gradient(int x, int z) {
    constexpr float INV_SQRT2 = 0.70710678118f;
    static constexpr std::array<glm::vec2, 8> gradients = {
        glm::vec2(1.0f, 0.0f),
        glm::vec2(-1.0f, 0.0f),
        glm::vec2(0.0f, 1.0f),
        glm::vec2(0.0f, -1.0f),
        glm::vec2(INV_SQRT2, INV_SQRT2),
        glm::vec2(-INV_SQRT2, INV_SQRT2),
        glm::vec2(INV_SQRT2, -INV_SQRT2),
        glm::vec2(-INV_SQRT2, -INV_SQRT2)
    };

    const uint32_t hash = hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
    return gradients[hash & 7u];
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
}

float sampleTerrainHeight(const glm::vec2& position, const glm::uvec3& voxelDimensions) {
    const float broadShape = 10.0f*fractalNoise(position * 0.00045f);
    const float detailShape = fractalNoise(position * 0.0018f + glm::vec2(37.0f, -19.0f));
    const float halfWorldHeight = (static_cast<float>(voxelDimensions.y) - 1.0f) * 0.5f;
    const float baseHeight = halfWorldHeight - 20.0f;
    const float terrainHeight = baseHeight + 2.0f * broadShape * 18.0f + 2.0f * detailShape * 6.0f;

    return glm::clamp(terrainHeight, 1.0f, static_cast<float>(voxelDimensions.y - 1));
}
