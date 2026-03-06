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

struct WindowConfig {
    std::string title = "Xebble";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool resizable = true;
    bool fullscreen = false;
};

class Window {
public:
    static std::expected<Window, Error> create(const WindowConfig& config);

    ~Window();
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool should_close() const;
    void poll_events();
    std::span<const Event> events() const;

    float content_scale() const;
    std::pair<uint32_t, uint32_t> framebuffer_size() const;
    std::pair<uint32_t, uint32_t> window_size() const;

    GLFWwindow* native_handle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Window() = default;
};

} // namespace xebble
