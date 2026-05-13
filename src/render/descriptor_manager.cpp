#include "descriptor_manager.hpp"
#include <stdexcept>
#include "instance.hpp"

void DescriptorManager::initLayout(Instance* instance, const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(instance->device(), &layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void DescriptorManager::initPool(Instance* instance, const std::vector<VkDescriptorPoolSize>& poolSizes, int maxSetCount) {
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(maxSetCount);

    if (vkCreateDescriptorPool(instance->device(), &poolInfo, nullptr, &_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void DescriptorManager::initSets(Instance* instance,
                                 int descriptorCount,
                                 const std::vector<std::vector<DescriptorWrite>>& perFrameWrites) {
    std::vector<VkDescriptorSetLayout> layouts(descriptorCount, _descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = _descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(descriptorCount);
    allocInfo.pSetLayouts = layouts.data();

    _descriptorSets.resize(descriptorCount);
    if (vkAllocateDescriptorSets(instance->device(), &allocInfo, _descriptorSets.data()) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate descriptor sets!");

    updateSets(instance, perFrameWrites);
}

void DescriptorManager::updateSets(Instance* instance, const std::vector<std::vector<DescriptorWrite>>& perFrameWrites) {
    for (uint32_t i = 0; i < _descriptorSets.size(); i++) {
        std::vector<VkWriteDescriptorSet> writes;
        writes.reserve(perFrameWrites[i].size());

        for (const auto& w : perFrameWrites[i]) {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = _descriptorSets[i];
            write.dstBinding = w.binding;
            write.descriptorType = w.type;
            write.descriptorCount = 1;
            write.pBufferInfo = w.bufferInfo;
            write.pImageInfo = w.imageInfo;
            writes.push_back(write);
        }

        vkUpdateDescriptorSets(instance->device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void DescriptorManager::cleanup(Instance* instance) {
    if (_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(instance->device(), _descriptorPool, nullptr);
        _descriptorPool = VK_NULL_HANDLE;
    }

    if (_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(instance->device(), _descriptorSetLayout, nullptr);
        _descriptorSetLayout = VK_NULL_HANDLE;
    }

    _descriptorSets.clear();
}