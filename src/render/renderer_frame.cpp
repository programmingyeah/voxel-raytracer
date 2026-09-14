#include "renderer.hpp"

#include "renderer_shared.hpp"
#include "../world/gpu_world_layout.hpp"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <stdexcept>

void VulkanApp::drawFrame(const Camera& camera) {
    FrameSyncObjects frameSyncObjects = syncManager.getFrame(currentFrame);

    vkWaitForFences(instance.device(), 1, &frameSyncObjects.inFlight, VK_TRUE, UINT64_MAX);
    cleanupUploads(currentFrame);
    processBrickRequests(static_cast<size_t>(currentFrame));
    syncWorld();
    resetRequestBuffer(static_cast<size_t>(currentFrame));

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        instance.device(),
        swapchain.getHandle(),
        UINT64_MAX,
        frameSyncObjects.imageAvailable,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    updateFrameTiming();
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    drawStatsUi();
    ImGui::Render();

    syncManager.waitForImageIfNeeded(&instance, imageIndex);
    vkResetFences(instance.device(), 1, &frameSyncObjects.inFlight);

    commandPool.resetBufferAtIndex(currentFrame);
    recordComputeCommand(commandPool.getBufferByIndex(currentFrame), imageIndex, camera);

    VkSemaphore waitSemaphores[] = {frameSyncObjects.imageAvailable};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
    VkSemaphore signalSemaphores[] = {syncManager.getRenderFinishedSemaphore(imageIndex)};
    VkCommandBuffer commandBuffer = commandPool.getBufferByIndex(currentFrame);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(instance.getGraphicsQueue(), 1, &submitInfo, frameSyncObjects.inFlight) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit compute command buffer!");
    }

    syncManager.setImageFence(imageIndex, frameSyncObjects.inFlight);

    VkSwapchainKHR swapChains[] = {swapchain.getHandle()};
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(instance.getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanApp::recordComputeCommand(VkCommandBuffer commandBuffer, uint32_t imageIndex, const Camera& camera) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording compute command buffer!");
    }

    recordUploads(commandBuffer);

    Image& computeImage = computeImages.at(currentFrame);
    if (!computeImagesInitialized[currentFrame]) {
        insertImageBarrier(
            commandBuffer,
            computeImage.getImage(),
            0,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );
    } else {
        insertImageBarrier(
            commandBuffer,
            computeImage.getImage(),
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        );
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    const VkDescriptorSet descriptorSet = descriptorManager.getDescriptorSet(currentFrame);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        computePipelineLayout,
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );

    const VkExtent2D extent = swapchain.getSwapExtent();
    const float verticalFovRadians = glm::radians(70.0f);
    const float focalScale = 1.0f / std::tan(verticalFovRadians * 0.5f);
    ComputePushConstants pushConstants{};
    pushConstants.cameraPos = glm::vec4(camera.getPosition(), 0.0f);
    pushConstants.cameraForward = glm::vec4(camera.getForward(), 0.0f);
    pushConstants.cameraRight = glm::vec4(camera.getRight(), 0.0f);
    pushConstants.cameraUp = glm::vec4(camera.getUp(), 0.0f);
    pushConstants.worldMin = glm::ivec4(world->getVoxelMin(), 0);
    pushConstants.worldMax = glm::ivec4(world->getVoxelMax(), 0);
    pushConstants.chunkWindowDimensions = glm::ivec4(world->getChunkCounts(), 0);
    pushConstants.renderParams = glm::vec4(rayQueryVisualizationIntensity, 2048.0f, focalScale, static_cast<float>(rayQueryVisualizationMode));

    vkCmdPushConstants(commandBuffer, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);
    vkCmdDispatch(commandBuffer, (extent.width + 7u) / 8u, (extent.height + 7u) / 8u, 1u);

    insertImageBarrier(
        commandBuffer,
        computeImage.getImage(),
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );
    insertImageBarrier(
        commandBuffer,
        swapchain.getImage(imageIndex),
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.extent = {extent.width, extent.height, 1};

    vkCmdCopyImage(
        commandBuffer,
        computeImage.getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapchain.getImage(imageIndex), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    insertImageBarrier(
        commandBuffer,
        swapchain.getImage(imageIndex),
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = imguiRenderPass;
    renderPassInfo.framebuffer = imguiFramebuffers.at(imageIndex);
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    computeImagesInitialized[currentFrame] = true;

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record compute command buffer!");
    }
}

void VulkanApp::recreateSwapChain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(instance.device());
    cleanupSwapchain();

    swapchain.init(&instance, window);
    createComputeImages();
    createDescriptorSets(false);
    createImGuiFramebuffers();
    ImGui_ImplVulkan_SetMinImageCount(static_cast<uint32_t>(swapchain.getSwapImgCount()));
    syncManager.resetImages(&instance, swapchain.getSwapImgCount());
}

void VulkanApp::cleanupImGuiFramebuffers() {
    for (VkFramebuffer framebuffer : imguiFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(instance.device(), framebuffer, nullptr);
        }
    }
    imguiFramebuffers.clear();

    for (VkImageView imageView : swapchainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(instance.device(), imageView, nullptr);
        }
    }
    swapchainImageViews.clear();
}

void VulkanApp::cleanupSwapchain() {
    cleanupImGuiFramebuffers();

    for (auto& image : computeImages) {
        image.cleanup(&instance);
    }

    computeImages.clear();
    computeImagesInitialized.clear();
    swapchain.cleanup(&instance);
}

void VulkanApp::cleanupImGui() {
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    if (imguiRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(instance.device(), imguiRenderPass, nullptr);
        imguiRenderPass = VK_NULL_HANDLE;
    }

    if (imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(instance.device(), imguiDescriptorPool, nullptr);
        imguiDescriptorPool = VK_NULL_HANDLE;
    }
}

void VulkanApp::cleanup() {
    vkDeviceWaitIdle(instance.device());
    for (size_t i = 0; i < frameUploads.size(); i++) {
        cleanupUploads(i);
    }
    cleanupSwapchain();
    cleanupImGui();

    if (computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(instance.device(), computePipeline, nullptr);
        computePipeline = VK_NULL_HANDLE;
    }

    if (computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(instance.device(), computePipelineLayout, nullptr);
        computePipelineLayout = VK_NULL_HANDLE;
    }

    brickResidency.destroyRequestBuffers(instance);
    chunkWindowIndexBuffer.cleanup(&instance);
    chunkBrickMapBuffer.cleanup(&instance);
    brickPoolBuffer.cleanup(&instance);
    descriptorManager.cleanup(&instance);
    syncManager.cleanup(&instance);
    commandPool.cleanup(&instance);
    instance.cleanup();

    glfwDestroyWindow(window);
    glfwTerminate();
}