/// @file window_windows.cpp
/// @brief Windows-specific window helpers — display mode enumeration and
///        native-pixel window sizing via Win32/DXGI.
///
/// This file is compiled only on Windows targets (see src/CMakeLists.txt).
/// All public symbols are prefixed `windows_` to make the platform origin
/// clear at call sites in window.cpp.
///
/// @note NOT YET IMPLEMENTED. This stub exists to mark the intended structure.
///       Implement using EnumDisplaySettingsEx() (or IDXGIOutput::GetDisplayModeList())
///       for mode enumeration, and glfwSetWindowSize() for resizing after
///       mapping pixel dimensions to screen coordinates via Win32.

#ifndef _WIN32
#error "window_windows.cpp must only be compiled on Windows"
#endif

#include <xebble/log.hpp>
#include <xebble/window.hpp>

struct GLFWwindow;

namespace xebble {

std::vector<DisplayMode> windows_available_display_modes() {
    // TODO: implement using EnumDisplaySettingsEx() or IDXGIOutput::GetDisplayModeList()
    log(LogLevel::Warn, "windows_available_display_modes: not yet implemented");
    return {};
}

void windows_set_window_display_mode(GLFWwindow* /*window*/, const DisplayMode& /*mode*/) {
    // TODO: implement using glfwSetWindowSize() after mapping the requested
    //       pixel dimensions to screen coordinates via GetDeviceCaps()/DPI APIs.
    log(LogLevel::Warn, "windows_set_window_display_mode: not yet implemented");
}

} // namespace xebble
