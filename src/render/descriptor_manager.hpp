#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

class Instance;

struct DescriptorWrite {
    uint32_t binding;
    VkDescriptorType type;
    VkDescriptorBufferInfo* bufferInfo = nullptr;
    VkDescriptorImageInfo* imageInfo = nullptr;
};

class DescriptorManager {
public:
    void initLayout(Instance* instance, const std::vector<VkDescriptorSetLayoutBinding>& bindings);
    void initPool(Instance* instance, const std::vector<VkDescriptorPoolSize>& poolSizes, int maxSetCount);
    void initSets(Instance* instance, int descriptorCount, const std::vector<std::vector<DescriptorWrite>>& perFrameWrites);
    void updateSets(Instance* instance, const std::vector<std::vector<DescriptorWrite>>& perFrameWrites);
    VkDescriptorSet getDescriptorSet(size_t index) const {return _descriptorSets.at(index); }

    VkDescriptorSetLayout getLayout() const {return _descriptorSetLayout; }

    void cleanup(Instance* instance);
private:
    VkDescriptorSetLayout _descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> _descriptorSets;
};