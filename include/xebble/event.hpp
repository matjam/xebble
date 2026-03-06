/// @file event.hpp
/// @brief Input and window event types for Xebble.
///
/// Events are produced by the Window class during poll_events() and consumed
/// by game code via the events() span. Each Event carries a discriminated type
/// tag and type-safe payload accessed through named accessors (key(), mouse_button(),
/// mouse_move(), mouse_scroll(), resize()).
#pragma once

#include <xebble/types.hpp>
#include <variant>

namespace xebble {

/// @brief Discriminator for event types.
enum class EventType {
    KeyPress, KeyRelease, KeyRepeat,
    MousePress, MouseRelease,
    MouseMove, MouseScroll,
    WindowResize, WindowFocusGained, WindowFocusLost, WindowClose,
};

/// @brief Payload for key events (press, release, repeat).
struct KeyData {
    Key key;
    Modifiers mods;
};

/// @brief Payload for mouse button events (press, release).
struct MouseButtonData {
    MouseButton button;
    Modifiers mods;
    Vec2 position;
};

/// @brief Payload for mouse movement events.
struct MouseMoveData {
    Vec2 position;
};

/// @brief Payload for scroll wheel events.
struct MouseScrollData {
    float dx = 0.0f;
    float dy = 0.0f;
};

/// @brief Payload for window resize events. Values are in framebuffer pixels.
struct ResizeData {
    uint32_t width = 0;
    uint32_t height = 0;
};

/// @brief A tagged union representing an input or window event.
///
/// Events are created via static factory methods and read via typed accessors.
/// The type field is always set and can be used in a switch statement.
class Event {
public:
    EventType type;

    /// @name Factory methods
    /// @{
    static Event key_press(Key key, Modifiers mods) {
        Event e; e.type = EventType::KeyPress; e.data_ = KeyData{key, mods}; return e;
    }
    static Event key_release(Key key, Modifiers mods) {
        Event e; e.type = EventType::KeyRelease; e.data_ = KeyData{key, mods}; return e;
    }
    static Event key_repeat(Key key, Modifiers mods) {
        Event e; e.type = EventType::KeyRepeat; e.data_ = KeyData{key, mods}; return e;
    }
    static Event mouse_press(MouseButton button, Modifiers mods, Vec2 pos) {
        Event e; e.type = EventType::MousePress; e.data_ = MouseButtonData{button, mods, pos}; return e;
    }
    static Event mouse_release(MouseButton button, Modifiers mods, Vec2 pos) {
        Event e; e.type = EventType::MouseRelease; e.data_ = MouseButtonData{button, mods, pos}; return e;
    }
    static Event mouse_move(Vec2 pos) {
        Event e; e.type = EventType::MouseMove; e.data_ = MouseMoveData{pos}; return e;
    }
    static Event mouse_scroll(float dx, float dy) {
        Event e; e.type = EventType::MouseScroll; e.data_ = MouseScrollData{dx, dy}; return e;
    }
    static Event window_resize(uint32_t w, uint32_t h) {
        Event e; e.type = EventType::WindowResize; e.data_ = ResizeData{w, h}; return e;
    }
    static Event window_focus_gained() {
        Event e; e.type = EventType::WindowFocusGained; return e;
    }
    static Event window_focus_lost() {
        Event e; e.type = EventType::WindowFocusLost; return e;
    }
    static Event window_close() {
        Event e; e.type = EventType::WindowClose; return e;
    }
    /// @}

    /// @name Typed accessors
    /// @{
    const KeyData& key() const { return std::get<KeyData>(data_); }
    const MouseButtonData& mouse_button() const { return std::get<MouseButtonData>(data_); }
    const MouseMoveData& mouse_move() const { return std::get<MouseMoveData>(data_); }
    const MouseScrollData& mouse_scroll() const { return std::get<MouseScrollData>(data_); }
    const ResizeData& resize() const { return std::get<ResizeData>(data_); }
    /// @}

private:
    Event() = default;
    std::variant<std::monostate, KeyData, MouseButtonData, MouseMoveData, MouseScrollData, ResizeData> data_;
};

} // namespace xebble
