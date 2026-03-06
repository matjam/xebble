#include <xebble/window.hpp>
#include <xebble/log.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <atomic>

namespace xebble {

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
}

struct Window::Impl {
    GLFWwindow* window = nullptr;
    std::vector<Event> event_queue;

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
            case GLFW_PRESS:   impl->event_queue.push_back(Event::key_press(k, m)); break;
            case GLFW_RELEASE: impl->event_queue.push_back(Event::key_release(k, m)); break;
            case GLFW_REPEAT:  impl->event_queue.push_back(Event::key_repeat(k, m)); break;
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
            case GLFW_PRESS:   impl->event_queue.push_back(Event::mouse_press(btn, m, pos)); break;
            case GLFW_RELEASE: impl->event_queue.push_back(Event::mouse_release(btn, m, pos)); break;
        }
    }

    static void cursor_pos_callback(GLFWwindow* w, double xpos, double ypos) {
        auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        impl->event_queue.push_back(Event::mouse_move({static_cast<float>(xpos), static_cast<float>(ypos)}));
    }

    static void scroll_callback(GLFWwindow* w, double xoffset, double yoffset) {
        auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        impl->event_queue.push_back(Event::mouse_scroll(static_cast<float>(xoffset), static_cast<float>(yoffset)));
    }

    static void framebuffer_size_callback(GLFWwindow* w, int width, int height) {
        auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        impl->event_queue.push_back(Event::window_resize(
            static_cast<uint32_t>(width), static_cast<uint32_t>(height)));
    }

    static void focus_callback(GLFWwindow* w, int focused) {
        auto* impl = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        if (focused) {
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

std::expected<Window, Error> Window::create(const WindowConfig& config) {
    if (g_glfw_ref_count++ == 0) {
        if (!glfwInit()) {
            g_glfw_ref_count--;
            return std::unexpected(Error{"Failed to initialize GLFW"});
        }
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

    GLFWmonitor* monitor = config.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    GLFWwindow* glfw_window = glfwCreateWindow(
        static_cast<int>(config.width),
        static_cast<int>(config.height),
        config.title.c_str(),
        monitor,
        nullptr);

    if (!glfw_window) {
        if (--g_glfw_ref_count == 0) glfwTerminate();
        return std::unexpected(Error{"Failed to create GLFW window"});
    }

    Window window;
    window.impl_ = std::make_unique<Impl>();
    window.impl_->window = glfw_window;

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

GLFWwindow* Window::native_handle() const {
    return impl_->window;
}

} // namespace xebble
