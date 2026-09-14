#include "renderer.hpp"

#include "renderer_shared.hpp"

#include "imgui.h"

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
    const VkDeviceSize worldBufferBytes = chunkBrickMapBuffer.size + brickPoolBuffer.size;

    const std::string worldBufferSize = formatByteSize(worldBufferBytes);
    const std::string chunkMapBufferSize = formatByteSize(chunkBrickMapBuffer.size);
    const std::string brickPoolBufferSize = formatByteSize(brickPoolBuffer.size);
    static const char* rayQueryVisualizationModes[] = {"Off", "Query heat", "Hierarchy breakdown"};

    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("Diagnostics");
    ImGui::Text("Frame rate: %.1f FPS", frameRate);
    ImGui::Text("Frame time: %.2f ms", frameTimeMs);
    ImGui::Separator();
    ImGui::Text("Chunk count: %zu", chunkCount);
    ImGui::Text("Generated chunks: %zu / %zu", generatedChunkCount, chunkCount);
    ImGui::Text("Voxel count: %llu", static_cast<unsigned long long>(worldVoxelCount));
    ImGui::Text("Solid voxels: %llu", static_cast<unsigned long long>(worldStats.solidVoxelCount));
    ImGui::Text("Allocated bricks: %zu / %zu", allocatedBrickCount, brickCapacity);
    ImGui::Text("GPU resident bricks: %u / %u", brickResidency.getNextGpuBrickSlot(), brickResidency.getGpuBrickCapacity());
    ImGui::Text("Brick requests: %u", brickResidency.getLastBrickRequestCount());
    ImGui::Text("Dropped brick requests: %u", brickResidency.getLastDroppedBrickRequestCount());
    ImGui::Text("Avg chunk load: %.3f ms", worldStats.averageChunkGenerationMs);
    ImGui::Text("World gen total: %.2f ms", worldStats.totalGenerationMs);
    ImGui::Separator();
    ImGui::Text("World buffer VRAM: %s", worldBufferSize.c_str());
    ImGui::Text("Chunk map buffer: %s", chunkMapBufferSize.c_str());
    ImGui::Text("Brick pool buffer: %s", brickPoolBufferSize.c_str());
    ImGui::Separator();
    ImGui::Text("Ray query visualization");
    ImGui::Combo("Mode", &rayQueryVisualizationMode, rayQueryVisualizationModes, IM_ARRAYSIZE(rayQueryVisualizationModes));
    ImGui::SliderFloat("Intensity", &rayQueryVisualizationIntensity, 0.25f, 4.0f, "%.2fx");
    ImGui::TextWrapped("Heat shows weighted traversal cost. Breakdown colors surfaces by query source: red = voxel tests, green = sparse brick/coarse checks, blue = chunk traversal.");
    ImGui::End();
}