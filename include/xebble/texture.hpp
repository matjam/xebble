/// @file texture.hpp
/// @brief GPU texture loading and management.
///
/// Provides a move-only Texture class that owns a VkImage, VkImageView,
/// VkSampler, and VMA allocation. Textures can be loaded from files (via
/// stb_image), from in-memory data (for ZIP archive support), or created
/// empty (for render targets like the offscreen framebuffer).
///
/// All textures use nearest-neighbor sampling by default for pixel-perfect
/// rendering. The load functions handle staging buffer upload and image
/// layout transitions automatically.
#pragma once

#include <xebble/types.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <expected>
#include <filesystem>
#include <memory>

namespace xebble::vk {
class Context;
}

namespace xebble {

/// @brief Owns a GPU texture (VkImage + VkImageView + VkSampler + VmaAllocation).
///
/// Move-only. Created via static factory methods. Uses nearest-neighbor
/// filtering for pixel-art rendering.
class Texture {
public:
    /// @brief Load a texture from a file on disk.
    /// @param ctx Vulkan context for GPU resource creation.
    /// @param path Path to an image file (PNG, BMP, etc. — anything stb_image supports).
    static std::expected<Texture, Error> load(
        vk::Context& ctx,
        const std::filesystem::path& path);

    /// @brief Load a texture from in-memory image data (e.g. from a ZIP archive).
    /// @param ctx Vulkan context.
    /// @param data Pointer to encoded image data (PNG, BMP, etc.).
    /// @param size Size of the data in bytes.
    static std::expected<Texture, Error> load_from_memory(
        vk::Context& ctx,
        const uint8_t* data, size_t size);

    /// @brief Create an empty texture (e.g. for offscreen framebuffer render targets).
    /// @param ctx Vulkan context.
    /// @param width Texture width in pixels.
    /// @param height Texture height in pixels.
    /// @param format Vulkan image format.
    /// @param usage Additional usage flags beyond SAMPLED and TRANSFER_DST.
    static std::expected<Texture, Error> create_empty(
        vk::Context& ctx,
        uint32_t width, uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage = 0);

    ~Texture();
    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    uint32_t width() const;
    uint32_t height() const;
    VkImage image() const;
    VkImageView image_view() const;
    VkSampler sampler() const;

    /// @brief Create a texture from decoded RGBA pixel data.
    /// @param ctx Vulkan context.
    /// @param pixels Pointer to RGBA pixel data (4 bytes per pixel).
    /// @param width Image width in pixels.
    /// @param height Image height in pixels.
    static std::expected<Texture, Error> create_from_pixels(
        vk::Context& ctx, const uint8_t* pixels,
        uint32_t width, uint32_t height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Texture() = default;
};

} // namespace xebble
