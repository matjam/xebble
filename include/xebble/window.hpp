/// @file window.hpp
/// @brief RAII wrapper around a GLFW window configured for Vulkan rendering.
///
/// `Window` is a move-only object that owns a GLFW window handle. It manages
/// GLFW initialization and termination via a reference count, so multiple
/// windows can coexist safely.
///
/// In normal usage you do not create a `Window` directly — `xebble::run()`
/// creates and manages it for you based on `GameConfig::window`. You only need
/// this header if you are building a custom game loop outside of `run()`.
///
/// ## Custom game loop example
///
/// @code
/// #include <xebble/window.hpp>
/// #include <xebble/renderer.hpp>
/// using namespace xebble;
///
/// int main() {
///     auto win = Window::create({
///         .title  = "My Game",
///         .width  = 1280,
///         .height = 720,
///     }).value();
///
///     auto ren = Renderer::create(win, {
///         .virtual_width  = 320,
///         .virtual_height = 180,
///     }).value();
///
///     while (!win.should_close()) {
///         win.poll_events();
///         for (const Event& e : win.events()) {
///             if (e.type == EventType::WindowClose) return 0;
///         }
///         if (ren.begin_frame()) {
///             // ... draw calls ...
///             ren.end_frame();
///         }
///     }
///     return 0;
/// }
/// @endcode
#pragma once

#include <xebble/types.hpp>
#include <xebble/event.hpp>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;

namespace xebble {

// ---------------------------------------------------------------------------
// WindowConfig
// ---------------------------------------------------------------------------

/// @brief Configuration passed to `Window::create()`.
///
/// @code
/// WindowConfig cfg;
/// cfg.title      = "Dungeon Explorer";
/// cfg.width      = 1280;
/// cfg.height     = 720;
/// cfg.resizable  = true;   // allow the user to resize
/// cfg.fullscreen = false;  // start windowed
/// @endcode
struct WindowConfig {
    std::string title      = "Xebble";  ///< Title bar text.
    uint32_t    width      = 1280;      ///< Initial width in screen coordinates (not pixels).
    uint32_t    height     = 720;       ///< Initial height in screen coordinates.
    bool        resizable  = true;      ///< Whether the user can resize the window.
    bool        fullscreen = false;     ///< Start in fullscreen mode.
};

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

/// @brief Move-only RAII wrapper around a GLFW Vulkan window.
///
/// Manages the full GLFW window lifecycle. All input events are collected into
/// an internal queue during `poll_events()` and exposed via `events()` as a
/// span that is valid until the next `poll_events()` call.
class Window {
public:
    /// @brief Create a new window and initialise GLFW if needed.
    ///
    /// @param config  Window settings.
    /// @return The window, or an `Error` if GLFW initialisation or window
    ///         creation failed (e.g. no display available).
    ///
    /// @code
    /// auto win = Window::create({.title = "My Game", .width = 1280, .height = 720});
    /// if (!win) {
    ///     std::cerr << "Window creation failed: " << win.error().message << '\n';
    ///     return 1;
    /// }
    /// @endcode
    static std::expected<Window, Error> create(const WindowConfig& config);

    ~Window();
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /// @brief Return true if the OS or user has requested the window to close.
    ///
    /// Check this in your game loop; when true, exit cleanly.
    ///
    /// @code
    /// while (!window.should_close()) {
    ///     window.poll_events();
    ///     // ... update and draw ...
    /// }
    /// @endcode
    bool should_close() const;

    /// @brief Collect all pending OS events into the internal queue.
    ///
    /// Must be called once per frame before reading `events()`. Clears the
    /// previous frame's events first.
    void poll_events();

    /// @brief Span of events collected during the most recent `poll_events()`.
    ///
    /// The span is invalidated by the next call to `poll_events()`.
    ///
    /// @code
    /// window.poll_events();
    /// for (const Event& e : window.events()) {
    ///     // handle e ...
    /// }
    /// @endcode
    std::span<const Event> events() const;

    /// @brief HiDPI content scale (e.g. 2.0 on Retina/HiDPI displays).
    ///
    /// The renderer uses this to size its swapchain correctly. You rarely
    /// need this directly unless writing custom Vulkan code.
    float content_scale() const;

    /// @brief Framebuffer size in physical pixels (affected by content scale).
    ///
    /// On a Retina display a 1280×720 window will have a 2560×1440 framebuffer.
    std::pair<uint32_t, uint32_t> framebuffer_size() const;

    /// @brief Window size in screen coordinates (independent of HiDPI).
    std::pair<uint32_t, uint32_t> window_size() const;

    /// @brief Underlying `GLFWwindow*` for Vulkan surface creation.
    ///
    /// Used internally by `Renderer::create()`. Only needed if you are
    /// creating your own Vulkan surface.
    GLFWwindow* native_handle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Window() = default;
};

} // namespace xebble
