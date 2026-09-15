#include "renderer.hpp"

#include "renderer_shared.hpp"

#include "imgui.h"

namespace {
constexpr uint32_t BASE_RENDER_DISTANCE_CHUNKS = 5u;
constexpr uint32_t WORLD_HEIGHT_CHUNKS = 3u;
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
    const uint64_t worldVoxelCount = static_cast<uint64_t>(chunkCount) * Chunk::VOXEL_COUNT;
    const size_t allocatedBrickCount = world != nullptr ? world->getAllocatedBrickCount() : 0;
    const size_t brickCapacity = world != nullptr ? world->getBrickCapacity() : 0;
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
    ImGui::Text("Render distance: %u chunks", BASE_RENDER_DISTANCE_CHUNKS);
    ImGui::Text("Effective render distance: %u chunks", BASE_RENDER_DISTANCE_CHUNKS << (WORLD_LOD_COUNT - 1u));
    ImGui::Text("Effective render distance: %u world units", BASE_RENDER_DISTANCE_CHUNKS * Chunk::SIZE * (1u << (WORLD_LOD_COUNT - 1u)));

    ImGui::Separator();
    ImGui::Text("World summary");
    ImGui::Text("LOD0 generated chunks: %zu / %zu", generatedChunkCount, chunkCount);
    ImGui::Text("Voxel count: %llu", static_cast<unsigned long long>(worldVoxelCount));
    ImGui::Text("Solid voxels: %llu", static_cast<unsigned long long>(worldStats.solidVoxelCount));
    ImGui::Text("Allocated bricks: %zu / %zu", allocatedBrickCount, brickCapacity);
    ImGui::Text("World height: %u chunks", WORLD_HEIGHT_CHUNKS);
    ImGui::Text("Avg chunk load: %.3f ms", worldStats.averageChunkGenerationMs);
    ImGui::Text("World gen total: %.2f ms", worldStats.totalGenerationMs);

    if (ImGui::TreeNode("LOD summary")) {
        for (size_t lodIndex = 0; lodIndex < WORLD_LOD_COUNT; lodIndex++) {
            const WorldLod lod = static_cast<WorldLod>(lodIndex);
            const size_t lodChunkCount = world != nullptr ? world->getChunkCount(lod) : 0;
            const size_t lodGeneratedChunkCount = world != nullptr ? world->getGeneratedChunkCount(lod) : 0;
            const auto& lodGpu = gpuLods.at(lodIndex);
            ImGui::Text(
                "LOD%zu  gen %zu/%zu  resident %u/%u  requests %u  dropped %u",
                lodIndex,
                lodGeneratedChunkCount,
                lodChunkCount,
                lodGpu.brickResidency.getNextGpuBrickSlot(),
                lodGpu.brickResidency.getGpuBrickCapacity(),
                lodGpu.brickResidency.getLastBrickRequestCount(),
                lodGpu.brickResidency.getLastDroppedBrickRequestCount()
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
