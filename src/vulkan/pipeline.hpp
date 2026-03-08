/// @file pipeline.hpp
/// @brief Vulkan graphics pipeline builder with SPIR-V loading.
///
/// Provides a Pipeline RAII wrapper and a builder for creating the two
/// pipelines used by Xebble:
/// - **Sprite pipeline**: Instanced quads with per-instance position, UV,
///   quad size, and color. Alpha blending enabled, no depth test.
/// - **Blit pipeline**: Fullscreen triangle with texture sampling for the
///   offscreen-to-swapchain blit. No vertex input, no blending.
#pragma once

#include <xebble/types.hpp>

#include <vulkan/vulkan.h>

#include <expected>
#include <filesystem>
#include <memory>

namespace xebble::vk {

/// @brief Owns a VkPipeline and VkPipelineLayout. Move-only.
class Pipeline {
public:
    ~Pipeline();
    Pipeline(Pipeline&&) noexcept;
    Pipeline& operator=(Pipeline&&) noexcept;
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    VkPipeline handle() const;
    VkPipelineLayout layout() const;

    /// @brief Create the sprite rendering pipeline.
    /// @param device Vulkan logical device.
    /// @param render_pass The render pass this pipeline will be used with.
    /// @param descriptor_set_layout Layout for texture descriptor (binding 0).
    /// @param vert_path Path to compiled sprite.vert.spv.
    /// @param frag_path Path to compiled sprite.frag.spv.
    static std::expected<Pipeline, Error> create_sprite_pipeline(
        VkDevice device, VkRenderPass render_pass, VkDescriptorSetLayout descriptor_set_layout,
        const std::filesystem::path& vert_path, const std::filesystem::path& frag_path);

    /// @brief Create the fullscreen blit pipeline.
    /// @param device Vulkan logical device.
    /// @param render_pass The render pass this pipeline will be used with.
    /// @param descriptor_set_layout Layout for texture descriptor (binding 0).
    /// @param vert_path Path to compiled blit.vert.spv.
    /// @param frag_path Path to compiled blit.frag.spv.
    static std::expected<Pipeline, Error> create_blit_pipeline(
        VkDevice device, VkRenderPass render_pass, VkDescriptorSetLayout descriptor_set_layout,
        const std::filesystem::path& vert_path, const std::filesystem::path& frag_path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Pipeline() = default;
};

} // namespace xebble::vk
