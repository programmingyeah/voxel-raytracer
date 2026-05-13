#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdexcept>

class Instance;

class Image {
public:
    void create(Instance* instance,
        uint32_t width, 
        uint32_t height, 
        VkSampleCountFlagBits sampleCount, 
        VkFormat format, 
        VkImageTiling tiling, 
        VkImageUsageFlags usage, 
        VkMemoryPropertyFlags properties, 
        VkImageAspectFlags aspectFlags);

    VkImageView getView() const { return imageView; }
    VkImage getImage() const { return image; }
    VkFormat getFormat() const { return format; }
    VkExtent3D getExtent() const { return extent; }
    void cleanup(Instance* instance);

private:
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;

    VkFormat format{};
    VkExtent3D extent{};
    VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};
};
