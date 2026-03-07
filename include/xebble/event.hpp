/// @file event.hpp
/// @brief Input and window event types produced each frame by the Window.
///
/// Events are collected during `Window::poll_events()` and delivered to game
/// systems via the `EventQueue` resource on the World.  Each `Event` carries a
/// type tag (`EventType`) plus a typed payload accessed through named accessor
/// methods (`key()`, `mouse_button()`, `mouse_move()`, `mouse_scroll()`,
/// `resize()`).
///
/// ## Typical event loop
///
/// @code
/// // In a System::update() override:
/// void update(World& world, float dt) override {
///     auto& q = world.resource<EventQueue>();
///     for (const Event& e : q.events) {
///         switch (e.type) {
///
///         case EventType::KeyPress: {
///             auto& k = e.key();
///             if (k.key == Key::Escape) request_quit();
///             if (k.key == Key::F && k.mods.ctrl) toggle_fullscreen();
///             break;
///         }
///
///         case EventType::MousePress: {
///             auto& mb = e.mouse_button();
///             if (mb.button == MouseButton::Left)
///                 handle_left_click(mb.position);
///             break;
///         }
///
///         case EventType::MouseMove:
///             update_cursor(e.mouse_move().position);
///             break;
///
///         case EventType::MouseScroll:
///             camera_zoom(e.mouse_scroll().dy);
///             break;
///
///         case EventType::WindowClose:
///             request_quit();
///             break;
///
///         default: break;
///         }
///     }
/// }
/// @endcode
#pragma once

#include <xebble/types.hpp>
#include <variant>

namespace xebble {

// ---------------------------------------------------------------------------
// EventType
// ---------------------------------------------------------------------------

/// @brief Discriminator tag for every event variant.
///
/// Check `event.type` before calling a typed accessor; calling the wrong
/// accessor (e.g. `key()` on a `MouseMove` event) will throw `std::bad_variant_access`.
enum class EventType {
    /// A key was pressed for the first time (not a repeat).
    KeyPress,
    /// A key was released.
    KeyRelease,
    /// A key is being held and the OS is sending auto-repeat events.
    KeyRepeat,
    /// A mouse button was pressed.
    MousePress,
    /// A mouse button was released.
    MouseRelease,
    /// The mouse cursor moved. Position is in screen (not virtual) pixels.
    MouseMove,
    /// The scroll wheel moved.
    MouseScroll,
    /// The window was resized. Dimensions are in framebuffer pixels.
    WindowResize,
    /// The window gained keyboard focus.
    WindowFocusGained,
    /// The window lost keyboard focus.
    WindowFocusLost,
    /// The user asked to close the window (e.g. clicked the X button).
    WindowClose,
};

// ---------------------------------------------------------------------------
// Payload types
// ---------------------------------------------------------------------------

/// @brief Payload for `KeyPress`, `KeyRelease`, and `KeyRepeat` events.
///
/// @code
/// if (e.type == EventType::KeyPress) {
///     auto& k = e.key();
///     // Directional input.
///     if (k.key == Key::Up)    move_player({ 0, -1});
///     if (k.key == Key::Down)  move_player({ 0,  1});
///     // Ctrl+Z to undo last action.
///     if (k.key == Key::Z && k.mods.ctrl) undo();
/// }
/// @endcode
struct KeyData {
    Key       key;   ///< Which key was involved.
    Modifiers mods;  ///< Modifier keys held at the time of the event.
};

/// @brief Payload for `MousePress` and `MouseRelease` events.
///
/// `position` is in *screen* coordinates (physical pixels). Convert to virtual
/// pixel space before using it for game-world hit-testing:
///
/// @code
/// if (e.type == EventType::MousePress) {
///     auto& mb = e.mouse_button();
///     if (mb.button == MouseButton::Left) {
///         // Convert to virtual pixels for tile picking.
///         Vec2 vpos = renderer.screen_to_virtual(mb.position);
///         IVec2 tile = {(int)vpos.x / tile_size, (int)vpos.y / tile_size};
///         select_tile(tile);
///     }
/// }
/// @endcode
struct MouseButtonData {
    MouseButton button;    ///< Which button was pressed or released.
    Modifiers   mods;      ///< Modifier keys held at the time of the event.
    Vec2        position;  ///< Cursor position in screen (physical) pixels.
};

/// @brief Payload for `MouseMove` events.
///
/// Position is in screen coordinates. Use `Renderer::screen_to_virtual()` to
/// map it to virtual-pixel space for game-world hit-testing.
///
/// @code
/// if (e.type == EventType::MouseMove) {
///     Vec2 screen_pos = e.mouse_move().position;
///     Vec2 vpos       = renderer.screen_to_virtual(screen_pos);
///     hover_tile = {(int)vpos.x / tile_size, (int)vpos.y / tile_size};
/// }
/// @endcode
struct MouseMoveData {
    Vec2 position;  ///< Cursor position in screen (physical) pixels.
};

/// @brief Payload for `MouseScroll` events.
///
/// Both axes are provided — most mice only produce vertical (`dy`) scroll,
/// but trackpads and some mice also produce horizontal (`dx`) scroll.
///
/// @code
/// if (e.type == EventType::MouseScroll) {
///     float zoom_delta = e.mouse_scroll().dy;
///     camera_zoom = std::clamp(camera_zoom + zoom_delta * 0.1f, 0.5f, 4.0f);
/// }
/// @endcode
struct MouseScrollData {
    float dx = 0.0f;  ///< Horizontal scroll delta (positive = right).
    float dy = 0.0f;  ///< Vertical scroll delta   (positive = up on most platforms).
};

/// @brief Payload for `WindowResize` events.
///
/// Dimensions are in *framebuffer* pixels, which may differ from screen
/// coordinates on HiDPI (Retina) displays. The renderer handles this
/// automatically — you only need this if you're doing custom Vulkan work.
///
/// @code
/// if (e.type == EventType::WindowResize) {
///     auto& r = e.resize();
///     log(LogLevel::Info, "window resized to " +
///         std::to_string(r.width) + "x" + std::to_string(r.height));
/// }
/// @endcode
struct ResizeData {
    uint32_t width  = 0;  ///< New framebuffer width in pixels.
    uint32_t height = 0;  ///< New framebuffer height in pixels.
};

// ---------------------------------------------------------------------------
// Event
// ---------------------------------------------------------------------------

/// @brief A tagged union representing one input or window event.
///
/// Events are created internally by the `Window` class during `poll_events()`
/// and delivered to game systems via the `EventQueue` resource.
///
/// The `type` field is always set and safe to inspect. Call the matching typed
/// accessor only after confirming the type — calling the wrong one throws
/// `std::bad_variant_access`.
///
/// The `consumed` flag can be set by a system to signal that downstream
/// systems should ignore the event (e.g. a UI layer consuming keyboard input
/// before it reaches the game logic layer).
///
/// @code
/// for (Event& e : world.resource<EventQueue>().events) {
///     // The UI layer gets first pick; mark keyboard events as consumed.
///     if (e.type == EventType::KeyPress && ui_has_focus()) {
///         ui.handle_key(e.key());
///         e.consumed = true;
///         continue;
///     }
///     if (e.consumed) continue;
///
///     // Game logic handles everything else.
///     if (e.type == EventType::KeyPress)
///         game.handle_key(e.key());
/// }
/// @endcode
class Event {
public:
    EventType type;          ///< Discriminator — always valid.
    bool consumed = false;   ///< Set by a handler to prevent downstream processing.

    /// @name Factory methods (used internally by Window)
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

    /// @name Typed payload accessors
    ///
    /// Each accessor asserts that the event carries the correct payload type.
    /// Call only after confirming `type` with a switch or if-check.
    /// @{

    /// @brief Access key event data. Valid for KeyPress, KeyRelease, KeyRepeat.
    const KeyData& key() const { return std::get<KeyData>(data_); }

    /// @brief Access mouse button data. Valid for MousePress, MouseRelease.
    const MouseButtonData& mouse_button() const { return std::get<MouseButtonData>(data_); }

    /// @brief Access mouse move data. Valid for MouseMove.
    const MouseMoveData& mouse_move() const { return std::get<MouseMoveData>(data_); }

    /// @brief Access scroll data. Valid for MouseScroll.
    const MouseScrollData& mouse_scroll() const { return std::get<MouseScrollData>(data_); }

    /// @brief Access resize data. Valid for WindowResize.
    const ResizeData& resize() const { return std::get<ResizeData>(data_); }
    /// @}

private:
    Event() = default;
    std::variant<std::monostate, KeyData, MouseButtonData,
                 MouseMoveData, MouseScrollData, ResizeData> data_;
};

} // namespace xebble
