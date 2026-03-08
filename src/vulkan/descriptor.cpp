#include "descriptor.hpp"

namespace xebble::vk {

struct DescriptorPool::Impl {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;

    ~Impl() {
        if (pool)
            vkDestroyDescriptorPool(device, pool, nullptr);
    }
};

std::expected<DescriptorPool, Error>
DescriptorPool::create(VkDevice device, uint32_t max_sets,
                       std::vector<VkDescriptorPoolSize> pool_sizes) {
    VkDescriptorPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets = max_sets;
    ci.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    ci.pPoolSizes = pool_sizes.data();

    DescriptorPool dp;
    dp.impl_ = std::make_unique<Impl>();
    dp.impl_->device = device;

    if (vkCreateDescriptorPool(device, &ci, nullptr, &dp.impl_->pool) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create descriptor pool"});
    }
    return dp;
}

DescriptorPool::~DescriptorPool() = default;
DescriptorPool::DescriptorPool(DescriptorPool&&) noexcept = default;
DescriptorPool& DescriptorPool::operator=(DescriptorPool&&) noexcept = default;

std::expected<VkDescriptorSet, Error> DescriptorPool::allocate(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = impl_->pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;

    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(impl_->device, &ai, &set) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to allocate descriptor set"});
    }
    return set;
}

VkDescriptorPool DescriptorPool::handle() const {
    return impl_->pool;
}

std::expected<VkDescriptorSetLayout, Error> create_single_texture_layout(VkDevice device,
                                                                         VkShaderStageFlags stage) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = stage;

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings = &binding;

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create descriptor set layout"});
    }
    return layout;
}

void write_texture_descriptor(VkDevice device, VkDescriptorSet set, VkImageView image_view,
                              VkSampler sampler) {
    VkDescriptorImageInfo image_info{};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = image_view;
    image_info.sampler = sampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &image_info;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

} // namespace xebble::vk
