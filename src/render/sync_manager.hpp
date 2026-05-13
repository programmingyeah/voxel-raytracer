#pragma once
class Instance;
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

struct FrameSyncObjects {
    VkSemaphore imageAvailable;
    VkFence inFlight;
};

class SyncManager {
public:
    void init(Instance* instance, size_t swapImgCount, size_t count);

    FrameSyncObjects getFrame(size_t index) const {return _frames.at(index); };

    VkSemaphore getRenderFinishedSemaphore(uint32_t imageIndex) const { return renderFinishedSemaphores.at(imageIndex); }

    void waitForImageIfNeeded(Instance* instance, uint32_t imageIndex);
    void setImageFence(uint32_t imageIndex, VkFence fence);
    void resetImages(Instance* instance, size_t swapImgCount);

    void cleanup(Instance* instance);
private:
    std::vector<FrameSyncObjects> _frames;
    std::vector<VkFence> imagesInFlight;
    std::vector<VkSemaphore> renderFinishedSemaphores;
};