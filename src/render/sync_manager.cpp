#include "sync_manager.hpp"
#include "instance.hpp"
#include <stdexcept>

void SyncManager::init(Instance* instance, size_t swapImgCount, size_t maxFramesInFlight) {
    _frames.resize(maxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < maxFramesInFlight; i++) {
        if (vkCreateSemaphore(instance->device(), &semaphoreInfo, nullptr, &_frames[i].imageAvailable) != VK_SUCCESS ||
            vkCreateFence(instance->device(), &fenceInfo, nullptr, &_frames[i].inFlight) != VK_SUCCESS) {
            throw std::runtime_error("failed to create sync objects for a frame!");
        }
    }

    resetImages(instance, swapImgCount);
}

void SyncManager::waitForImageIfNeeded(Instance* instance, uint32_t imageIndex) {
    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(instance->device(), 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
}

void SyncManager::setImageFence(uint32_t imageIndex, VkFence fence) {
    imagesInFlight[imageIndex] = fence;
}

void SyncManager::resetImages(Instance* instance, size_t swapImgCount) {
    for (VkSemaphore semaphore : renderFinishedSemaphores) {
        vkDestroySemaphore(instance->device(), semaphore, nullptr);
    }

    imagesInFlight.assign(swapImgCount, VK_NULL_HANDLE);
    renderFinishedSemaphores.resize(swapImgCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (VkSemaphore& semaphore : renderFinishedSemaphores) {
        if (vkCreateSemaphore(instance->device(), &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("failed to create render-finished semaphore!");
        }
    }
}

void SyncManager::cleanup(Instance* instance) {
    for (VkSemaphore semaphore : renderFinishedSemaphores) {
        vkDestroySemaphore(instance->device(), semaphore, nullptr);
    }

    for (FrameSyncObjects frame : _frames) {
        vkDestroySemaphore(instance->device(), frame.imageAvailable, nullptr);
        vkDestroyFence(instance->device(), frame.inFlight, nullptr);
    }
}