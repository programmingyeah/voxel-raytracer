#include "chunk_selection.hpp"

#include "terrain_noise.hpp"
#include "voxel_world.hpp"

#include <cmath>
#include <limits>

namespace {
constexpr float VIEW_ALIGNMENT_THRESHOLD = 0.35f;
constexpr float SURFACE_PRIORITY_MARGIN = static_cast<float>(Chunk::SIZE);

enum class ChunkPriorityBucket : uint32_t {
    InViewNearSurface = 0,
    NearSurface = 1,
    InViewAboveGround = 2,
    AboveGround = 3,
    Underground = 4,
};

struct ChunkSelectionScore {
    ChunkPriorityBucket bucket = ChunkPriorityBucket::Underground;
    int64_t distanceSquared = std::numeric_limits<int64_t>::max();
    float alignment = -1.0f;
};

glm::vec2 normalizedViewDirectionXZ(glm::vec3 viewForward) {
    const glm::vec2 forwardXZ(viewForward.x, viewForward.z);
    const float lengthSquared = forwardXZ.x * forwardXZ.x + forwardXZ.y * forwardXZ.y;
    return lengthSquared <= 1e-6f ? glm::vec2(0.0f) : forwardXZ / std::sqrt(lengthSquared);
}

float viewAlignmentToChunkXZ(const glm::vec2& normalizedViewXZ, int64_t deltaX, int64_t deltaZ) {
    if (normalizedViewXZ.x == 0.0f && normalizedViewXZ.y == 0.0f) {
        return -1.0f;
    }

    if (deltaX == 0 && deltaZ == 0) {
        return 1.0f;
    }

    const glm::vec2 toChunk(static_cast<float>(deltaX), static_cast<float>(deltaZ));
    const float lengthSquared = toChunk.x * toChunk.x + toChunk.y * toChunk.y;
    if (lengthSquared <= 1e-6f) {
        return 1.0f;
    }

    return glm::dot(normalizedViewXZ, toChunk / std::sqrt(lengthSquared));
}

ChunkSelectionScore scoreChunkSelection(
    const glm::ivec3& chunkCoordinate,
    glm::ivec2 focusChunkXZ,
    glm::vec2 normalizedViewXZ,
    const glm::uvec3& voxelDimensions
) {
    const int64_t deltaX = static_cast<int64_t>(chunkCoordinate.x) - focusChunkXZ.x;
    const int64_t deltaZ = static_cast<int64_t>(chunkCoordinate.z) - focusChunkXZ.y;
    const int64_t distanceSquared = deltaX * deltaX + deltaZ * deltaZ;
    const float alignment = viewAlignmentToChunkXZ(normalizedViewXZ, deltaX, deltaZ);
    const bool isInView = alignment >= VIEW_ALIGNMENT_THRESHOLD;

    const float surfaceHeight = sampleTerrainHeight(
        glm::vec2(
            (static_cast<float>(chunkCoordinate.x) + 0.5f) * static_cast<float>(Chunk::SIZE),
            (static_cast<float>(chunkCoordinate.z) + 0.5f) * static_cast<float>(Chunk::SIZE)
        ),
        voxelDimensions
    );
    const float chunkMinWorldY = static_cast<float>(chunkCoordinate.y * static_cast<int32_t>(Chunk::SIZE));
    const float chunkMaxWorldY = chunkMinWorldY + static_cast<float>(Chunk::SIZE - 1u);
    const bool isNearSurface =
        surfaceHeight >= chunkMinWorldY - SURFACE_PRIORITY_MARGIN &&
        surfaceHeight <= chunkMaxWorldY + SURFACE_PRIORITY_MARGIN;
    const bool isUnderground = surfaceHeight > chunkMaxWorldY + SURFACE_PRIORITY_MARGIN;

    const ChunkPriorityBucket bucket =
        isInView && isNearSurface ? ChunkPriorityBucket::InViewNearSurface :
        isNearSurface ? ChunkPriorityBucket::NearSurface :
        isInView && !isUnderground ? ChunkPriorityBucket::InViewAboveGround :
        !isUnderground ? ChunkPriorityBucket::AboveGround :
        ChunkPriorityBucket::Underground;

    return ChunkSelectionScore{bucket, distanceSquared, alignment};
}

bool isChunkSelectionScoreBetter(const ChunkSelectionScore& candidate, const ChunkSelectionScore& best) {
    if (candidate.bucket != best.bucket) {
        return static_cast<uint32_t>(candidate.bucket) < static_cast<uint32_t>(best.bucket);
    }

    if (candidate.distanceSquared != best.distanceSquared) {
        return candidate.distanceSquared < best.distanceSquared;
    }

    return candidate.alignment > best.alignment;
}
}

std::optional<uint32_t> findBestUngeneratedChunkWindowIndex(
    const VoxelWorld& world,
    glm::ivec2 focusChunkXZ,
    glm::vec3 viewForward
) {
    std::optional<uint32_t> bestIndex;
    ChunkSelectionScore bestScore{};
    const glm::vec2 normalizedViewXZ = normalizedViewDirectionXZ(viewForward);
    const glm::uvec3 voxelDimensions = world.getVoxelDimensions();

    for (size_t localWindowIndex = 0; localWindowIndex < world.getChunkCount(); localWindowIndex++) {
        if (world.getChunkStateByWindowIndex(localWindowIndex) != ChunkRuntimeState::Ungenerated) {
            continue;
        }

        const glm::ivec3 chunkCoordinate = world.getChunkByWindowIndex(localWindowIndex).getChunkCoordinate();
        const ChunkSelectionScore candidateScore = scoreChunkSelection(
            chunkCoordinate,
            focusChunkXZ,
            normalizedViewXZ,
            voxelDimensions
        );

        if (!bestIndex || isChunkSelectionScoreBetter(candidateScore, bestScore)) {
            bestIndex = static_cast<uint32_t>(localWindowIndex);
            bestScore = candidateScore;
        }
    }

    return bestIndex;
}
