/// @file window.cpp
/// @brief Platform-agnostic GLFW window implementation.
///
/// macOS-specific behaviour (CoreGraphics display enumeration, Retina hint,
/// native-pixel resizing) is forwarded to functions defined in
/// window_macos.cpp and declared here under #ifdef __APPLE__.

#include <xebble/log.hpp>
#include <xebble/window.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <numeric>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace xebble {

// ---------------------------------------------------------------------------
// Platform-specific forward declarations.
// Each platform's implementation file provides these two functions.
// ---------------------------------------------------------------------------

#ifdef __APPLE__
std::vector<DisplayMode> macos_available_display_modes();
void macos_set_window_display_mode(GLFWwindow* window, const DisplayMode& mode);
#elifdef _WIN32
std::vector<DisplayMode> windows_available_display_modes();
void windows_set_window_display_mode(GLFWwindow* window, const DisplayMode& mode);
#elifdef __linux__
std::vector<DisplayMode> linux_available_display_modes();
void linux_set_window_display_mode(GLFWwindow* window, const DisplayMode& mode);
#endif

namespace {
std::atomic<int> g_glfw_ref_count{0};

Modifiers glfw_mods(int mods) {
    return {
        .shift = (mods & GLFW_MOD_SHIFT) != 0,
        .ctrl = (mods & GLFW_MOD_CONTROL) != 0,
        .alt = (mods & GLFW_MOD_ALT) != 0,
        .super = (mods & GLFW_MOD_SUPER) != 0,
    };
}
} // namespace

struct Window::Impl {
    GLFWwindow* window = nullptr;
    std::vector<Event> event_queue;
    /// True when the window was created with platform HiDPI scaling disabled
    /// (e.g. GLFW_COCOA_RETINA_FRAMEBUFFER=false on macOS), giving a 1:1
    /// framebuffer-to-pixel mapping.
    bool retina_disabled = false;

    // Saved windowed geometry so we can restore it when leaving fullscreen.
    int windowed_x = 100;
    int windowed_y = 100;
    int windowed_w = 1280;
    int windowed_h = 720;
    bool is_fullscreen = false;

    ~Impl() {
        if (window != nullptr) {
            glfwDestroyWindow(window);
            window = nullptr;
        }
        if (--g_glfw_ref_count == 0) {
            glfwTerminate();
        }
    }

    static void key_callback(GLFWwindow* w, int key, int /*scancode*/, int action, int mods) {
        auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        auto k = static_cast<Key>(key);
        auto m = glfw_mods(mods);
        switch (action) {
        case GLFW_PRESS:
            impl->event_queue.push_back(Event::key_press(k, m));
            break;
        case GLFW_RELEASE:
            impl->event_queue.push_back(Event::key_release(k, m));
            break;
        case GLFW_REPEAT:
            impl->event_queue.push_back(Event::key_repeat(k, m));
            break;
        default:
            break;
        }
    }

    static void mouse_button_callback(GLFWwindow* w, int button, int action, int mods) {
        auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        double xpos = 0.0;
        double ypos = 0.0;
        glfwGetCursorPos(w, &xpos, &ypos);
        auto btn = static_cast<MouseButton>(button);
        auto m = glfw_mods(mods);
        const Vec2 pos{static_cast<float>(xpos), static_cast<float>(ypos)};
        switch (action) {
        case GLFW_PRESS:
            impl->event_queue.push_back(Event::mouse_press(btn, m, pos));
            break;
        case GLFW_RELEASE:
            impl->event_queue.push_back(Event::mouse_release(btn, m, pos));
            break;
        default:
            break;
        }
    }

    static void cursor_pos_callback(GLFWwindow* w, double xpos, double ypos) {
        auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        impl->event_queue.push_back(
            Event::mouse_move({static_cast<float>(xpos), static_cast<float>(ypos)}));
    }

    static void scroll_callback(GLFWwindow* w, double xoffset, double yoffset) {
        auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        impl->event_queue.push_back(
            Event::mouse_scroll(static_cast<float>(xoffset), static_cast<float>(yoffset)));
    }

    static void framebuffer_size_callback(GLFWwindow* w, int width, int height) {
        auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        impl->event_queue.push_back(
            Event::window_resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height)));
    }

    static void focus_callback(GLFWwindow* w, int focused) {
        auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        if (focused != 0) {
            impl->event_queue.push_back(Event::window_focus_gained());
        } else {
            impl->event_queue.push_back(Event::window_focus_lost());
        }
    }

    static void close_callback(GLFWwindow* w) {
        auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        impl->event_queue.push_back(Event::window_close());
    }
};

// ---------------------------------------------------------------------------
// Window::available_display_modes()
// ---------------------------------------------------------------------------

std::vector<DisplayMode> Window::available_display_modes() {
#ifdef __APPLE__
    return macos_available_display_modes();
#elifdef _WIN32
    return windows_available_display_modes();
#elifdef __linux__
    return linux_available_display_modes();
#else
    return {};
#endif
}

// ---------------------------------------------------------------------------
// Window::available_resolutions()
// ---------------------------------------------------------------------------

namespace {

struct CommonRes {
    uint32_t w;
    uint32_t h;
    const char* name;
};

// clang-format off
constexpr auto k_common_resolutions = std::to_array<CommonRes>({
    // 16:9
    {7680, 4320, "8K UHD"},
    {5120, 2880, "5K"},
    {3840, 2160, "4K UHD"},
    {2560, 1440, "1440p QHD"},
    {1920, 1080, "1080p FHD"},
    {1280,  720, "720p HD"},
    {854,   480, "480p"},
    {640,   360, "360p"},
    // 21:9 ultrawide
    {5120, 2160, "5K ultrawide"},
    {3440, 1440, "1440p ultrawide"},
    {2560, 1080, "1080p ultrawide"},
    // 4:3
    {1600, 1200, "UXGA"},
    {1024,  768, "XGA"},
    {800,   600, "SVGA"},
    {640,   480, "VGA"},
    // 16:10
    {2560, 1600, "WQXGA"},
    {1920, 1200, "WUXGA"},
    {1280,  800, "WXGA"},
});
// clang-format on

// Return the integer scale factor if (vw,vh) is pixel-perfect on native
// (nw,nh) under Fit or Crop, or 0 if it is not.
//
// Fit:  scale = min(nw/vw, nh/vh).  Pixel-perfect when both axes divide at
//       that same integer: nw%vw==0 && nh%vh==0 && nw/vw == nh/vh.
// Crop: scale = max(nw/vw, nh/vh).  Same divisibility requirement but using
//       the larger ratio (one axis will be cropped).
uint32_t pixel_perfect_scale(uint32_t vw, uint32_t vh, uint32_t nw, uint32_t nh, ScaleMode mode) {
    if (vw == 0 || vh == 0 || nw < vw || nh < vh) {
        return 0;
    }
    if (nw % vw != 0 || nh % vh != 0) {
        return 0;
    }
    const uint32_t sx = nw / vw;
    const uint32_t sy = nh / vh;
    if (mode == ScaleMode::Fit) {
        return (sx == sy) ? sx : 0;
    }
    // Crop: the larger scale is used; both axes must still divide cleanly.
    return (sx == sy) ? sx : 0;
}

// Return the best (largest) pixel-perfect scale across all natives, or 0.
uint32_t best_pixel_perfect_scale(uint32_t vw, uint32_t vh,
                                  const std::vector<std::pair<uint32_t, uint32_t>>& natives,
                                  ScaleMode mode) {
    uint32_t best = 0;
    for (const auto& [nw, nh] : natives) {
        best = std::max(best, pixel_perfect_scale(vw, vh, nw, nh, mode));
    }
    return best;
}

std::string aspect_ratio_label(uint32_t w, uint32_t h) {
    const uint32_t g = std::gcd(w, h);
    return std::format("{}:{}", w / g, h / g);
}

bool already_in(const std::vector<ResolutionInfo>& v, uint32_t w, uint32_t h) {
    return std::ranges::any_of(
        v, [w, h](const ResolutionInfo& r) { return r.width == w && r.height == h; });
}

} // namespace

std::vector<ResolutionInfo> Window::available_resolutions([[maybe_unused]] uint32_t virtual_width,
                                                          [[maybe_unused]] uint32_t virtual_height,
                                                          ScaleMode scale_mode) {
    // Collect unique native (w, h) pairs from display modes.
    const auto modes = available_display_modes();
    std::vector<std::pair<uint32_t, uint32_t>> natives;
    for (const auto& m : modes) {
        auto entry = std::make_pair(m.pixel_width, m.pixel_height);
        if (std::ranges::find(natives, entry) == natives.end()) {
            natives.push_back(entry);
        }
    }

    // -----------------------------------------------------------------------
    // Phase 1: exact integer-divisor sub-resolutions of each native display.
    // These are always pixel-perfect (scale == divisor).
    // -----------------------------------------------------------------------
    std::vector<ResolutionInfo> pp_entries;
    for (const auto& [nw, nh] : natives) {
        for (uint32_t d = 1; d <= 4; ++d) {
            if (nw % d != 0 || nh % d != 0) {
                continue;
            }
            const uint32_t w = nw / d;
            const uint32_t h = nh / d;
            if (w < 320 || h < 270) {
                continue;
            }
            if (already_in(pp_entries, w, h)) {
                continue;
            }
            const uint32_t scale = best_pixel_perfect_scale(w, h, natives, scale_mode);
            const auto ar = aspect_ratio_label(w, h);
            std::string lbl;
            if (d == 1) {
                lbl = std::format("{}x{}  (native, {})", w, h, ar);
            } else {
                lbl = std::format("{}x{}  (pixel perfect x{}, {})", w, h, scale, ar);
            }
            pp_entries.push_back({w, h, scale > 0, scale, std::move(lbl)});
        }
    }
    std::ranges::sort(pp_entries, [](const ResolutionInfo& a, const ResolutionInfo& b) {
        return (a.width * a.height) > (b.width * b.height);
    });

    // -----------------------------------------------------------------------
    // Phase 2: curated common resolutions not already covered by phase 1.
    // Capped at the largest native area.
    // -----------------------------------------------------------------------
    uint32_t max_area = 0;
    for (const auto& [nw, nh] : natives) {
        max_area = std::max(max_area, nw * nh);
    }

    std::vector<ResolutionInfo> common_entries;
    for (const auto& cr : k_common_resolutions) {
        if (cr.w * cr.h > max_area) {
            continue;
        }
        if (already_in(pp_entries, cr.w, cr.h) || already_in(common_entries, cr.w, cr.h)) {
            continue;
        }
        const uint32_t scale = best_pixel_perfect_scale(cr.w, cr.h, natives, scale_mode);
        const bool pp = scale > 0;
        const auto ar = aspect_ratio_label(cr.w, cr.h);
        std::string lbl;
        if (pp) {
            lbl = std::format("{}x{}  ({}, pixel perfect x{}, {})", cr.w, cr.h, cr.name, scale, ar);
        } else {
            lbl = std::format("{}x{}  ({}, {})", cr.w, cr.h, cr.name, ar);
        }
        common_entries.push_back({cr.w, cr.h, pp, scale, std::move(lbl)});
    }
    std::ranges::sort(common_entries, [](const ResolutionInfo& a, const ResolutionInfo& b) {
        return (a.width * a.height) > (b.width * b.height);
    });

    // Merge: pixel-perfect first, then common.
    std::vector<ResolutionInfo> result;
    result.insert(result.end(), pp_entries.begin(), pp_entries.end());
    result.insert(result.end(), common_entries.begin(), common_entries.end());
    return result;
}

// ---------------------------------------------------------------------------
// Window::create()
// ---------------------------------------------------------------------------

std::expected<Window, Error> Window::create(const WindowConfig& config) {
    if (g_glfw_ref_count++ == 0) {
#ifdef __APPLE__
        // Locate and register the MoltenVK ICD before glfwInit() so the
        // Vulkan loader can find the driver.
        if (!std::getenv("VK_DRIVER_FILES") && !std::getenv("VK_ICD_FILENAMES")) {
            std::vector<std::string> icd_paths;

            char exe_buf[4096];
            uint32_t exe_size = sizeof(exe_buf);
            if (_NSGetExecutablePath(exe_buf, &exe_size) == 0) {
                auto exe_dir = std::filesystem::path(exe_buf).parent_path();
                icd_paths.push_back(
                    (exe_dir / "../Resources/vulkan/icd.d/MoltenVK_icd.json").string());
            }
#ifdef XEBBLE_MOLTENVK_ICD
            icd_paths.push_back(XEBBLE_MOLTENVK_ICD);
#endif
            for (auto& path : icd_paths) {
                if (std::filesystem::exists(path)) {
                    auto canonical = std::filesystem::canonical(path).string();
                    setenv("VK_DRIVER_FILES", canonical.c_str(), 0);
                    log(LogLevel::Info, "MoltenVK ICD: " + canonical);
                    break;
                }
            }
        }
#endif
        if (glfwInit() == 0) {
            g_glfw_ref_count--;
            return std::unexpected(Error{"Failed to initialize GLFW"});
        }
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

#ifdef __APPLE__
    int win_w = static_cast<int>(config.width);
    int win_h = static_cast<int>(config.height);
    bool retina_disabled = false;
    if (config.display_mode.has_value()) {
        // Disable Retina/HiDPI so framebuffer pixels == window pixels 1:1.
        // glfwCreateWindow will then treat the dimensions as physical pixels.
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
        win_w = static_cast<int>(config.display_mode->pixel_width);
        win_h = static_cast<int>(config.display_mode->pixel_height);
        retina_disabled = true;
        log(LogLevel::Info, "Native-pixel display mode: " + config.display_mode->label);
    }
#else
    const int win_w = static_cast<int>(config.width);
    const int win_h = static_cast<int>(config.height);
    const bool retina_disabled = false;
#endif

    GLFWmonitor* monitor = config.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    GLFWwindow* glfw_window =
        glfwCreateWindow(win_w, win_h, config.title.c_str(), monitor, nullptr);

    if (glfw_window == nullptr) {
        if (--g_glfw_ref_count == 0) {
            glfwTerminate();
        }
        return std::unexpected(Error{"Failed to create GLFW window"});
    }

    Window window;
    window.impl_ = std::make_unique<Impl>();
    window.impl_->window = glfw_window;
    window.impl_->retina_disabled = retina_disabled;

    glfwSetWindowUserPointer(glfw_window, window.impl_.get());
    glfwSetKeyCallback(glfw_window, Impl::key_callback);
    glfwSetMouseButtonCallback(glfw_window, Impl::mouse_button_callback);
    glfwSetCursorPosCallback(glfw_window, Impl::cursor_pos_callback);
    glfwSetScrollCallback(glfw_window, Impl::scroll_callback);
    glfwSetFramebufferSizeCallback(glfw_window, Impl::framebuffer_size_callback);
    glfwSetWindowFocusCallback(glfw_window, Impl::focus_callback);
    glfwSetWindowCloseCallback(glfw_window, Impl::close_callback);

    log(LogLevel::Info, "Window created: " + config.title);
    return window;
}

Window::~Window() = default;
Window::Window(Window&& other) noexcept = default;
Window& Window::operator=(Window&& other) noexcept = default;

bool Window::should_close() const {
    return glfwWindowShouldClose(impl_->window) != 0;
}

void Window::poll_events() {
    impl_->event_queue.clear();
    glfwPollEvents();
}

std::span<const Event> Window::events() const {
    return impl_->event_queue;
}

float Window::content_scale() const {
    if (impl_->retina_disabled) {
        return 1.0f;
    }
    float xscale = 0.0F;
    float yscale = 0.0F;
    glfwGetWindowContentScale(impl_->window, &xscale, &yscale);
    return xscale;
}

std::pair<uint32_t, uint32_t> Window::framebuffer_size() const {
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(impl_->window, &w, &h);
    return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
}

std::pair<uint32_t, uint32_t> Window::window_size() const {
    int w = 0;
    int h = 0;
    glfwGetWindowSize(impl_->window, &w, &h);
    return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
}

void Window::set_display_mode(const DisplayMode& mode) {
#ifdef __APPLE__
    macos_set_window_display_mode(impl_->window, mode);
#elifdef _WIN32
    windows_set_window_display_mode(impl_->window, mode);
#elifdef __linux__
    linux_set_window_display_mode(impl_->window, mode);
#else
    (void)mode;
#endif
}

GLFWwindow* Window::native_handle() const {
    return impl_->window;
}

void Window::set_fullscreen(bool fullscreen) {
    if (impl_->is_fullscreen == fullscreen) {
        return;
    }

    if (fullscreen) {
        // Save current windowed geometry before switching.
        glfwGetWindowPos(impl_->window, &impl_->windowed_x, &impl_->windowed_y);
        glfwGetWindowSize(impl_->window, &impl_->windowed_w, &impl_->windowed_h);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(impl_->window, monitor, 0, 0, mode->width, mode->height,
                             mode->refreshRate);
        log(LogLevel::Info, "Window: entered fullscreen");
    } else {
        glfwSetWindowMonitor(impl_->window, nullptr, impl_->windowed_x, impl_->windowed_y,
                             impl_->windowed_w, impl_->windowed_h, GLFW_DONT_CARE);
        log(LogLevel::Info, "Window: returned to windowed");
    }
    impl_->is_fullscreen = fullscreen;
}

} // namespace xebble
