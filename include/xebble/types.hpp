/// @file types.hpp
/// @brief Fundamental value types shared across the entire Xebble API.
///
/// Provides:
/// - `Vec2`      — 2D floating-point position/size used by the renderer and UI.
/// - `Rect`      — Floating-point axis-aligned rectangle (screen/virtual space).
/// - `Color`     — RGBA colour with 8-bit channels.
/// - `Error`     — Error message for `std::expected<T, Error>` return values.
/// - `Key`       — Keyboard key codes (values match GLFW constants).
/// - `MouseButton` — Mouse button identifiers.
/// - `Modifiers` — Keyboard modifier state (shift, ctrl, alt, super).
/// - `LogLevel`  — Log severity levels.
///
/// These types are included transitively by every other Xebble header, so you
/// rarely need to include this file directly.
#pragma once

#include <cstdint>
#include <string>

namespace xebble {

// ---------------------------------------------------------------------------
// Vec2
// ---------------------------------------------------------------------------

/// @brief 2D floating-point vector used throughout the rendering and UI layers.
///
/// Coordinates are in virtual pixels (the resolution set by `RendererConfig`)
/// unless otherwise noted. x increases rightward, y increases downward.
///
/// @code
/// // Position a sprite at the centre of a 640×360 virtual screen.
/// Position pos{320.0f, 180.0f};
///
/// // Offset a camera position by a scroll delta.
/// Vec2 delta{4.0f, 0.0f};
/// camera.x += delta.x;
/// camera.y += delta.y;
///
/// // Convert a mouse event position from screen to virtual pixels.
/// Vec2 virtual_pos = renderer.screen_to_virtual(event.mouse_move().position);
/// @endcode
struct Vec2 {
    float x = 0.0f;  ///< Horizontal component.
    float y = 0.0f;  ///< Vertical component.
};

// ---------------------------------------------------------------------------
// Rect
// ---------------------------------------------------------------------------

/// @brief Axis-aligned rectangle in floating-point virtual pixel coordinates.
///
/// Used by the renderer and UI layer for bounds, clip regions, and placement.
/// Follows the standard top-left + size convention: (x, y) is the top-left
/// corner, w and h are the width and height.
///
/// @code
/// // A 200×100 panel in the top-left corner with a 10-pixel margin.
/// Rect panel{10.0f, 10.0f, 200.0f, 100.0f};
///
/// // Test whether a virtual-space mouse position is inside a rect.
/// Vec2 cursor = renderer.screen_to_virtual(mouse_pos);
/// bool hit = cursor.x >= panel.x && cursor.x < panel.x + panel.w
///         && cursor.y >= panel.y && cursor.y < panel.y + panel.h;
/// @endcode
struct Rect {
    float x = 0.0f;  ///< Left edge in virtual pixels.
    float y = 0.0f;  ///< Top  edge in virtual pixels.
    float w = 0.0f;  ///< Width  in virtual pixels.
    float h = 0.0f;  ///< Height in virtual pixels.
};

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------

/// @brief RGBA colour with 8-bit per channel. Defaults to opaque white.
///
/// Used for sprite tints, UI colours, and the letterbox border colour.
/// The default value `{255, 255, 255, 255}` means no tint (fully opaque white),
/// which passes the underlying texture colour through unchanged.
///
/// @code
/// // Common colour constants.
/// Color white  = {255, 255, 255, 255};  // default — no tint
/// Color red    = {255,   0,   0, 255};
/// Color green  = {  0, 255,   0, 255};
/// Color blue   = {  0,   0, 255, 255};
/// Color black  = {  0,   0,   0, 255};
/// Color clear  = {  0,   0,   0,   0};  // fully transparent
///
/// // Semi-transparent dark overlay (e.g. for a "revealed but not visible" fog).
/// Color fog    = {  0,   0,   0, 160};
///
/// // Apply a red tint to indicate a damaged entity.
/// sprite.tint = {255, 80, 80, 255};
///
/// // Set the letterbox border to a deep blue.
/// renderer.set_border_color({10, 10, 40, 255});
/// @endcode
struct Color {
    uint8_t r = 255;  ///< Red channel   (0–255).
    uint8_t g = 255;  ///< Green channel (0–255).
    uint8_t b = 255;  ///< Blue channel  (0–255).
    uint8_t a = 255;  ///< Alpha channel (0 = transparent, 255 = opaque).
};

// ---------------------------------------------------------------------------
// Error
// ---------------------------------------------------------------------------

/// @brief Error message type used as the `E` in `std::expected<T, Error>`.
///
/// All Xebble factory functions that can fail return
/// `std::expected<SomeType, Error>`. Check the result before use:
///
/// @code
/// auto result = SpriteSheet::load(ctx, "tiles.png", 16, 16);
/// if (!result) {
///     log(LogLevel::Error, result.error().message);
///     return 1;
/// }
/// SpriteSheet& sheet = *result;
///
/// // Or with the monadic API:
/// auto sheet = SpriteSheet::load(ctx, "tiles.png", 16, 16)
///     .value_or_else([](const Error& e){
///         throw std::runtime_error(e.message);
///     });
/// @endcode
struct Error {
    std::string message;  ///< Human-readable description of what went wrong.
};

// ---------------------------------------------------------------------------
// Key
// ---------------------------------------------------------------------------

/// @brief Keyboard key codes. Values match GLFW key constants.
///
/// Used in `KeyData` payloads from `KeyPress`, `KeyRelease`, and `KeyRepeat`
/// events. Compare against these named constants rather than raw integers.
///
/// @code
/// for (const Event& e : world.resource<EventQueue>().events) {
///     if (e.type == EventType::KeyPress) {
///         auto& k = e.key();
///         switch (k.key) {
///             case Key::Up:     move_player({ 0, -1}); break;
///             case Key::Down:   move_player({ 0,  1}); break;
///             case Key::Left:   move_player({-1,  0}); break;
///             case Key::Right:  move_player({ 1,  0}); break;
///             case Key::Escape: request_pause();        break;
///             default: break;
///         }
///     }
/// }
/// @endcode
enum class Key {
    Unknown = -1,

    // Printable keys
    Space = 32,
    Apostrophe = 39,   ///< `'`
    Comma = 44,        ///< `,`
    Minus = 45,        ///< `-`
    Period = 46,       ///< `.`
    Slash = 47,        ///< `/`
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Semicolon = 59,    ///< `;`
    Equal = 61,        ///< `=`
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91,  ///< `[`
    Backslash = 92,    ///< `\`
    RightBracket = 93, ///< `]`
    GraveAccent = 96,  ///< `` ` ``

    // Control keys
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Insert = 260,
    Delete = 261,
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
    PageUp = 266,
    PageDown = 267,
    Home = 268,
    End = 269,
    CapsLock = 280,
    ScrollLock = 281,
    NumLock = 282,
    PrintScreen = 283,
    Pause = 284,

    // Function keys
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Modifier keys
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    LeftSuper = 343,   ///< Left Windows / Command key.
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    RightSuper = 347,  ///< Right Windows / Command key.
    Menu = 348,
};

// ---------------------------------------------------------------------------
// MouseButton
// ---------------------------------------------------------------------------

/// @brief Mouse button identifiers used in `MouseButtonData` event payloads.
///
/// @code
/// for (const Event& e : world.resource<EventQueue>().events) {
///     if (e.type == EventType::MousePress) {
///         auto& mb = e.mouse_button();
///         if (mb.button == MouseButton::Left) {
///             Vec2 pos = renderer.screen_to_virtual(mb.position);
///             handle_left_click(pos);
///         }
///         if (mb.button == MouseButton::Right) {
///             open_context_menu(mb.position);
///         }
///     }
/// }
/// @endcode
enum class MouseButton {
    Left   = 0,  ///< Primary (left) mouse button.
    Right  = 1,  ///< Secondary (right) mouse button.
    Middle = 2,  ///< Middle mouse button / scroll wheel click.
};

// ---------------------------------------------------------------------------
// Modifiers
// ---------------------------------------------------------------------------

/// @brief Keyboard modifier state accompanying key and mouse button events.
///
/// All fields default to false (no modifier held). Check these alongside
/// `Key` or `MouseButton` for common shortcuts.
///
/// @code
/// if (e.type == EventType::KeyPress) {
///     auto& k = e.key();
///     // Ctrl+S — save game.
///     if (k.key == Key::S && k.mods.ctrl) { save_game(); }
///     // Shift+Tab — previous menu item.
///     if (k.key == Key::Tab && k.mods.shift) { select_prev(); }
/// }
/// @endcode
struct Modifiers {
    bool shift = false;  ///< Left or right Shift is held.
    bool ctrl  = false;  ///< Left or right Control is held.
    bool alt   = false;  ///< Left or right Alt is held.
    bool super = false;  ///< Left or right Super (Windows/Command) is held.
};

// ---------------------------------------------------------------------------
// LogLevel
// ---------------------------------------------------------------------------

/// @brief Severity levels for the Xebble logging system.
///
/// Passed to `log()` and delivered to the log callback set by
/// `set_log_callback()`. Use these to filter or colour-code log output.
///
/// @code
/// xebble::set_log_callback([](LogLevel level, std::string_view msg) {
///     const char* prefix = "";
///     switch (level) {
///         case LogLevel::Debug: prefix = "[DEBUG] "; break;
///         case LogLevel::Info:  prefix = "[INFO]  "; break;
///         case LogLevel::Warn:  prefix = "[WARN]  "; break;
///         case LogLevel::Error: prefix = "[ERROR] "; break;
///     }
///     std::cerr << prefix << msg << '\n';
/// });
/// @endcode
enum class LogLevel {
    Debug,  ///< Verbose diagnostic information, usually disabled in release builds.
    Info,   ///< General operational messages (asset loads, state transitions, etc.).
    Warn,   ///< Unexpected but recoverable situations.
    Error,  ///< Failures that prevent correct operation.
};

} // namespace xebble
