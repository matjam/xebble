/// @file window_linux.cpp
/// @brief Linux-specific window helpers — display mode enumeration and
///        window resizing for both X11 and Wayland backends.
///
/// This file is compiled only on Linux targets (see src/CMakeLists.txt).
/// All public symbols are prefixed `linux_` to make the platform origin
/// clear at call sites in window.cpp.
///
/// Display mode enumeration delegates entirely to GLFW's cross-backend
/// glfwGetVideoModes() / glfwGetMonitors(), which is implemented for
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

#include <format>
#include <string>

namespace xebble {

// ---------------------------------------------------------------------------
// linux_monitors
// ---------------------------------------------------------------------------

std::vector<MonitorInfo> linux_monitors() { // NOLINT(misc-use-internal-linkage)
    int count = 0;
    GLFWmonitor* const* glfw_monitors = glfwGetMonitors(&count);
    if (glfw_monitors == nullptr || count == 0) {
        log(LogLevel::Warn, "linux_monitors: no monitors reported by GLFW");
        return {};
    }

    const GLFWmonitor* const primary = glfwGetPrimaryMonitor();

    std::vector<MonitorInfo> result;
    result.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i) {
        GLFWmonitor* mon = glfw_monitors[i];
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        if (mode == nullptr) {
            continue;
        }

        const char* name_cstr = glfwGetMonitorName(mon);
        std::string name = (name_cstr != nullptr) ? name_cstr : std::format("Monitor {}", i);
        const bool is_primary = (mon == primary);

        std::string label = std::format("{}  {}x{}{}", name, mode->width, mode->height,
                                        is_primary ? "  (primary)" : "");

        result.push_back(MonitorInfo{
            .name = std::move(name),
            .native_width = static_cast<uint32_t>(mode->width),
            .native_height = static_cast<uint32_t>(mode->height),
            .is_primary = is_primary,
            .label = std::move(label),
        });
    }

    // Sort: primary first, then by pixel area descending.
    // NOLINTNEXTLINE(modernize-use-ranges) -- std::ranges::stable_sort not in libstdc++15
    std::stable_sort(result.begin(), result.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.is_primary != b.is_primary) {
            return a.is_primary;
        }
        return (a.native_width * a.native_height) > (b.native_width * b.native_height);
    });

    return result;
}

// ---------------------------------------------------------------------------
// linux_available_display_modes  (primary monitor only — legacy helper)
// ---------------------------------------------------------------------------

std::vector<DisplayMode> linux_available_display_modes() { // NOLINT(misc-use-internal-linkage)
    // Ensure GLFW is initialised before querying monitors.
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr) {
        log(LogLevel::Warn, "linux_available_display_modes: no primary monitor reported by GLFW");
        return {};
    }

    int count = 0;
    const GLFWvidmode* modes = glfwGetVideoModes(monitor, &count);
    if (modes == nullptr || count == 0) {
        log(LogLevel::Warn, "linux_available_display_modes: glfwGetVideoModes returned no modes");
        return {};
    }

    std::vector<DisplayMode> result;
    result.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i) {
        const GLFWvidmode& m = modes[i];
        std::string label = std::format("{}x{}@{}Hz", m.width, m.height, m.refreshRate);
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

// NOLINTNEXTLINE(misc-use-internal-linkage) -- declared extern in window.cpp
void linux_set_window_display_mode(GLFWwindow* window, const DisplayMode& mode) {
    if (window == nullptr) {
        log(LogLevel::Warn, "linux_set_window_display_mode: null window handle");
        return;
    }
    glfwSetWindowSize(window, static_cast<int>(mode.pixel_width),
                      static_cast<int>(mode.pixel_height));
    log(LogLevel::Info, "linux_set_window_display_mode: resized to " + mode.label);
}

} // namespace xebble
