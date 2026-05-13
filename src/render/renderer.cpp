#include "renderer.hpp"
#include <stdexcept>
#include <string>
#include <vector>
#include "utils.hpp"

namespace {
struct ComputePushConstants {
    glm::vec4 cameraPos;
    glm::vec4 cameraForward;
    glm::vec4 cameraRight;
    glm::vec4 cameraUp;
    glm::ivec4 worldDimensions;
    glm::vec4 renderParams;
};

void insertImageBarrier(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStageMask,
        dstStageMask,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}
}

void VulkanApp::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;
    auto app = reinterpret_cast<VulkanApp*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

GLFWwindow* VulkanApp::init(const VoxelWorld& worldRef) {
    world = &worldRef;
    initWindow();
    initVulkan();

    return window;
}

void VulkanApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void VulkanApp::initVulkan() {
    instance.init(window);
    swapchain.init(&instance, window);
    commandPool.init(&instance);

    VkDescriptorSetLayoutBinding storageImageBinding{};
    storageImageBinding.binding = 0;
    storageImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    storageImageBinding.descriptorCount = 1;
    storageImageBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    storageImageBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding voxelBufferBinding{};
    voxelBufferBinding.binding = 1;
    voxelBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    voxelBufferBinding.descriptorCount = 1;
    voxelBufferBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    voxelBufferBinding.pImmutableSamplers = nullptr;

    descriptorManager.initLayout(&instance, {storageImageBinding, voxelBufferBinding});
    descriptorManager.initPool(
        &instance,
        {
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_FRAMES_IN_FLIGHT},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT}
        },
        MAX_FRAMES_IN_FLIGHT
    );

    createComputeImages();
    createVoxelBuffer();
    createDescriptorSets(true);
    createComputePipeline();
    commandPool.allocateCommandBuffers(&instance, MAX_FRAMES_IN_FLIGHT);
    syncManager.init(&instance, swapchain.getSwapImgCount(), MAX_FRAMES_IN_FLIGHT);
}

VkShaderModule VulkanApp::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(instance.device(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute shader module!");
    }

    return shaderModule;
}

void VulkanApp::createComputePipeline() {
    const auto shaderCode = readFile(std::string(SHADER_DIR) + "/comp.spv");
    VkShaderModule shaderModule = createShaderModule(shaderCode);

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = shaderModule;
    shaderStage.pName = "main";

    VkDescriptorSetLayout descriptorSetLayout = descriptorManager.getLayout();
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ComputePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(instance.device(), &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(instance.device(), shaderModule, nullptr);
        throw std::runtime_error("failed to create compute pipeline layout!");
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = computePipelineLayout;

    if (vkCreateComputePipelines(instance.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(instance.device(), shaderModule, nullptr);
        throw std::runtime_error("failed to create compute pipeline!");
    }

    vkDestroyShaderModule(instance.device(), shaderModule, nullptr);
}

void VulkanApp::createComputeImages() {
    if (swapchain.getSwapImgFormat() != VK_FORMAT_R8G8B8A8_UNORM) {
        throw std::runtime_error("compute path currently requires VK_FORMAT_R8G8B8A8_UNORM swapchain images");
    }

    computeImages.clear();
    computeImages.resize(MAX_FRAMES_IN_FLIGHT);
    computeImagesInitialized.assign(MAX_FRAMES_IN_FLIGHT, false);

    const VkExtent2D extent = swapchain.getSwapExtent();
    for (auto& image : computeImages) {
        image.create(
            &instance,
            extent.width,
            extent.height,
            VK_SAMPLE_COUNT_1_BIT,
            swapchain.getSwapImgFormat(),
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );
    }
}

void VulkanApp::createVoxelBuffer() {
    if (world == nullptr) {
        throw std::runtime_error("voxel world must be set before creating the voxel buffer");
    }

    const std::vector<uint32_t> voxels = world->buildVoxelBuffer();
    const VkDeviceSize voxelBufferSize = sizeof(uint32_t) * voxels.size();

    voxelBuffer.createBuffer(
        &instance,
        voxelBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    voxelBuffer.upload(&instance, voxels.data(), voxelBufferSize);
}

void VulkanApp::createDescriptorSets(bool allocateSets) {
    std::vector<VkDescriptorImageInfo> imageInfos(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorBufferInfo> bufferInfos(MAX_FRAMES_IN_FLIGHT);
    std::vector<std::vector<DescriptorWrite>> descriptorWrites(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        imageInfos[i].sampler = VK_NULL_HANDLE;
        imageInfos[i].imageView = computeImages[i].getView();
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        descriptorWrites[i].push_back({
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            nullptr,
            &imageInfos[i]
        });

        bufferInfos[i].buffer = voxelBuffer.buffer;
        bufferInfos[i].offset = 0;
        bufferInfos[i].range = voxelBuffer.size;

        descriptorWrites[i].push_back({
            1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            &bufferInfos[i],
            nullptr
        });
    }

    if (allocateSets) {
        descriptorManager.initSets(&instance, MAX_FRAMES_IN_FLIGHT, descriptorWrites);
    } else {
        descriptorManager.updateSets(&instance, descriptorWrites);
    }
}

void VulkanApp::cleanup() {
    vkDeviceWaitIdle(instance.device());
    cleanupSwapchain();

    if (computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(instance.device(), computePipeline, nullptr);
        computePipeline = VK_NULL_HANDLE;
    }

    if (computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(instance.device(), computePipelineLayout, nullptr);
        computePipelineLayout = VK_NULL_HANDLE;
    }

    voxelBuffer.cleanup(&instance);
    descriptorManager.cleanup(&instance);
    syncManager.cleanup(&instance);
    commandPool.cleanup(&instance);
    instance.cleanup();

    glfwDestroyWindow(window);
    glfwTerminate();
}

void VulkanApp::drawFrame(const Camera& camera) {
    FrameSyncObjects frameSyncObjects = syncManager.getFrame(currentFrame);

    vkWaitForFences(instance.device(), 1, &frameSyncObjects.inFlight, VK_TRUE, UINT64_MAX);

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

    Image& computeImage = computeImages[currentFrame];
    const VkExtent2D extent = swapchain.getSwapExtent();

    insertImageBarrier(
        commandBuffer,
        computeImage.getImage(),
        computeImagesInitialized[currentFrame] ? VK_ACCESS_TRANSFER_READ_BIT : 0,
        VK_ACCESS_SHADER_WRITE_BIT,
        computeImagesInitialized[currentFrame] ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL,
        computeImagesInitialized[currentFrame] ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
    );

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    VkDescriptorSet descriptorSet = descriptorManager.getDescriptorSet(currentFrame);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    const glm::uvec3 worldDimensions = world->getVoxelDimensions();
    ComputePushConstants pushConstants{};
    pushConstants.cameraPos = glm::vec4(camera.getPosition(), 1.0f);
    pushConstants.cameraForward = glm::vec4(camera.getForward(), 0.0f);
    pushConstants.cameraRight = glm::vec4(camera.getRight(), 0.0f);
    pushConstants.cameraUp = glm::vec4(camera.getUp(), 0.0f);
    pushConstants.worldDimensions = glm::ivec4(
        static_cast<int>(worldDimensions.x),
        static_cast<int>(worldDimensions.y),
        static_cast<int>(worldDimensions.z),
        0
    );
    pushConstants.renderParams = glm::vec4(0.1f, static_cast<float>(glm::length(glm::vec3(worldDimensions))) * 2.0f, 1.4f, 0.0f);

    vkCmdPushConstants(
        commandBuffer,
        computePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(ComputePushConstants),
        &pushConstants
    );

    const uint32_t groupCountX = (extent.width + 7) / 8;
    const uint32_t groupCountY = (extent.height + 7) / 8;
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);

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
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
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
        0,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
    );

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
    syncManager.resetImages(&instance, swapchain.getSwapImgCount());
}

void VulkanApp::cleanupSwapchain() {
    for (auto& image : computeImages) {
        image.cleanup(&instance);
    }

    computeImages.clear();
    computeImagesInitialized.clear();
    swapchain.cleanup(&instance);
}
