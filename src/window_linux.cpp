/// @file window_linux.cpp
/// @brief Linux-specific window helpers — display mode enumeration and
///        window resizing for both X11 and Wayland backends.
///
/// This file is compiled only on Linux targets (see src/CMakeLists.txt).
/// All public symbols are prefixed `linux_` to make the platform origin
/// clear at call sites in window.cpp.
///
/// Display mode enumeration delegates entirely to GLFW's cross-backend
/// glfwGetVideoModes() / glfwGetPrimaryMonitor(), which is implemented for
/// both the X11 (XRandR) and Wayland (wl_output) backends in GLFW 3.4.
/// No direct XRandR or wayland-client calls are required.
///
/// Window resizing uses glfwSetWindowSize().  On Linux (both X11 and Wayland)
/// framebuffer pixels == window screen coordinates at 1x scaling; at higher
/// fractional scaling the compositor handles the upscale transparently, so
/// we pass physical pixel dimensions directly.

#ifndef __linux__
#error "window_linux.cpp must only be compiled on Linux"
#endif

#define GLFW_INCLUDE_VULKAN
#include <xebble/log.hpp>
#include <xebble/window.hpp>

#include <GLFW/glfw3.h>

#include <string>

namespace xebble {

// ---------------------------------------------------------------------------
// linux_available_display_modes
// ---------------------------------------------------------------------------

std::vector<DisplayMode> linux_available_display_modes() {
    // Ensure GLFW is initialised before querying monitors.
    // (Window::create() calls glfwInit() before any platform function, so
    // this is always satisfied when called from Window::available_display_modes()
    // after at least one Window has been created.  For a pre-window call the
    // caller receives an empty list, matching the documented fallback.)
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) {
        log(LogLevel::Warn, "linux_available_display_modes: no primary monitor reported by GLFW");
        return {};
    }

    int count = 0;
    const GLFWvidmode* modes = glfwGetVideoModes(monitor, &count);
    if (!modes || count == 0) {
        log(LogLevel::Warn, "linux_available_display_modes: glfwGetVideoModes returned no modes");
        return {};
    }

    std::vector<DisplayMode> result;
    result.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i) {
        const GLFWvidmode& m = modes[i];
        // Build a human-readable label: "<width>x<height>@<hz>Hz"
        std::string label = std::to_string(m.width) + "x" + std::to_string(m.height) + "@" +
                            std::to_string(m.refreshRate) + "Hz";
        result.push_back(DisplayMode{
            .pixel_width = static_cast<uint32_t>(m.width),
            .pixel_height = static_cast<uint32_t>(m.height),
            .label = std::move(label),
        });
    }

    return result;
}

// ---------------------------------------------------------------------------
// linux_set_window_display_mode
// ---------------------------------------------------------------------------

void linux_set_window_display_mode(GLFWwindow* window, const DisplayMode& mode) {
    if (!window) {
        log(LogLevel::Warn, "linux_set_window_display_mode: null window handle");
        return;
    }
    glfwSetWindowSize(window, static_cast<int>(mode.pixel_width),
                      static_cast<int>(mode.pixel_height));
    log(LogLevel::Info, "linux_set_window_display_mode: resized to " + mode.label);
}

} // namespace xebble
