/// @file renderer.hpp
/// @brief Core renderer with offscreen framebuffer and pixel-perfect blit.
///
/// The Renderer is the main public interface for all drawing. It renders to
/// an offscreen framebuffer at the virtual resolution, then blits to the
/// swapchain with nearest-neighbor filtering and letterboxing. All draw calls
/// between begin_frame() and end_frame() are batched and submitted together.
///
/// The renderer uses 2 frames in flight for optimal GPU utilization.
#pragma once

#include <xebble/types.hpp>
#include <xebble/texture.hpp>
#include <expected>
#include <memory>
#include <span>
#include <vector>

namespace xebble {

class Window;
class SpriteSheet;
struct Sprite;
struct TileMap;
struct TextBlock;

namespace vk {
class Context;
}

/// @brief Configuration for the renderer.
struct RendererConfig {
    uint32_t virtual_width = 640;   ///< Virtual framebuffer width in pixels.
    uint32_t virtual_height = 360;  ///< Virtual framebuffer height in pixels.
    bool vsync = true;              ///< Enable vertical sync.
    bool nearest_filter = true;     ///< Use nearest-neighbor filtering (pixel-perfect).
};

/// @brief A named resolution option.
struct Resolution {
    uint32_t width;
    uint32_t height;
    std::string label;
};

/// @brief Per-instance data for sprite rendering. Matches the vertex shader layout.
struct SpriteInstance {
    float pos_x, pos_y;         ///< Position in virtual pixels.
    float uv_x, uv_y;          ///< Top-left UV in spritesheet.
    float uv_w, uv_h;          ///< UV width/height.
    float quad_w, quad_h;       ///< Quad size in virtual pixels.
    float r, g, b, a;           ///< Color tint (1,1,1,1 = no tint).
};

/// @brief Core renderer. Owns Vulkan state, pipelines, and the offscreen framebuffer.
///
/// Usage: create(), then each frame call begin_frame(), draw(), end_frame().
/// All draw calls are batched per-texture and submitted in end_frame().
class Renderer {
public:
    /// @brief Create the renderer, initializing Vulkan and all GPU resources.
    /// @param window The window to render into.
    /// @param config Renderer configuration (virtual resolution, vsync, etc.).
    static std::expected<Renderer, Error> create(Window& window, const RendererConfig& config);

    ~Renderer();
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// @brief Begin a new frame. Must be called before any draw calls.
    /// @return true if the frame was begun successfully, false if the window is minimized.
    bool begin_frame();

    /// @brief End the current frame. Submits all batched draw commands and presents.
    void end_frame();

    /// @brief Submit sprite instances for rendering with a given texture.
    /// @param instances Span of sprite instance data.
    /// @param texture The texture to sample from.
    /// @param z_order Draw order (lower = drawn first / behind).
    void submit_instances(std::span<const SpriteInstance> instances,
                          const Texture& texture, float z_order = 0.0f);

    /// @brief Set the letterbox border color.
    void set_border_color(Color color);

    /// @brief Time elapsed since the previous frame, in seconds.
    float delta_time() const;

    /// @brief Total time elapsed since renderer creation, in seconds.
    float elapsed_time() const;

    /// @brief Total number of frames rendered.
    uint64_t frame_count() const;

    /// @brief Get the virtual resolution width.
    uint32_t virtual_width() const;

    /// @brief Get the virtual resolution height.
    uint32_t virtual_height() const;

    /// @brief Access the underlying Vulkan context (for texture/spritesheet creation).
    vk::Context& context();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Renderer() = default;
};

} // namespace xebble
