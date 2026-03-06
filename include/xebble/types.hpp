/// @file types.hpp
/// @brief Common types used throughout the Xebble API.
///
/// Provides fundamental value types (Vec2, Rect, Color), keyboard/mouse
/// enumerations mirroring GLFW key codes, modifier state, and the Error
/// type used with std::expected for fallible operations.
#pragma once

#include <cstdint>
#include <string>

namespace xebble {

/// @brief 2D floating-point vector.
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

/// @brief Axis-aligned rectangle defined by position and size.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

/// @brief RGBA color with 8-bit channels. Defaults to opaque white.
struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

/// @brief Error type for fallible operations. Used as the E in std::expected<T, Error>.
struct Error {
    std::string message;
};

/// @brief Keyboard key codes. Values match GLFW key constants.
enum class Key {
    Unknown = -1,
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Semicolon = 59,
    Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,
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
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    LeftSuper = 343,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    RightSuper = 347,
    Menu = 348,
};

/// @brief Mouse button identifiers.
enum class MouseButton {
    Left = 0,
    Right = 1,
    Middle = 2,
};

/// @brief Keyboard modifier state (shift, ctrl, alt, super).
struct Modifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
};

/// @brief Log severity levels.
enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

} // namespace xebble
