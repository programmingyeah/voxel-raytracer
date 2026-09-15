#include "renderer.hpp"

#include "renderer_shared.hpp"

#include "imgui.h"

#include <string>

namespace {
std::string formatWithCommas(uint64_t value) {
    std::string digits = std::to_string(value);
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(digits.size()) - 3; i > 0; i -= 3) {
        digits.insert(static_cast<size_t>(i), ",");
    }
    return digits;
}
}

void VulkanApp::updateFrameTiming() {
    const auto now = std::chrono::steady_clock::now();
    const float deltaMs = std::chrono::duration<float, std::milli>(now - lastFrameTimestamp).count();
    lastFrameTimestamp = now;

    frameTimeMs = deltaMs;
    if (deltaMs > 0.0f) {
        const float instantFrameRate = 1000.0f / deltaMs;
        frameRate = frameRate == 0.0f ? instantFrameRate : frameRate * 0.9f + instantFrameRate * 0.1f;
    }
}

void VulkanApp::drawStatsUi() {
    const size_t chunkCount = world != nullptr ? world->getChunkCount() : 0;
    const size_t generatedChunkCount = world != nullptr ? world->getGeneratedChunkCount() : 0;
    const glm::uvec3 lod0ChunkCounts = world != nullptr ? world->getChunkCounts(WorldLod::Lod0) : glm::uvec3(0u);
    const glm::uvec3 coarsestVoxelDimensions = world != nullptr ? world->getVoxelDimensions(static_cast<WorldLod>(WORLD_LOD_COUNT - 1u)) : glm::uvec3(0u);
    const uint64_t worldVoxelCount = static_cast<uint64_t>(chunkCount) * Chunk::VOXEL_COUNT;
    const uint64_t effectiveVoxelCount = static_cast<uint64_t>(coarsestVoxelDimensions.x) * static_cast<uint64_t>(coarsestVoxelDimensions.y) * static_cast<uint64_t>(coarsestVoxelDimensions.z);
    const size_t allocatedBrickCount = world != nullptr ? world->getAllocatedBrickCount() : 0;
    const size_t brickCapacity = world != nullptr ? world->getBrickCapacity() : 0;
    const uint32_t renderDistanceChunks = lod0ChunkCounts.x > 0u ? (lod0ChunkCounts.x - 1u) / 2u : 0u;
    const uint32_t worldHeightChunks = lod0ChunkCounts.y;
    const uint32_t effectiveRenderDistanceChunks = renderDistanceChunks * (1u << (WORLD_LOD_COUNT - 1u));
    const uint32_t effectiveRenderDistanceWorldUnits = effectiveRenderDistanceChunks * Chunk::SIZE;
    static const char* rayQueryVisualizationModes[] = {"Off", "Query heat", "Hierarchy breakdown"};
    const bool combinedSelected = lodRenderMode >= static_cast<int>(WORLD_LOD_COUNT);

    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("Diagnostics");
    ImGui::Text("Frame rate: %.1f FPS", frameRate);
    ImGui::Text("Frame time: %.2f ms", frameTimeMs);
    ImGui::Separator();

    ImGui::Text("LOD rendering");
    if (ImGui::RadioButton("Combined", combinedSelected)) {
        lodRenderMode = static_cast<int>(WORLD_LOD_COUNT);
    }
    ImGui::SameLine();
    for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
        const std::string label = "LOD" + std::to_string(lodIndex);
        if (lodIndex > 0) {
            ImGui::SameLine();
        }
        if (ImGui::RadioButton(label.c_str(), lodRenderMode == static_cast<int>(lodIndex))) {
            lodRenderMode = static_cast<int>(lodIndex);
        }
    }

    ImGui::Separator();
    ImGui::Text("Render distance: %u chunks", renderDistanceChunks);
    ImGui::Text("Effective render distance: %u chunks", effectiveRenderDistanceChunks);
    ImGui::Text("Effective render distance: %u world units", effectiveRenderDistanceWorldUnits);

    ImGui::Separator();
    ImGui::Text("World summary");
    ImGui::Text("LOD0 generated chunks: %zu / %zu", generatedChunkCount, chunkCount);
    ImGui::Text("Voxel count: %s", formatWithCommas(worldVoxelCount).c_str());
    ImGui::Text("Effective voxel count: %s", formatWithCommas(effectiveVoxelCount).c_str());
    ImGui::Text("Solid voxels: %s", formatWithCommas(worldStats.solidVoxelCount).c_str());
    ImGui::Text("Allocated bricks: %s / %s", formatWithCommas(static_cast<uint64_t>(allocatedBrickCount)).c_str(), formatWithCommas(static_cast<uint64_t>(brickCapacity)).c_str());
    ImGui::Text("World height: %u chunks", worldHeightChunks);
    ImGui::Text("Avg chunk load: %.3f ms", worldStats.averageChunkGenerationMs);
    ImGui::Text("World gen total: %.2f ms", worldStats.totalGenerationMs);

    ImGui::Text("Shared GPU brick pool: %.2f MB (%u resident, %u allocated, %u capacity)",
        static_cast<double>(sharedBrickPoolBuffer.size) / (1024.0 * 1024.0),
        brickResidency.getResidentGpuBrickCount(),
        brickResidency.getAllocatedGpuBrickCount(),
        brickResidency.getGpuBrickCapacity());

    if (ImGui::TreeNode("LOD summary")) {
        for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
            const WorldLod lod = static_cast<WorldLod>(lodIndex);
            const size_t lodChunkCount = world != nullptr ? world->getChunkCount(lod) : 0;
            const size_t lodGeneratedChunkCount = world != nullptr ? world->getGeneratedChunkCount(lod) : 0;
            const auto& lodGpu = gpuLods.at(lodIndex);
            ImGui::Text(
                "LOD%zu  gen %zu/%zu  requests %u  dropped %u  chunkIdx %.2f KB  chunkMap %.2f KB",
                lodIndex,
                lodGeneratedChunkCount,
                lodChunkCount,
                brickResidency.getLastBrickRequestCount(lod),
                brickResidency.getLastDroppedBrickRequestCount(lod),
                static_cast<double>(lodGpu.chunkWindowIndexBuffer.size) / 1024.0,
                static_cast<double>(lodGpu.chunkBrickMapBuffer.size) / 1024.0
            );
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::Text("Ray query visualization");
    ImGui::Combo("Mode", &rayQueryVisualizationMode, rayQueryVisualizationModes, IM_ARRAYSIZE(rayQueryVisualizationModes));
    ImGui::SliderFloat("Intensity", &rayQueryVisualizationIntensity, 0.25f, 4.0f, "%.2fx");
    ImGui::TextWrapped("Heat shows weighted traversal cost. Breakdown colors surfaces by query source: red = voxel tests, green = sparse brick/coarse checks, blue = chunk traversal.");
    ImGui::End();
}
