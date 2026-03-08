/// @file window_macos.cpp
/// @brief macOS-specific window helpers — display mode enumeration and
///        native-pixel window sizing via CoreGraphics.
///
/// This file is compiled only on Apple targets (see src/CMakeLists.txt).
/// All public symbols are prefixed `macos_` to make the platform origin clear
/// at call sites in window.cpp.

#include <xebble/window.hpp>
#include <xebble/log.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// CoreGraphics provides CGDisplayCopyAllDisplayModes,
// CGDisplayModeGetPixelWidth/Height, and CGDisplayModeIsUsableForDesktopGUI.
// Its opaque CGDisplayMode type is distinct from xebble::DisplayMode.
#include <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <string>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// macos_available_display_modes
//
// Queries CoreGraphics for every desktop-usable display mode on the main
// display and returns one xebble::DisplayMode per unique pixel resolution,
// sorted largest-first.
// ---------------------------------------------------------------------------

std::vector<DisplayMode> macos_available_display_modes() {
    std::vector<DisplayMode> result;

    CGDirectDisplayID display   = CGMainDisplayID();
    CFArrayRef        mode_list = CGDisplayCopyAllDisplayModes(display, nullptr);
    if (!mode_list) return result;

    CFIndex count = CFArrayGetCount(mode_list);
    for (CFIndex i = 0; i < count; i++) {
        auto* cg_mode = static_cast<CGDisplayModeRef>(
            const_cast<void*>(CFArrayGetValueAtIndex(mode_list, i)));

        // Skip modes not suitable for normal windowed / desktop rendering.
        if (!CGDisplayModeIsUsableForDesktopGUI(cg_mode))
            continue;

        auto pw = static_cast<uint32_t>(CGDisplayModeGetPixelWidth(cg_mode));
        auto ph = static_cast<uint32_t>(CGDisplayModeGetPixelHeight(cg_mode));
        if (pw == 0 || ph == 0) continue;

        // One entry per unique pixel resolution — deduplicate scaled variants.
        bool dup = false;
        for (auto& r : result) {
            if (r.pixel_width == pw && r.pixel_height == ph) { dup = true; break; }
        }
        if (dup) continue;

        result.push_back({pw, ph, std::to_string(pw) + "x" + std::to_string(ph)});
    }
    CFRelease(mode_list);

    // Largest resolution first.
    std::sort(result.begin(), result.end(), [](const DisplayMode& a, const DisplayMode& b) {
        return (a.pixel_width * a.pixel_height) > (b.pixel_width * b.pixel_height);
    });

    return result;
}

// ---------------------------------------------------------------------------
// macos_set_window_display_mode
//
// Resizes an existing GLFW window to exactly the pixel dimensions of `mode`.
// Assumes the window was created with GLFW_COCOA_RETINA_FRAMEBUFFER disabled
// so that screen-coordinate pixels == physical pixels (1:1 mapping).
// ---------------------------------------------------------------------------

void macos_set_window_display_mode(GLFWwindow* window, const DisplayMode& mode) {
    glfwSetWindowSize(window,
        static_cast<int>(mode.pixel_width),
        static_cast<int>(mode.pixel_height));
    log(LogLevel::Info, "Display mode changed: " + mode.label);
}

} // namespace xebble
