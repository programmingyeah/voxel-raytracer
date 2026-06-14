#pragma once

#include <cstdint>
#include <optional>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class VoxelWorld;

std::optional<uint32_t> findBestUngeneratedChunkWindowIndex(
    const VoxelWorld& world,
    glm::ivec2 focusChunkXZ,
    glm::vec3 viewForward
);
