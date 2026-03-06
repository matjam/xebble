/// @file window.hpp
/// @brief GLFW window wrapper for Xebble.
///
/// Provides a move-only Window class that owns a GLFW window configured for
/// Vulkan (GLFW_NO_API). Input events are collected during poll_events() and
/// exposed as a span of Event objects. GLFW initialization and termination
/// are reference-counted so multiple windows can coexist.
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

/// @brief Configuration for window creation.
struct WindowConfig {
    std::string title = "Xebble";  ///< Window title bar text.
    uint32_t width = 1280;         ///< Initial window width in screen coordinates.
    uint32_t height = 720;         ///< Initial window height in screen coordinates.
    bool resizable = true;         ///< Whether the user can resize the window.
    bool fullscreen = false;       ///< Whether to create a fullscreen window.
};

/// @brief RAII wrapper around a GLFW window with integrated event queue.
///
/// Move-only. Use the static create() factory to construct.
/// Call poll_events() each frame, then iterate events() to process input.
class Window {
public:
    /// @brief Create a new window.
    /// @param config Window configuration.
    /// @return The window, or an Error if GLFW initialization or window creation failed.
    static std::expected<Window, Error> create(const WindowConfig& config);

    ~Window();
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /// @brief Returns true if the window has been asked to close.
    bool should_close() const;

    /// @brief Poll for new input/window events. Clears the previous frame's events.
    void poll_events();

    /// @brief Access events collected during the most recent poll_events() call.
    std::span<const Event> events() const;

    /// @brief Get the HiDPI content scale factor (e.g. 2.0 on Retina displays).
    float content_scale() const;

    /// @brief Get the framebuffer size in pixels (affected by content scale).
    std::pair<uint32_t, uint32_t> framebuffer_size() const;

    /// @brief Get the window size in screen coordinates.
    std::pair<uint32_t, uint32_t> window_size() const;

    /// @brief Access the underlying GLFWwindow pointer for Vulkan surface creation.
    GLFWwindow* native_handle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Window() = default;
};

} // namespace xebble
