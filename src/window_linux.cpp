/// @file window_linux.cpp
/// @brief Linux-specific window helpers — display mode enumeration and
///        native-pixel window sizing via XRandR / Wayland protocols.
///
/// This file is compiled only on Linux targets (see src/CMakeLists.txt).
/// All public symbols are prefixed `linux_` to make the platform origin
/// clear at call sites in window.cpp.
///
/// @note NOT YET IMPLEMENTED. This stub exists to mark the intended structure.
///       Implement using XRandR (libXrandr) or the wl_output / xdg-output
///       Wayland protocol for mode enumeration, and glfwSetWindowSize() for
///       resizing (pixels == screen coordinates on Linux with no HiDPI scaling
///       applied by the compositor by default).

#ifndef __linux__
#error "window_linux.cpp must only be compiled on Linux"
#endif

#include <xebble/log.hpp>
#include <xebble/window.hpp>

struct GLFWwindow;

namespace xebble {

std::vector<DisplayMode> linux_available_display_modes() {
    // TODO: implement using XRandR (XRRGetScreenResources / XRRConfigSizes)
    //       or wl_output listener for Wayland.
    log(LogLevel::Warn, "linux_available_display_modes: not yet implemented");
    return {};
}

void linux_set_window_display_mode(GLFWwindow* /*window*/, const DisplayMode& /*mode*/) {
    // TODO: implement using glfwSetWindowSize(); on most Linux setups pixels
    //       already equal screen coordinates, but verify under HiDPI compositors.
    log(LogLevel::Warn, "linux_set_window_display_mode: not yet implemented");
}

} // namespace xebble
