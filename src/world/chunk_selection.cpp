#include "chunk_selection.hpp"

#include "terrain_noise.hpp"
#include "voxel_world.hpp"

#include <cmath>
#include <limits>

namespace {
constexpr float VIEW_ALIGNMENT_THRESHOLD = 0.35f;
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

struct ScoredChunkSelection {
    uint32_t windowIndex = 0;
    ChunkSelectionScore score{};
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
    const VoxelWorld& world,
    WorldLod lod,
    const glm::ivec3& chunkCoordinate,
    glm::ivec2 focusChunkXZ,
    glm::vec2 normalizedViewXZ,
    uint32_t terrainWorldHeight
) {
    const int64_t deltaX = static_cast<int64_t>(chunkCoordinate.x) - focusChunkXZ.x;
    const int64_t deltaZ = static_cast<int64_t>(chunkCoordinate.z) - focusChunkXZ.y;
    const int64_t distanceSquared = deltaX * deltaX + deltaZ * deltaZ;
    const float alignment = viewAlignmentToChunkXZ(normalizedViewXZ, deltaX, deltaZ);
    const bool isInView = alignment >= VIEW_ALIGNMENT_THRESHOLD;
    const float chunkWorldSpan = static_cast<float>(world.getChunkWorldSpan(lod));
    const float surfacePriorityMargin = chunkWorldSpan;
    const glm::ivec3 chunkWorldMin = world.getChunkWorldMin(lod, chunkCoordinate);

    const float surfaceHeight = sampleTerrainHeight(
        glm::vec2(
            static_cast<float>(chunkWorldMin.x) + 0.5f * chunkWorldSpan,
            static_cast<float>(chunkWorldMin.z) + 0.5f * chunkWorldSpan
        ),
        terrainWorldHeight
    );
    const float chunkMinWorldY = static_cast<float>(chunkWorldMin.y);
    const float chunkMaxWorldY = chunkMinWorldY + chunkWorldSpan - 1.0f;
    const bool isNearSurface =
        surfaceHeight >= chunkMinWorldY - surfacePriorityMargin &&
        surfaceHeight <= chunkMaxWorldY + surfacePriorityMargin;
    const bool isUnderground = surfaceHeight > chunkMaxWorldY + surfacePriorityMargin;

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

std::vector<uint32_t> findBestUngeneratedChunkWindowIndices(
    const VoxelWorld& world,
    WorldLod lod,
    glm::ivec2 focusChunkXZ,
    glm::vec3 viewForward,
    size_t maxCount
) {
    if (maxCount == 0u) {
        return {};
    }

    std::vector<ScoredChunkSelection> bestSelections;
    bestSelections.reserve(maxCount);

    const glm::vec2 normalizedViewXZ = normalizedViewDirectionXZ(viewForward);
    const uint32_t terrainWorldHeight = world.getTerrainWorldHeight();

    for (size_t localWindowIndex = 0; localWindowIndex < world.getChunkCount(lod); localWindowIndex++) {
        if (world.getChunkStateByWindowIndex(lod, localWindowIndex) != ChunkRuntimeState::Ungenerated) {
            continue;
        }

        const glm::ivec3 chunkCoordinate = world.getChunkByWindowIndex(lod, localWindowIndex).getChunkCoordinate();
        const ChunkSelectionScore candidateScore = scoreChunkSelection(
            world,
            lod,
            chunkCoordinate,
            focusChunkXZ,
            normalizedViewXZ,
            terrainWorldHeight
        );

        if (bestSelections.size() == maxCount &&
            !isChunkSelectionScoreBetter(candidateScore, bestSelections.back().score)) {
            continue;
        }

        const ScoredChunkSelection candidateSelection{
            static_cast<uint32_t>(localWindowIndex),
            candidateScore
        };

        auto insertPosition = bestSelections.begin();
        while (insertPosition != bestSelections.end() &&
               !isChunkSelectionScoreBetter(candidateScore, insertPosition->score)) {
            ++insertPosition;
        }

        bestSelections.insert(insertPosition, candidateSelection);
        if (bestSelections.size() > maxCount) {
            bestSelections.pop_back();
        }
    }

    std::vector<uint32_t> bestIndices;
    bestIndices.reserve(bestSelections.size());
    for (const ScoredChunkSelection& selection : bestSelections) {
        bestIndices.push_back(selection.windowIndex);
    }

    return bestIndices;
}
