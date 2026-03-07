/// @file renderer.hpp
/// @brief Vulkan-backed 2D renderer with a fixed virtual resolution.
///
/// The Renderer renders to an offscreen framebuffer at the configured
/// **virtual resolution** (e.g. 320×180), then blits it to the swapchain with
/// nearest-neighbor filtering and automatic letterboxing/pillarboxing so the
/// pixel-art content always appears sharp regardless of window size.
///
/// The internal pipeline uses two frames in flight for optimal GPU utilisation.
/// All draw calls between `begin_frame()` and `end_frame()` are batched by
/// texture and submitted to the GPU together.
///
/// ## Typical frame loop (custom — not needed when using `run()`)
///
/// @code
/// while (!window.should_close()) {
///     window.poll_events();
///
///     if (renderer.begin_frame()) {
///         // Submit tilemaps, sprites, and text.
///         world.tick_draw(renderer);
///
///         renderer.end_frame();
///     }
/// }
/// @endcode
///
/// ## Virtual resolution
///
/// All coordinates passed to the renderer are in **virtual pixels**. Think of
/// the virtual framebuffer as the "game screen" at a retro resolution. The
/// renderer scales it up to the actual window at display time.
///
/// @code
/// // Configure a 320×180 virtual screen (16:9 at 2× for a 640×360 window).
/// RendererConfig cfg;
/// cfg.virtual_width  = 320;
/// cfg.virtual_height = 180;
/// cfg.vsync          = true;
/// @endcode
///
/// ## Coordinate conversion
///
/// Mouse events are in screen (physical) pixels. Convert them to virtual pixel
/// space before using them for game-world hit-testing:
///
/// @code
/// Vec2 vpos = renderer.screen_to_virtual(mouse_event.position);
/// IVec2 tile = {(int)vpos.x / TILE_SIZE, (int)vpos.y / TILE_SIZE};
/// @endcode
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
struct TileMap;
struct TextBlock;

namespace vk {
class Context;
}

// ---------------------------------------------------------------------------
// RendererConfig
// ---------------------------------------------------------------------------

/// @brief Configuration for `Renderer::create()`.
struct RendererConfig {
    uint32_t virtual_width  = 640;   ///< Virtual framebuffer width  in pixels.
    uint32_t virtual_height = 360;   ///< Virtual framebuffer height in pixels.
    bool     vsync          = true;  ///< Enable vertical sync (prevents tearing).
    bool     nearest_filter = true;  ///< Nearest-neighbor blit — keeps pixels sharp.
};

/// @brief A named resolution option (for a settings menu, for example).
struct Resolution {
    uint32_t    width;
    uint32_t    height;
    std::string label;  ///< Human-readable name, e.g. "1920×1080".
};

// ---------------------------------------------------------------------------
// SpriteInstance
// ---------------------------------------------------------------------------

/// @brief Per-sprite data fed to the sprite vertex shader.
///
/// Built by `SpriteRenderSystem` and `TileMapRenderSystem`. You only need to
/// construct these manually if building a custom rendering system.
///
/// All positional values are in **virtual pixels** (top-left origin).
/// Colour channels are in the range [0.0, 1.0].
///
/// @code
/// Rect uv = sheet.region(tile_index);
/// SpriteInstance inst;
/// inst.pos_x  = entity_x;             inst.pos_y  = entity_y;
/// inst.uv_x   = uv.x;                 inst.uv_y   = uv.y;
/// inst.uv_w   = uv.w;                 inst.uv_h   = uv.h;
/// inst.quad_w = (float)sheet.tile_width();
/// inst.quad_h = (float)sheet.tile_height();
/// inst.r = 1.0f; inst.g = 1.0f; inst.b = 1.0f; inst.a = 1.0f;
///
/// renderer.submit_instances({&inst, 1}, sheet.texture(), z_order);
/// @endcode
struct SpriteInstance {
    float pos_x, pos_y;    ///< Top-left position in virtual pixels (before pivot/rotation).
    float uv_x, uv_y;     ///< Top-left UV coordinate (normalised 0–1).
    float uv_w, uv_h;     ///< UV extent (normalised 0–1).
    float quad_w, quad_h; ///< Unscaled quad size in virtual pixels (shader applies scale).
    float r, g, b, a;     ///< Multiplicative colour tint (1,1,1,1 = no tint).
    float scale;           ///< Uniform scale multiplier (1 = native size).
    float rotation;        ///< Rotation in radians, counter-clockwise.
    float pivot_x;         ///< Pivot X in 0–1 quad-local space (0=left, 1=right).
    float pivot_y;         ///< Pivot Y in 0–1 quad-local space (0=top,  1=bottom).
};

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

/// @brief Vulkan-backed 2D renderer. Owns all GPU state and pipelines.
///
/// Move-only. Created via `Renderer::create()`. Manages the swapchain,
/// offscreen framebuffer, sprite pipeline, and frame synchronisation.
class Renderer {
public:
    /// @brief Initialise Vulkan and create all GPU resources.
    ///
    /// @param window  The window to render into. Must outlive the Renderer.
    /// @param config  Virtual resolution, vsync, and filter settings.
    ///
    /// @code
    /// auto renderer = Renderer::create(window, {
    ///     .virtual_width  = 320,
    ///     .virtual_height = 180,
    ///     .vsync          = true,
    /// });
    /// if (!renderer) {
    ///     log(LogLevel::Error, renderer.error().message);
    ///     return 1;
    /// }
    /// @endcode
    static std::expected<Renderer, Error> create(Window& window, const RendererConfig& config);

    ~Renderer();
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// @brief Begin a new frame. Must be called before any draw calls.
    ///
    /// Returns `false` if the window is minimised (zero framebuffer size) —
    /// skip `end_frame()` and all draw calls in this case.
    ///
    /// @code
    /// if (renderer.begin_frame()) {
    ///     submit_all_draw_calls(renderer);
    ///     renderer.end_frame();
    /// }
    /// @endcode
    bool begin_frame();

    /// @brief Finish the frame — flush all batched draw commands and present.
    ///
    /// Must be called exactly once after a successful `begin_frame()`.
    void end_frame();

    /// @brief Submit a batch of sprite instances for rendering.
    ///
    /// All instances are drawn using the same @p texture. Calls with the same
    /// texture and z_order are automatically merged into a single GPU draw.
    ///
    /// @param instances  Span of `SpriteInstance` data (any size).
    /// @param texture    Atlas texture to sample from.
    /// @param z_order    Draw order — lower values are drawn first (behind).
    ///
    /// @code
    /// // Draw a 16×16 tile sprite.
    /// Rect uv = sheet.region(tile_index);
    /// SpriteInstance inst{x, y, uv.x, uv.y, uv.w, uv.h, 16, 16, 1, 1, 1, 1};
    /// renderer.submit_instances({&inst, 1}, sheet.texture(), 2.0f);
    /// @endcode
    void submit_instances(std::span<const SpriteInstance> instances,
                          const Texture& texture, float z_order = 0.0f);

    /// @brief Set the letterbox/pillarbox border colour (default black).
    ///
    /// @code
    /// // A deep blue border to complement a dark fantasy aesthetic.
    /// renderer.set_border_color({5, 5, 20, 255});
    /// @endcode
    void set_border_color(Color color);

    /// @brief Seconds elapsed since the previous frame (wall-clock).
    ///
    /// Use this for smooth animations and interpolation in `draw()` systems.
    /// Use the fixed `dt` parameter in `update()` for physics and game logic.
    float delta_time() const;

    /// @brief Seconds elapsed since the renderer was created.
    float elapsed_time() const;

    /// @brief Total number of frames rendered since creation.
    uint64_t frame_count() const;

    uint32_t virtual_width()  const;  ///< Virtual framebuffer width in pixels.
    uint32_t virtual_height() const;  ///< Virtual framebuffer height in pixels.

    /// @brief Convert a screen-space position to virtual pixel coordinates.
    ///
    /// Mouse event positions are in screen (physical) pixels. Use this to map
    /// them into virtual pixel space for tile-picking and UI hit-testing.
    ///
    /// @code
    /// for (const Event& e : events) {
    ///     if (e.type == EventType::MousePress) {
    ///         Vec2 vpos = renderer.screen_to_virtual(e.mouse_button().position);
    ///         int tile_x = (int)vpos.x / TILE_SIZE;
    ///         int tile_y = (int)vpos.y / TILE_SIZE;
    ///         handle_tile_click({tile_x, tile_y});
    ///     }
    /// }
    /// @endcode
    Vec2 screen_to_virtual(Vec2 screen_pos) const;

    /// @brief Access the Vulkan context for texture and spritesheet creation.
    ///
    /// Pass this to `Texture::load()`, `SpriteSheet::load()`, `Font::load()`,
    /// and `AssetManager::create()`.
    vk::Context& context();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Renderer() = default;
};

} // namespace xebble
