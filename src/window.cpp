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

#include <atomic>
#include <cstdlib>
#include <filesystem>
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
#elif defined(_WIN32)
std::vector<DisplayMode> windows_available_display_modes();
void windows_set_window_display_mode(GLFWwindow* window, const DisplayMode& mode);
#elif defined(__linux__)
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

    ~Impl() {
        if (window) {
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
        double xpos, ypos;
        glfwGetCursorPos(w, &xpos, &ypos);
        auto btn = static_cast<MouseButton>(button);
        auto m = glfw_mods(mods);
        Vec2 pos{static_cast<float>(xpos), static_cast<float>(ypos)};
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
        if (focused)
            impl->event_queue.push_back(Event::window_focus_gained());
        else
            impl->event_queue.push_back(Event::window_focus_lost());
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
#elif defined(_WIN32)
    return windows_available_display_modes();
#elif defined(__linux__)
    return linux_available_display_modes();
#else
    return {};
#endif
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
        if (!glfwInit()) {
            g_glfw_ref_count--;
            return std::unexpected(Error{"Failed to initialize GLFW"});
        }
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

    int win_w = static_cast<int>(config.width);
    int win_h = static_cast<int>(config.height);
    bool retina_disabled = false;

#ifdef __APPLE__
    if (config.display_mode.has_value()) {
        // Disable Retina/HiDPI so framebuffer pixels == window pixels 1:1.
        // glfwCreateWindow will then treat the dimensions as physical pixels.
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
        win_w = static_cast<int>(config.display_mode->pixel_width);
        win_h = static_cast<int>(config.display_mode->pixel_height);
        retina_disabled = true;
        log(LogLevel::Info, "Native-pixel display mode: " + config.display_mode->label);
    }
#endif

    GLFWmonitor* monitor = config.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    GLFWwindow* glfw_window =
        glfwCreateWindow(win_w, win_h, config.title.c_str(), monitor, nullptr);

    if (!glfw_window) {
        if (--g_glfw_ref_count == 0)
            glfwTerminate();
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
    return glfwWindowShouldClose(impl_->window);
}

void Window::poll_events() {
    impl_->event_queue.clear();
    glfwPollEvents();
}

std::span<const Event> Window::events() const {
    return impl_->event_queue;
}

float Window::content_scale() const {
    if (impl_->retina_disabled)
        return 1.0f;
    float xscale, yscale;
    glfwGetWindowContentScale(impl_->window, &xscale, &yscale);
    return xscale;
}

std::pair<uint32_t, uint32_t> Window::framebuffer_size() const {
    int w, h;
    glfwGetFramebufferSize(impl_->window, &w, &h);
    return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
}

std::pair<uint32_t, uint32_t> Window::window_size() const {
    int w, h;
    glfwGetWindowSize(impl_->window, &w, &h);
    return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
}

void Window::set_display_mode(const DisplayMode& mode) {
#ifdef __APPLE__
    macos_set_window_display_mode(impl_->window, mode);
#elif defined(_WIN32)
    windows_set_window_display_mode(impl_->window, mode);
#elif defined(__linux__)
    linux_set_window_display_mode(impl_->window, mode);
#else
    (void)mode;
#endif
}

GLFWwindow* Window::native_handle() const {
    return impl_->window;
}

} // namespace xebble
