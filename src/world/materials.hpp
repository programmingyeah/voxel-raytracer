#pragma once

#include <cstdint>
#include <limits>

using MaterialId = uint8_t;

inline constexpr MaterialId AIR_MATERIAL = 0u;
inline constexpr MaterialId STONE_MATERIAL = 1u;
inline constexpr uint32_t MATERIAL_COUNT = 2u;

static_assert(MATERIAL_COUNT <= (std::numeric_limits<MaterialId>::max() + 1u), "Material ids must fit in uint8_t");