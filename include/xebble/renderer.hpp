/// @file renderer.hpp
/// @brief Vulkan-backed 2D renderer with a fixed virtual resolution.
///
/// The Renderer renders to an offscreen framebuffer at the configured
/// **virtual resolution** (e.g. 960×540 — half 1080p), then blits it to the
/// swapchain maintaining the correct aspect ratio. The scaling behaviour is
/// controlled by `ScaleMode`: `Fit` letterboxes/pillarboxes so the entire
/// canvas is always visible; `Crop` fills the window and crops the edges.
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
/// // Configure a 960×540 virtual screen (half 1080p, clean 2× on a 1920×1080 display).
/// RendererConfig cfg;
/// cfg.virtual_width  = 960;
/// cfg.virtual_height = 540;
/// cfg.vsync          = true;
/// cfg.scale_mode     = ScaleMode::Fit; // or ScaleMode::Crop
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

#include <xebble/texture.hpp>
#include <xebble/types.hpp>
#include <xebble/window.hpp>

#include <expected>
#include <memory>
#include <span>
#include <vector>

namespace xebble {

class SpriteSheet;
struct TileMap;
struct TextBlock;

namespace vk {
class Context;
}

// ---------------------------------------------------------------------------
// ScaleMode
// ---------------------------------------------------------------------------

/// @brief How the virtual framebuffer is scaled to fit the window.
///
/// Both modes maintain the exact aspect ratio of the virtual resolution and
/// center the image in the window. They differ only in how they handle windows
/// whose aspect ratio does not match the virtual canvas:
///
/// - **Fit**  — the entire canvas is always visible; empty bars fill the gaps
///              (letterbox or pillarbox). Safe for all content.
/// - **Crop** — the canvas fills the entire window; edges that fall outside
///              the window area are cropped. No bars, but some content may be
///              invisible near the edges.
///
/// For pixel-art games a common approach is to use **Fit** so nothing is ever
/// hidden, and to design the virtual canvas with a 16:9 ratio that matches the
/// majority of modern displays.
enum class ScaleMode {
    Fit,  ///< Letterbox / pillarbox — entire canvas visible, bars on short sides.
    Crop, ///< Fill window — canvas edges cropped on long sides, no bars.
};

// ---------------------------------------------------------------------------
// RendererConfig
// ---------------------------------------------------------------------------

/// @brief Configuration for `Renderer::create()`.
struct RendererConfig {
    uint32_t virtual_width = 960;          ///< Virtual framebuffer width  in pixels.
    uint32_t virtual_height = 540;         ///< Virtual framebuffer height in pixels.
    bool vsync = true;                     ///< Enable vertical sync (prevents tearing).
    bool nearest_sample = false;           ///< Use nearest-neighbour sampling on blit (sharp pixel
                                           ///< edges at integer scales). Default false (bilinear).
    ScaleMode scale_mode = ScaleMode::Fit; ///< How to handle aspect ratio mismatch.
};

/// @brief A named resolution option (for a settings menu, for example).
struct Resolution {
    uint32_t width;
    uint32_t height;
    std::string label; ///< Human-readable name, e.g. "1920×1080".
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
    float pos_x, pos_y;   ///< Top-left position in virtual pixels (before pivot/rotation).
    float uv_x, uv_y;     ///< Top-left UV coordinate (normalised 0–1).
    float uv_w, uv_h;     ///< UV extent (normalised 0–1).
    float quad_w, quad_h; ///< Unscaled quad size in virtual pixels (shader applies scale).
    float r, g, b, a;     ///< Multiplicative colour tint (1,1,1,1 = no tint).
    float scale;          ///< Uniform scale multiplier (1 = native size).
    float rotation;       ///< Rotation in radians, counter-clockwise.
    float pivot_x;        ///< Pivot X in 0–1 quad-local space (0=left, 1=right).
    float pivot_y;        ///< Pivot Y in 0–1 quad-local space (0=top,  1=bottom).
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
    [[nodiscard]] static std::expected<Renderer, Error> create(Window& window,
                                                               const RendererConfig& config);

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
    [[nodiscard]] bool begin_frame();

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
    void submit_instances(std::span<const SpriteInstance> instances, const Texture& texture,
                          float z_order = 0.0f);

    /// @brief Ensure the instance buffer can hold at least @p count instances
    ///        and return a pointer to the mapped memory for direct writes.
    ///
    /// The caller writes `SpriteInstance` data directly into the returned
    /// buffer, then calls `commit_direct_instances()` to record draw batches
    /// and flush the written region.
    ///
    /// This avoids the intermediate staging vector and memcpy used by
    /// `submit_instances()`.
    ///
    /// @param count  Minimum number of SpriteInstance slots required.
    /// @return Pointer to the start of mapped instance memory, or nullptr
    ///         if the buffer could not be grown.
    [[nodiscard]] SpriteInstance* map_instance_buffer(uint32_t count);

    /// @brief Record a draw batch referencing data already written to the
    ///        instance buffer via `map_instance_buffer()`.
    ///
    /// @param texture        Atlas texture to sample from.
    /// @param z_order        Draw order — lower values are drawn first.
    /// @param first_instance Offset (in instances, not bytes) into the buffer.
    /// @param instance_count Number of instances in this batch.
    void record_batch(const Texture& texture, float z_order, uint32_t first_instance,
                      uint32_t instance_count);

    /// @brief Flush the instance buffer after direct writes and mark the
    ///        total number of instances written.
    ///
    /// Must be called after all `record_batch()` calls for this frame.
    /// @param total_instances  Total number of instances written to the buffer.
    void flush_instance_buffer(uint32_t total_instances);

    /// @brief Index of the current frame-in-flight slot (0 or 1).
    ///
    /// Use this to track per-frame-slot state when writing directly to
    /// instance buffers (e.g. knowing which slots need a full rewrite
    /// after a structural rebuild).
    [[nodiscard]] uint32_t current_frame_index() const;

    /// @brief Number of frame-in-flight slots (currently 2).
    [[nodiscard]] static constexpr uint32_t frames_in_flight() { return 2; }

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
    [[nodiscard]] float delta_time() const;

    /// @brief Seconds elapsed since the renderer was created.
    [[nodiscard]] float elapsed_time() const;

    /// @brief Total number of frames rendered since creation.
    [[nodiscard]] uint64_t frame_count() const;

    [[nodiscard]] uint32_t virtual_width() const;  ///< Virtual framebuffer width in pixels.
    [[nodiscard]] uint32_t virtual_height() const; ///< Virtual framebuffer height in pixels.

    /// @brief Change the virtual resolution at runtime.
    ///
    /// The new resolution takes effect at the start of the next `begin_frame()`
    /// call (after GPU idle + offscreen framebuffer recreation). Use this from
    /// a settings menu to let the player choose their preferred virtual canvas.
    ///
    /// @code
    /// renderer.set_virtual_resolution(320, 180);
    /// @endcode
    void set_virtual_resolution(uint32_t width, uint32_t height);

    /// @brief Switch to a native-pixel display mode at runtime.
    ///
    /// Resizes the window via `Window::set_display_mode()` so the framebuffer
    /// matches the requested pixel dimensions, then queues a virtual-resolution
    /// update to 1920×1080 (the default logical canvas). The swapchain is
    /// recreated automatically on the next frame from the resulting resize event.
    ///
    /// @code
    /// auto modes = Window::available_display_modes();
    /// renderer.set_display_mode(modes[0]);
    /// @endcode
    void set_display_mode(const DisplayMode& mode);

    /// @brief Toggle fullscreen mode at runtime.
    ///
    /// Delegates to `Window::set_fullscreen()`. The swapchain is recreated
    /// automatically on the next frame once the window resize event fires.
    ///
    /// @code
    /// renderer.set_fullscreen(true);   // enter fullscreen
    /// renderer.set_fullscreen(false);  // return to windowed
    /// @endcode
    void set_fullscreen(bool fullscreen);

    /// @brief Switch the blit sampler filter at runtime.
    ///
    /// When `nearest` is true the offscreen framebuffer is blitted to the
    /// swapchain with nearest-neighbour filtering — giving hard pixel edges at
    /// any integer scale. When false (the default) bilinear filtering is used
    /// for smooth scaling at non-integer ratios.
    ///
    /// Internally this queues an offscreen framebuffer recreate (which rebakes
    /// the sampler), taking effect at the start of the next frame.
    ///
    /// @code
    /// // Crisp pixel art at a 2x pixel-perfect scale:
    /// renderer.set_nearest_sample(true);
    /// renderer.set_virtual_resolution(960, 540);
    /// @endcode
    void set_nearest_sample(bool nearest);

    /// @brief Notify the renderer that the window framebuffer has been resized.
    ///
    /// Call this immediately after receiving a `EventType::WindowResize` event
    /// (before `begin_frame()`). The swapchain and framebuffers are recreated
    /// to match the new surface dimensions so the compositor does not have to
    /// scale a stale-sized surface (which causes stretching on Wayland).
    void handle_resize();

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
    [[nodiscard]] Vec2 screen_to_virtual(Vec2 screen_pos) const;

    /// @brief Access the Vulkan context for texture and spritesheet creation.
    ///
    /// Pass this to `Texture::load()`, `SpriteSheet::load()`, `Font::load()`,
    /// and `AssetManager::create()`.
    [[nodiscard]] vk::Context& context();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Renderer() = default;
};

} // namespace xebble
