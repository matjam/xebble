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
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;

namespace xebble {

// ---------------------------------------------------------------------------
// DisplayMode
//
// A native hardware display mode expressed in physical pixels.
// Enumerated by Window::available_display_modes() using platform-specific
// APIs (CoreGraphics on macOS, EnumDisplaySettingsEx on Windows,
// XRandR / wl_output on Linux).
// ---------------------------------------------------------------------------

/// @brief A native hardware display mode expressed in physical pixels.
///
/// Use `Window::available_display_modes()` to enumerate available modes and
/// `Window::set_display_mode()` to switch at runtime.
///
/// On macOS pixel dimensions are obtained from `CGDisplayModeGetPixelWidth/Height()`
/// so they always reflect the true hardware pixel count with no HiDPI scaling.
struct DisplayMode {
    uint32_t    pixel_width;   ///< Native pixel width.
    uint32_t    pixel_height;  ///< Native pixel height.
    std::string label;         ///< Human-readable, e.g. "1920x1080".
};

// ---------------------------------------------------------------------------
// WindowConfig
// ---------------------------------------------------------------------------

/// @brief Configuration passed to `Window::create()`.
///
/// @code
/// // Traditional screen-coordinate window:
/// WindowConfig cfg;
/// cfg.title  = "Dungeon Explorer";
/// cfg.width  = 1280;
/// cfg.height = 720;
/// @endcode
///
/// A native-pixel display mode can be selected for a 1:1 framebuffer with
/// no HiDPI scaling (macOS: disables `GLFW_COCOA_RETINA_FRAMEBUFFER`):
///
/// @code
/// auto modes = Window::available_display_modes();
/// WindowConfig cfg;
/// cfg.title        = "Dungeon Explorer";
/// cfg.display_mode = modes[0];
/// @endcode
struct WindowConfig {
    std::string title      = "Xebble";  ///< Title bar text.
    uint32_t    width      = 1280;      ///< Initial width in screen coordinates (ignored when display_mode is set).
    uint32_t    height     = 720;       ///< Initial height in screen coordinates (ignored when display_mode is set).
    bool        resizable  = true;      ///< Whether the user can resize the window.
    bool        fullscreen = false;     ///< Start in fullscreen mode.

    /// @brief Optional native-pixel display mode.
    ///
    /// When set the window is sized to exactly these pixel dimensions with
    /// platform HiDPI scaling disabled, giving `framebuffer_size() == pixel_width x pixel_height`.
    /// `width` and `height` are ignored when this is populated.
    std::optional<DisplayMode> display_mode;
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
    /// @brief Enumerate all native display modes for the primary monitor.
    ///
    /// Uses platform-specific APIs to obtain true hardware pixel dimensions:
    /// - macOS: `CGDisplayCopyAllDisplayModes()` / `CGDisplayModeGetPixelWidth/Height()`
    /// - Windows: `EnumDisplaySettingsEx()` (not yet implemented)
    /// - Linux: XRandR / wl_output (not yet implemented)
    ///
    /// Modes are returned in descending order of pixel area (largest first).
    ///
    /// @code
    /// auto modes = Window::available_display_modes();
    /// for (auto& m : modes)
    ///     std::cout << m.label << '\n';
    /// @endcode
    static std::vector<DisplayMode> available_display_modes();

    /// @brief Create a new window and initialise GLFW if needed.
    ///
    /// @param config  Window settings.
    /// @return The window, or an `Error` if GLFW initialisation or window
    ///         creation failed (e.g. no display available).
    static std::expected<Window, Error> create(const WindowConfig& config);

    ~Window();
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /// @brief Return true if the OS or user has requested the window to close.
    bool should_close() const;

    /// @brief Collect all pending OS events into the internal queue.
    ///
    /// Must be called once per frame before reading `events()`.
    void poll_events();

    /// @brief Span of events collected during the most recent `poll_events()`.
    ///
    /// The span is invalidated by the next call to `poll_events()`.
    std::span<const Event> events() const;

    /// @brief HiDPI content scale (e.g. 2.0 on Retina displays).
    ///
    /// Returns 1.0 when a `DisplayMode` was specified in `WindowConfig`
    /// (platform HiDPI scaling is disabled in that case).
    float content_scale() const;

    /// @brief Framebuffer size in physical pixels.
    ///
    /// When a `DisplayMode` is active this equals the mode's pixel dimensions
    /// directly (no HiDPI multiplication).
    std::pair<uint32_t, uint32_t> framebuffer_size() const;

    /// @brief Window size in screen coordinates (independent of HiDPI).
    std::pair<uint32_t, uint32_t> window_size() const;

    /// @brief Switch to a different native-pixel display mode at runtime.
    ///
    /// Resizes the window so its framebuffer matches the requested pixel
    /// dimensions exactly. The renderer's swapchain will be recreated
    /// automatically on the next frame via the resulting resize event.
    ///
    /// @code
    /// auto modes = Window::available_display_modes();
    /// window.set_display_mode(modes[1]);
    /// @endcode
    void set_display_mode(const DisplayMode& mode);

    /// @brief Underlying `GLFWwindow*` for Vulkan surface creation.
    GLFWwindow* native_handle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Window() = default;
};

} // namespace xebble
