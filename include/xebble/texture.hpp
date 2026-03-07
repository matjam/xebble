/// @file texture.hpp
/// @brief Move-only GPU texture owning a VkImage, VkImageView, VkSampler, and
///        VMA allocation.
///
/// Textures are the lowest-level image primitive in Xebble. They are used
/// directly by `SpriteSheet` and `Font`, and are occasionally needed when
/// working with raw `SpriteInstance` data.
///
/// All textures use **nearest-neighbor sampling** by default, which gives
/// crisp pixel-art rendering without blurring. The factory functions handle
/// staging-buffer upload and image-layout transitions automatically — you
/// never need to touch Vulkan barriers directly.
///
/// ## Typical use
///
/// Most developers never construct a Texture directly. Instead they use
/// `SpriteSheet::load()` or `Font::load()` which create textures internally.
/// Direct texture creation is only needed for:
/// - Loading raw image data from a custom source (e.g. procedurally generated).
/// - Creating render targets for post-processing effects.
///
/// @code
/// // Load from disk (most common case, usually done via AssetManager).
/// auto tex = Texture::load(renderer.context(), "ui_icons.png");
/// if (!tex) { log(LogLevel::Error, tex.error().message); return 1; }
///
/// // Load from memory (e.g. an image that was read from a ZIP archive).
/// auto raw = asset_manager.read_raw("font_atlas.png").value();
/// auto tex = Texture::load_from_memory(renderer.context(),
///                                       raw.data(), raw.size());
///
/// // Create a procedural texture from pixel data.
/// std::vector<uint8_t> pixels(64 * 64 * 4, 255); // 64x64 opaque white
/// auto tex = Texture::create_from_pixels(renderer.context(),
///                                         pixels.data(), 64, 64);
/// @endcode
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
/// Move-only — do not copy. Created exclusively through the static factory
/// methods. The destructor frees all GPU resources automatically (RAII).
class Texture {
public:
    /// @brief Load a texture from a file on disk.
    ///
    /// Supports any format that `stb_image` handles: PNG, JPG, BMP, TGA, GIF,
    /// PSD, HDR, PIC, PNM. RGBA channels are extracted; grayscale images are
    /// promoted to RGBA automatically.
    ///
    /// @param ctx   Vulkan context from `Renderer::context()`.
    /// @param path  Path to the image file.
    ///
    /// @code
    /// auto tex = Texture::load(renderer.context(), "assets/spritesheet.png");
    /// if (!tex) {
    ///     log(LogLevel::Error, "failed to load texture: " + tex.error().message);
    ///     return 1;
    /// }
    /// @endcode
    static std::expected<Texture, Error> load(
        vk::Context& ctx,
        const std::filesystem::path& path);

    /// @brief Load a texture from in-memory encoded image data.
    ///
    /// Use this when the image bytes were read from a ZIP archive or embedded
    /// in the binary rather than from a loose file.
    ///
    /// @param ctx   Vulkan context.
    /// @param data  Pointer to encoded image bytes (PNG, JPG, etc.).
    /// @param size  Number of bytes.
    ///
    /// @code
    /// // Read from the AssetManager's archive and create a texture.
    /// auto bytes = assets.read_raw("sprites/hero.png").value();
    /// auto tex   = Texture::load_from_memory(renderer.context(),
    ///                                         bytes.data(), bytes.size());
    /// @endcode
    static std::expected<Texture, Error> load_from_memory(
        vk::Context& ctx,
        const uint8_t* data, size_t size);

    /// @brief Create an empty texture suitable for use as a render target.
    ///
    /// Used internally by the renderer for its offscreen framebuffer. You would
    /// use this directly only if implementing custom post-process pipelines or
    /// deferred rendering passes.
    ///
    /// @param ctx     Vulkan context.
    /// @param width   Width in pixels.
    /// @param height  Height in pixels.
    /// @param format  Vulkan image format (e.g. `VK_FORMAT_R8G8B8A8_UNORM`).
    /// @param usage   Additional `VkImageUsageFlags` beyond `SAMPLED | TRANSFER_DST`.
    static std::expected<Texture, Error> create_empty(
        vk::Context& ctx,
        uint32_t width, uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage = 0);

    /// @brief Create a texture from a raw RGBA pixel buffer in CPU memory.
    ///
    /// Useful for procedurally generated textures — noise maps, minimap
    /// renders, dynamic palettes, or debug visualisations.
    ///
    /// @param ctx     Vulkan context.
    /// @param pixels  Pointer to RGBA pixel data (4 bytes per pixel, row-major).
    /// @param width   Image width in pixels.
    /// @param height  Image height in pixels.
    ///
    /// @code
    /// // Generate a simple checkerboard pattern.
    /// const uint32_t W = 64, H = 64;
    /// std::vector<uint8_t> pixels(W * H * 4);
    /// for (uint32_t y = 0; y < H; ++y)
    ///     for (uint32_t x = 0; x < W; ++x) {
    ///         uint8_t v = ((x / 8 + y / 8) % 2) ? 200 : 40;
    ///         size_t i = (y * W + x) * 4;
    ///         pixels[i+0] = pixels[i+1] = pixels[i+2] = v;
    ///         pixels[i+3] = 255;
    ///     }
    /// auto tex = Texture::create_from_pixels(renderer.context(),
    ///                                         pixels.data(), W, H).value();
    /// @endcode
    static std::expected<Texture, Error> create_from_pixels(
        vk::Context& ctx, const uint8_t* pixels,
        uint32_t width, uint32_t height);

    ~Texture();
    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    uint32_t   width()      const;  ///< Texture width in pixels.
    uint32_t   height()     const;  ///< Texture height in pixels.
    VkImage    image()      const;  ///< Underlying Vulkan image handle.
    VkImageView image_view() const; ///< Image view used for sampling.
    VkSampler  sampler()    const;  ///< Sampler handle (nearest-neighbor by default).

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Texture() = default;
};

} // namespace xebble
