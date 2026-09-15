#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class VoxelWorld;
enum class WorldLod : uint8_t;

std::vector<uint32_t> findBestUngeneratedChunkWindowIndices(
    const VoxelWorld& world,
    WorldLod lod,
    glm::ivec2 focusChunkXZ,
    glm::vec3 viewForward,
    size_t maxCount
);
