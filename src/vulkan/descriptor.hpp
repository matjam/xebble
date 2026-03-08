/// @file descriptor.hpp
/// @brief Descriptor pool, set layout, and set allocation helpers.
///
/// Provides a simple DescriptorPool RAII wrapper and free functions for
/// creating descriptor set layouts and writing image descriptors.
#pragma once

#include <xebble/types.hpp>

#include <vulkan/vulkan.h>

#include <expected>
#include <memory>
#include <vector>

namespace xebble::vk {

/// @brief Owns a VkDescriptorPool and provides set allocation. Move-only.
class DescriptorPool {
public:
    /// @brief Create a descriptor pool.
    /// @param device Vulkan logical device.
    /// @param max_sets Maximum number of descriptor sets that can be allocated.
    /// @param pool_sizes Array of pool size entries describing the pool capacity.
    [[nodiscard]] static std::expected<DescriptorPool, Error>
    create(VkDevice device, uint32_t max_sets, std::vector<VkDescriptorPoolSize> pool_sizes);

    ~DescriptorPool();
    DescriptorPool(DescriptorPool&&) noexcept;
    DescriptorPool& operator=(DescriptorPool&&) noexcept;
    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    /// @brief Allocate a descriptor set from this pool.
    /// @param layout The descriptor set layout to allocate with.
    [[nodiscard]] std::expected<VkDescriptorSet, Error> allocate(VkDescriptorSetLayout layout);

    [[nodiscard]] VkDescriptorPool handle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    DescriptorPool() = default;
};

/// @brief Create a descriptor set layout with a single combined image sampler at binding 0.
/// @param device Vulkan logical device.
/// @param stage Shader stage flags for the binding.
[[nodiscard]] std::expected<VkDescriptorSetLayout, Error>
create_single_texture_layout(VkDevice device,
                             VkShaderStageFlags stage = VK_SHADER_STAGE_FRAGMENT_BIT);

/// @brief Write a combined image sampler descriptor to a set at binding 0.
/// @param device Vulkan logical device.
/// @param set The descriptor set to write to.
/// @param image_view The image view to bind.
/// @param sampler The sampler to bind.
void write_texture_descriptor(VkDevice device, VkDescriptorSet set, VkImageView image_view,
                              VkSampler sampler);

} // namespace xebble::vk
