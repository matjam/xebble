# UI System Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add an immediate-mode UI system to xebble with panels, text, buttons, checkboxes, lists, and text input — auto-registered with an embedded default font so games need zero setup for basic UI.

**Architecture:** `UIContext` resource with imgui-style API. `UIInputSystem` (prepended before user systems) processes input against previous frame's widget rects. `UIFlushSystem` (appended after render systems) submits UI sprite batches. `run()` auto-registers everything including a default `UITheme` with an embedded 8x12 bitmap font.

**Tech Stack:** C++23, GoogleTest, CMake

---

### Task 1: Infrastructure Changes

**Files:**
- Modify: `include/xebble/event.hpp`
- Modify: `include/xebble/ecs.hpp`
- Modify: `include/xebble/world.hpp`
- Modify: `src/game.cpp`
- Modify: `src/renderer.cpp`
- Modify: `include/xebble/renderer.hpp`
- Modify: `tests/test_ecs.cpp`

Several small changes across existing files to support the UI system.

**Step 1: Add `consumed` field to Event**

In `include/xebble/event.hpp`, add a public field to the `Event` class:

```cpp
class Event {
public:
    EventType type;
    bool consumed = false;  // ADD THIS LINE
```

**Step 2: Change EventQueue to own events**

In `include/xebble/ecs.hpp`, change EventQueue from span to vector:

```cpp
struct EventQueue {
    std::vector<Event> events;
};
```

Add `#include <vector>` if not already present (it is).

Remove the `#include <span>` if no longer needed — but keep it since ComponentPool uses span indirectly. Actually, `<span>` is not used elsewhere in ecs.hpp, but leave it for safety.

**Step 3: Update run() to copy events**

In `src/game.cpp`, change the event line from:

```cpp
world.resource<EventQueue>().events = window->events();
```

to:

```cpp
auto raw = window->events();
world.resource<EventQueue>().events.assign(raw.begin(), raw.end());
```

**Step 4: Add prepend_system to World**

In `include/xebble/world.hpp`, add after the existing `add_system` method:

```cpp
    template<typename T, typename... Args>
    void prepend_system(Args&&... args) {
        systems_.insert(systems_.begin(), std::make_unique<T>(std::forward<Args>(args)...));
    }
```

**Step 5: Add screen_to_virtual to Renderer**

In `include/xebble/renderer.hpp`, add to the Renderer public interface (after `virtual_height()`):

```cpp
    /// @brief Convert screen coordinates (from mouse events) to virtual pixel coordinates.
    Vec2 screen_to_virtual(Vec2 screen_pos) const;
```

In `src/renderer.cpp`, add three fields to `Renderer::Impl`:

```cpp
    float blit_scale = 1.0f;
    float blit_offset_x = 0.0f;
    float blit_offset_y = 0.0f;
```

In the `end_frame()` method, after computing the blit viewport variables (`scale`, `offset_x`, `offset_y`), store them:

```cpp
    impl.blit_scale = scale;
    impl.blit_offset_x = offset_x;
    impl.blit_offset_y = offset_y;
```

Add the implementation:

```cpp
Vec2 Renderer::screen_to_virtual(Vec2 screen_pos) const {
    float cs = impl_->window->content_scale();
    float fx = screen_pos.x * cs;
    float fy = screen_pos.y * cs;
    return {
        (fx - impl_->blit_offset_x) / impl_->blit_scale,
        (fy - impl_->blit_offset_y) / impl_->blit_scale
    };
}
```

**Step 6: Add test for prepend_system**

In `tests/test_ecs.cpp`, add:

```cpp
TEST(World, PrependSystem) {
    World world;
    std::vector<int> order;

    struct SystemA : System {
        std::vector<int>* order;
        void update(World&, float) override { order->push_back(1); }
    };
    struct SystemB : System {
        std::vector<int>* order;
        void update(World&, float) override { order->push_back(2); }
    };

    auto a = std::make_unique<SystemA>();
    a->order = &order;
    auto b = std::make_unique<SystemB>();
    b->order = &order;

    world.add_system<SystemA>();
    // Access the system to set the pointer — need a different approach
    // Actually, use add_system with args:
    // This won't work because SystemA doesn't take constructor args.
    // Let's use a simpler test:

    struct OrderTracker {
        std::vector<int> order;
    };

    // Re-do with resources
    world.add_resource<OrderTracker>({});

    struct FirstSystem : System {
        void update(World& w, float) override { w.resource<OrderTracker>().order.push_back(1); }
    };
    struct SecondSystem : System {
        void update(World& w, float) override { w.resource<OrderTracker>().order.push_back(2); }
    };

    world.add_system<SecondSystem>();
    world.prepend_system<FirstSystem>();

    Renderer* dummy = nullptr;
    world.init_systems();
    world.tick_update(0.0f);

    auto& result = world.resource<OrderTracker>().order;
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], 1); // prepended system runs first
    EXPECT_EQ(result[1], 2);
}
```

**Step 7: Build and test**

Run: `cmake --build build/debug && ctest --test-dir build/debug --output-on-failure`
Expected: All tests pass including new PrependSystem test.

**Step 8: Commit**

```bash
git add include/xebble/event.hpp include/xebble/ecs.hpp include/xebble/world.hpp \
        src/game.cpp src/renderer.cpp include/xebble/renderer.hpp tests/test_ecs.cpp
git commit -m "feat: infrastructure for UI system — event consumed flag, owned EventQueue, prepend_system, screen_to_virtual"
```

---

### Task 2: Embedded Font

**Files:**
- Create: `include/xebble/embedded_font.hpp`
- Create: `src/embedded_font.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/test_embedded_font.cpp`
- Modify: `tests/CMakeLists.txt`

Embed an 8x12 bitmap font in the binary using the classic CP437 8x8 glyph data centered in 8x12 cells.

**Step 1: Create the header**

Create `include/xebble/embedded_font.hpp`:

```cpp
/// @file embedded_font.hpp
/// @brief Embedded 8x12 bitmap font for default UI rendering.
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace xebble {

class BitmapFont;
class SpriteSheet;

namespace vk {
class Context;
}

namespace embedded_font {

constexpr uint32_t GLYPH_W = 8;
constexpr uint32_t GLYPH_H = 12;
constexpr uint32_t CHARS_PER_ROW = 16;
constexpr int FIRST_CHAR = 32;
constexpr int LAST_CHAR = 126;
constexpr int NUM_CHARS = LAST_CHAR - FIRST_CHAR + 1;
constexpr uint32_t ROWS = (NUM_CHARS + CHARS_PER_ROW - 1) / CHARS_PER_ROW;
constexpr uint32_t ATLAS_W = CHARS_PER_ROW * GLYPH_W;
constexpr uint32_t ATLAS_H = ROWS * GLYPH_H;

/// @brief Generate RGBA pixel data for the embedded font atlas.
/// @return Vector of ATLAS_W * ATLAS_H * 4 bytes (RGBA).
std::vector<uint8_t> generate_pixels();

/// @brief Get the charset string (ASCII 32-126).
std::string charset();

/// @brief Create a BitmapFont from the embedded font data.
/// @param ctx Vulkan context for GPU texture creation.
std::expected<std::unique_ptr<BitmapFont>, Error> create_font(vk::Context& ctx);

} // namespace embedded_font
} // namespace xebble
```

**Step 2: Create the implementation**

Create `src/embedded_font.cpp`:

```cpp
/// @file embedded_font.cpp
/// @brief Embedded font data and creation.
#include <xebble/embedded_font.hpp>
#include <xebble/font.hpp>
#include <xebble/spritesheet.hpp>
#include <xebble/texture.hpp>
#include <xebble/types.hpp>

namespace xebble::embedded_font {

// CP437 8x8 font data for printable ASCII (32-126).
// Each character is 8 bytes, one per row, MSB = leftmost pixel.
// Classic IBM PC BIOS font (public domain).
// clang-format off
static constexpr uint8_t FONT_DATA[] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 32: space
    0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00, // 33: !
    0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00, // 34: "
    0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00, // 35: #
    0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00, // 36: $
    0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00, // 37: %
    0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00, // 38: &
    0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00, // 39: '
    0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00, // 40: (
    0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00, // 41: )
    0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00, // 42: *
    0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00, // 43: +
    0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30, // 44: ,
    0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00, // 45: -
    0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00, // 46: .
    0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00, // 47: /
    0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0x00, // 48: 0
    0x18,0x38,0x78,0x18,0x18,0x18,0x7E,0x00, // 49: 1
    0x7C,0xC6,0x06,0x1C,0x30,0x66,0xFE,0x00, // 50: 2
    0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00, // 51: 3
    0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00, // 52: 4
    0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00, // 53: 5
    0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00, // 54: 6
    0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00, // 55: 7
    0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00, // 56: 8
    0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00, // 57: 9
    0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00, // 58: :
    0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30, // 59: ;
    0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00, // 60: <
    0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00, // 61: =
    0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00, // 62: >
    0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00, // 63: ?
    0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00, // 64: @
    0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00, // 65: A
    0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00, // 66: B
    0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00, // 67: C
    0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00, // 68: D
    0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00, // 69: E
    0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00, // 70: F
    0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00, // 71: G
    0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00, // 72: H
    0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00, // 73: I
    0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00, // 74: J
    0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00, // 75: K
    0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00, // 76: L
    0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00, // 77: M
    0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00, // 78: N
    0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00, // 79: O
    0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00, // 80: P
    0x7C,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06, // 81: Q
    0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00, // 82: R
    0x7C,0xC6,0xC0,0x7C,0x06,0xC6,0x7C,0x00, // 83: S
    0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00, // 84: T
    0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00, // 85: U
    0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00, // 86: V
    0xC6,0xC6,0xD6,0xFE,0xFE,0xEE,0xC6,0x00, // 87: W
    0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00, // 88: X
    0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00, // 89: Y
    0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00, // 90: Z
    0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00, // 91: [
    0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00, // 92: backslash
    0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00, // 93: ]
    0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00, // 94: ^
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF, // 95: _
    0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00, // 96: `
    0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00, // 97: a
    0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00, // 98: b
    0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00, // 99: c
    0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00, // 100: d
    0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00, // 101: e
    0x1C,0x36,0x30,0x78,0x30,0x30,0x78,0x00, // 102: f
    0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8, // 103: g
    0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00, // 104: h
    0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00, // 105: i
    0x06,0x00,0x06,0x06,0x06,0x66,0x66,0x3C, // 106: j
    0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00, // 107: k
    0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00, // 108: l
    0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0x00, // 109: m
    0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00, // 110: n
    0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00, // 111: o
    0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0, // 112: p
    0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E, // 113: q
    0x00,0x00,0xDC,0x76,0x60,0x60,0xF0,0x00, // 114: r
    0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00, // 115: s
    0x30,0x30,0x7C,0x30,0x30,0x36,0x1C,0x00, // 116: t
    0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00, // 117: u
    0x00,0x00,0xC6,0xC6,0x6C,0x38,0x10,0x00, // 118: v
    0x00,0x00,0xC6,0xD6,0xD6,0xFE,0x6C,0x00, // 119: w
    0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00, // 120: x
    0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC, // 121: y
    0x00,0x00,0xFE,0x8C,0x18,0x32,0xFE,0x00, // 122: z
    0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00, // 123: {
    0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00, // 124: |
    0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00, // 125: }
    0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00, // 126: ~
};
// clang-format on

std::vector<uint8_t> generate_pixels() {
    std::vector<uint8_t> pixels(ATLAS_W * ATLAS_H * 4, 0);
    constexpr int SRC_GLYPH_H = 8;
    constexpr int PAD_TOP = (GLYPH_H - SRC_GLYPH_H) / 2; // 2px top padding

    for (int ci = 0; ci < NUM_CHARS; ci++) {
        int col = ci % CHARS_PER_ROW;
        int row = ci / CHARS_PER_ROW;
        const uint8_t* glyph = &FONT_DATA[ci * SRC_GLYPH_H];

        for (int gy = 0; gy < SRC_GLYPH_H; gy++) {
            uint8_t bits = glyph[gy];
            for (int gx = 0; gx < static_cast<int>(GLYPH_W); gx++) {
                if (bits & (0x80 >> gx)) {
                    int px = col * static_cast<int>(GLYPH_W) + gx;
                    int py = row * static_cast<int>(GLYPH_H) + PAD_TOP + gy;
                    int idx = (py * static_cast<int>(ATLAS_W) + px) * 4;
                    pixels[idx + 0] = 255;
                    pixels[idx + 1] = 255;
                    pixels[idx + 2] = 255;
                    pixels[idx + 3] = 255;
                }
            }
        }
    }
    return pixels;
}

std::string charset() {
    std::string s;
    for (int c = FIRST_CHAR; c <= LAST_CHAR; c++)
        s += static_cast<char>(c);
    return s;
}

std::expected<std::unique_ptr<BitmapFont>, Error> create_font(vk::Context& ctx) {
    auto pixels = generate_pixels();
    auto texture = Texture::create_from_pixels(ctx, pixels.data(), ATLAS_W, ATLAS_H);
    if (!texture) return std::unexpected(texture.error());

    auto sheet = SpriteSheet::from_texture(std::move(*texture), GLYPH_W, GLYPH_H);
    if (!sheet) return std::unexpected(sheet.error());

    auto font = BitmapFont::from_spritesheet(std::move(*sheet), charset());
    if (!font) return std::unexpected(font.error());

    return std::make_unique<BitmapFont>(std::move(*font));
}

} // namespace xebble::embedded_font
```

**Step 3: Add to CMake**

In `src/CMakeLists.txt`, add `embedded_font.cpp` after `builtin_systems.cpp`.

**Step 4: Write test**

Create `tests/test_embedded_font.cpp`:

```cpp
#include <xebble/embedded_font.hpp>
#include <gtest/gtest.h>

using namespace xebble::embedded_font;

TEST(EmbeddedFont, AtlasDimensions) {
    EXPECT_EQ(ATLAS_W, 128u); // 16 chars * 8px
    EXPECT_EQ(ATLAS_H, 72u);  // 6 rows * 12px
}

TEST(EmbeddedFont, PixelDataSize) {
    auto pixels = generate_pixels();
    EXPECT_EQ(pixels.size(), ATLAS_W * ATLAS_H * 4);
}

TEST(EmbeddedFont, PixelDataNotEmpty) {
    auto pixels = generate_pixels();
    // At least some pixels should be non-zero (not all transparent)
    bool has_content = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) { has_content = true; break; }
    }
    EXPECT_TRUE(has_content);
}

TEST(EmbeddedFont, SpaceIsEmpty) {
    auto pixels = generate_pixels();
    // Space char (index 0) should be all transparent
    // Space is at column 0, row 0 of the atlas
    for (uint32_t y = 0; y < GLYPH_H; y++) {
        for (uint32_t x = 0; x < GLYPH_W; x++) {
            int idx = (y * ATLAS_W + x) * 4;
            EXPECT_EQ(pixels[idx + 3], 0) << "at (" << x << "," << y << ")";
        }
    }
}

TEST(EmbeddedFont, Charset) {
    auto cs = charset();
    EXPECT_EQ(cs.size(), 95u); // 126 - 32 + 1
    EXPECT_EQ(cs[0], ' ');
    EXPECT_EQ(cs[cs.size() - 1], '~');
}
```

Add to `tests/CMakeLists.txt` — add `test_embedded_font.cpp` to the test sources list.

**Step 5: Build and test**

Run: `cmake --build build/debug && ctest --test-dir build/debug --output-on-failure`
Expected: All tests pass.

**Step 6: Commit**

```bash
git add include/xebble/embedded_font.hpp src/embedded_font.cpp src/CMakeLists.txt \
        tests/test_embedded_font.cpp tests/CMakeLists.txt
git commit -m "feat: add embedded 8x12 bitmap font for default UI theme"
```

---

### Task 3: UI Types and UIContext Header

**Files:**
- Create: `include/xebble/ui.hpp`
- Create: `tests/test_ui.cpp`
- Modify: `tests/CMakeLists.txt`

Define all UI types and the UIContext/PanelBuilder class declarations. Test the pure-math anchor resolution.

**Step 1: Create the header**

Create `include/xebble/ui.hpp`:

```cpp
/// @file ui.hpp
/// @brief Immediate-mode UI system — panels, controls, and theming.
#pragma once

#include <xebble/types.hpp>
#include <xebble/renderer.hpp>
#include <xebble/font.hpp>
#include <xebble/system.hpp>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace xebble {

class World;

// --- Enums ---

enum class Anchor {
    TopLeft, Top, TopRight,
    Left, Center, Right,
    BottomLeft, Bottom, BottomRight,
};

// --- Placement ---

struct PanelPlacement {
    Anchor anchor = Anchor::TopLeft;
    Vec2 size = {};    // 0.0-1.0 = fraction of screen, >1.0 = pixels
    Vec2 offset = {};  // pixel displacement from anchor point
};

// --- Style structs ---

struct TextStyle {
    Color color = {0, 0, 0, 0}; // {0,0,0,0} means "use theme default"
};

struct ButtonStyle {
    Color color = {0, 0, 0, 0};
    Color hover_color = {0, 0, 0, 0};
    Color text_color = {0, 0, 0, 0};
};

struct CheckboxStyle {
    Color color = {0, 0, 0, 0};
    Color checked_color = {0, 0, 0, 0};
    Color text_color = {0, 0, 0, 0};
};

struct ListStyle {
    Color color = {0, 0, 0, 0};
    Color selected_color = {0, 0, 0, 0};
    Color text_color = {0, 0, 0, 0};
    float visible_rows = 8;
};

struct TextInputStyle {
    Color color = {0, 0, 0, 0};
    Color active_color = {0, 0, 0, 0};
    Color text_color = {0, 0, 0, 0};
};

// --- Theme ---

struct UITheme {
    std::variant<const BitmapFont*, const Font*> font = static_cast<const BitmapFont*>(nullptr);
    Color bg_color = {20, 20, 30, 220};
    Color text_color = {200, 200, 200, 255};
    Color button_color = {60, 60, 80, 255};
    Color button_hover_color = {80, 80, 110, 255};
    Color button_text_color = {230, 230, 230, 255};
    Color checkbox_color = {60, 60, 80, 255};
    Color checkbox_checked_color = {100, 180, 100, 255};
    Color input_color = {40, 40, 50, 255};
    Color input_active_color = {50, 50, 70, 255};
    Color list_color = {40, 40, 50, 255};
    Color list_selected_color = {70, 70, 100, 255};
    float padding = 4.0f;
    float margin = 2.0f;
    float z_order = 100.0f;
};

// --- Forward declarations ---

class UIContext;

// --- PanelBuilder ---

class PanelBuilder {
public:
    PanelBuilder(UIContext& ctx, Rect panel_rect, float z_base);

    void text(std::string_view text, TextStyle style = {});
    bool button(std::string_view label, ButtonStyle style = {});
    void checkbox(std::string_view label, bool& value, CheckboxStyle style = {});
    void list(std::string_view id, std::span<const std::string> items,
              int& selected, ListStyle style = {});
    bool text_input(std::string_view id, std::string& value, TextInputStyle style = {});

    template<typename Fn>
    void horizontal(Fn&& fn) {
        float saved_y = cursor_y_;
        float saved_x = content_x_;
        float saved_w = content_width_;
        in_horizontal_ = true;
        horiz_cursor_x_ = content_x_;
        horiz_max_h_ = 0;
        fn(*this);
        in_horizontal_ = false;
        cursor_y_ = saved_y + horiz_max_h_ + margin_;
        content_x_ = saved_x;
        content_width_ = saved_w;
    }

private:
    friend class UIContext;

    Rect next_control_rect(float height);
    float text_height() const;
    float measure_text_width(std::string_view text) const;

    UIContext& ctx_;
    Rect panel_rect_;
    float z_base_;
    float cursor_y_;
    float content_x_;
    float content_width_;
    float padding_;
    float margin_;
    bool in_horizontal_ = false;
    float horiz_cursor_x_ = 0;
    float horiz_max_h_ = 0;
};

// --- UIContext ---

class UIContext {
public:
    UIContext();
    ~UIContext();

    void begin_frame(std::vector<Event>& events, const Renderer& renderer);

    template<typename Fn>
    void panel(std::string_view id, PanelPlacement placement, Fn&& fn) {
        auto rect = resolve_placement(placement);
        draw_panel_bg(rect);

        float z = theme_->z_order;
        PanelBuilder builder(*this, rect, z);
        fn(builder);
    }

    void flush(Renderer& renderer);

private:
    friend class PanelBuilder;

    Rect resolve_placement(const PanelPlacement& p) const;
    void draw_panel_bg(Rect rect);
    void draw_rect(Rect rect, Color color, float z);
    void draw_text_at(std::string_view text, float x, float y, Color color, float z);
    float glyph_width() const;
    float glyph_height() const;

    void register_widget(std::string_view id, Rect rect);
    bool is_hot(std::string_view id) const;
    bool is_clicked(std::string_view id) const;

    const UITheme* theme_ = nullptr;
    uint32_t screen_w_ = 0;
    uint32_t screen_h_ = 0;
    Vec2 mouse_pos_ = {};
    bool mouse_clicked_ = false;
    bool mouse_down_ = false;

    std::string hot_id_;
    std::string active_id_;
    std::string focused_input_id_;

    struct WidgetRect {
        std::string id;
        Rect rect;
    };
    std::vector<WidgetRect> prev_rects_;
    std::vector<WidgetRect> curr_rects_;

    // Scroll state for lists (persists across frames)
    std::unordered_map<std::string, int> scroll_offsets_;

    // Text input cursor position
    std::unordered_map<std::string, size_t> cursor_positions_;

    // Queued characters for text input (from key events)
    std::vector<char> input_chars_;

    // Draw batches
    struct DrawBatch {
        std::vector<SpriteInstance> instances;
        const Texture* texture;
        float z_order;
    };
    std::vector<DrawBatch> batches_;

    // Consumed events
    std::vector<Event>* frame_events_ = nullptr;
};

// --- Built-in Systems ---

class UIInputSystem : public System {
public:
    void update(World& world, float dt) override;
};

class UIFlushSystem : public System {
public:
    void draw(World& world, Renderer& renderer) override;
};

// --- Utility ---

/// @brief Resolve a PanelPlacement to a screen Rect.
Rect resolve_panel_placement(const PanelPlacement& p, uint32_t screen_w, uint32_t screen_h);

} // namespace xebble
```

**Step 2: Write anchor resolution tests**

Create `tests/test_ui.cpp`:

```cpp
#include <xebble/ui.hpp>
#include <gtest/gtest.h>

using namespace xebble;

TEST(UIPlacement, TopLeft) {
    PanelPlacement p{.anchor = Anchor::TopLeft, .size = {200, 100}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 0.0f);
    EXPECT_FLOAT_EQ(r.w, 200.0f);
    EXPECT_FLOAT_EQ(r.h, 100.0f);
}

TEST(UIPlacement, Center) {
    PanelPlacement p{.anchor = Anchor::Center, .size = {200, 100}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.x, 220.0f); // (640-200)/2
    EXPECT_FLOAT_EQ(r.y, 130.0f); // (360-100)/2
    EXPECT_FLOAT_EQ(r.w, 200.0f);
    EXPECT_FLOAT_EQ(r.h, 100.0f);
}

TEST(UIPlacement, BottomRight) {
    PanelPlacement p{.anchor = Anchor::BottomRight, .size = {200, 100}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.x, 440.0f); // 640-200
    EXPECT_FLOAT_EQ(r.y, 260.0f); // 360-100
}

TEST(UIPlacement, WithOffset) {
    PanelPlacement p{.anchor = Anchor::TopRight, .size = {128, 128}, .offset = {-8, 8}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.x, 640.0f - 128.0f - 8.0f); // 504
    EXPECT_FLOAT_EQ(r.y, 8.0f);
}

TEST(UIPlacement, FractionalWidth) {
    PanelPlacement p{.anchor = Anchor::Bottom, .size = {1.0f, 40}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.w, 640.0f); // 1.0 * screen width
    EXPECT_FLOAT_EQ(r.h, 40.0f);
    EXPECT_FLOAT_EQ(r.x, 0.0f);   // centered, full width
    EXPECT_FLOAT_EQ(r.y, 320.0f);  // 360 - 40
}

TEST(UIPlacement, FractionalBoth) {
    PanelPlacement p{.anchor = Anchor::Center, .size = {0.5f, 0.5f}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.w, 320.0f); // 0.5 * 640
    EXPECT_FLOAT_EQ(r.h, 180.0f); // 0.5 * 360
    EXPECT_FLOAT_EQ(r.x, 160.0f); // (640-320)/2
    EXPECT_FLOAT_EQ(r.y, 90.0f);  // (360-180)/2
}
```

Add `test_ui.cpp` to `tests/CMakeLists.txt`.

**Step 3: Build and test**

This will not link yet because `resolve_panel_placement` is declared but not implemented. Implement just this function to make the tests pass.

Create the start of `src/ui.cpp`:

```cpp
/// @file ui.cpp
/// @brief Immediate-mode UI system implementation.
#include <xebble/ui.hpp>
#include <xebble/world.hpp>
#include <xebble/spritesheet.hpp>

#include <algorithm>

namespace xebble {

// --- Placement resolution ---

static float resolve_size(float s, float screen) {
    return (s > 0.0f && s <= 1.0f) ? s * screen : s;
}

Rect resolve_panel_placement(const PanelPlacement& p, uint32_t screen_w, uint32_t screen_h) {
    float sw = static_cast<float>(screen_w);
    float sh = static_cast<float>(screen_h);
    float w = resolve_size(p.size.x, sw);
    float h = resolve_size(p.size.y, sh);
    float x = 0, y = 0;

    switch (p.anchor) {
        case Anchor::TopLeft:     x = 0;            y = 0;            break;
        case Anchor::Top:         x = (sw - w) / 2; y = 0;            break;
        case Anchor::TopRight:    x = sw - w;        y = 0;            break;
        case Anchor::Left:        x = 0;            y = (sh - h) / 2; break;
        case Anchor::Center:      x = (sw - w) / 2; y = (sh - h) / 2; break;
        case Anchor::Right:       x = sw - w;        y = (sh - h) / 2; break;
        case Anchor::BottomLeft:  x = 0;            y = sh - h;        break;
        case Anchor::Bottom:      x = (sw - w) / 2; y = sh - h;        break;
        case Anchor::BottomRight: x = sw - w;        y = sh - h;        break;
    }

    return {x + p.offset.x, y + p.offset.y, w, h};
}

// --- UIContext stubs (implemented in later tasks) ---

UIContext::UIContext() = default;
UIContext::~UIContext() = default;

void UIContext::begin_frame(std::vector<Event>&, const Renderer&) {}
void UIContext::flush(Renderer&) {}
Rect UIContext::resolve_placement(const PanelPlacement& p) const {
    return resolve_panel_placement(p, screen_w_, screen_h_);
}
void UIContext::draw_panel_bg(Rect) {}
void UIContext::draw_rect(Rect, Color, float) {}
void UIContext::draw_text_at(std::string_view, float, float, Color, float) {}
float UIContext::glyph_width() const { return 8.0f; }
float UIContext::glyph_height() const { return 12.0f; }
void UIContext::register_widget(std::string_view, Rect) {}
bool UIContext::is_hot(std::string_view) const { return false; }
bool UIContext::is_clicked(std::string_view) const { return false; }

// --- PanelBuilder stubs ---

PanelBuilder::PanelBuilder(UIContext& ctx, Rect panel_rect, float z_base)
    : ctx_(ctx), panel_rect_(panel_rect), z_base_(z_base),
      cursor_y_(panel_rect.y + ctx.theme_->padding),
      content_x_(panel_rect.x + ctx.theme_->padding),
      content_width_(panel_rect.w - 2 * ctx.theme_->padding),
      padding_(ctx.theme_->padding), margin_(ctx.theme_->margin) {}

Rect PanelBuilder::next_control_rect(float height) {
    Rect r;
    if (in_horizontal_) {
        r = {horiz_cursor_x_, cursor_y_, content_width_, height};
        horiz_cursor_x_ += content_width_ + margin_;
        horiz_max_h_ = std::max(horiz_max_h_, height);
    } else {
        r = {content_x_, cursor_y_, content_width_, height};
        cursor_y_ += height + margin_;
    }
    return r;
}

float PanelBuilder::text_height() const { return ctx_.glyph_height(); }
float PanelBuilder::measure_text_width(std::string_view text) const {
    return static_cast<float>(text.size()) * ctx_.glyph_width();
}

void PanelBuilder::text(std::string_view, TextStyle) {}
bool PanelBuilder::button(std::string_view, ButtonStyle) { return false; }
void PanelBuilder::checkbox(std::string_view, bool&, CheckboxStyle) {}
void PanelBuilder::list(std::string_view, std::span<const std::string>, int&, ListStyle) {}
bool PanelBuilder::text_input(std::string_view, std::string&, TextInputStyle) { return false; }

// --- Systems stubs ---

void UIInputSystem::update(World&, float) {}
void UIFlushSystem::draw(World&, Renderer&) {}

} // namespace xebble
```

Add `ui.cpp` to `src/CMakeLists.txt`.

Run: `cmake --build build/debug && ctest --test-dir build/debug --output-on-failure`
Expected: All tests pass including placement tests.

**Step 4: Commit**

```bash
git add include/xebble/ui.hpp src/ui.cpp tests/test_ui.cpp tests/CMakeLists.txt src/CMakeLists.txt
git commit -m "feat: add UI types, UIContext skeleton, and panel placement resolution"
```

---

### Task 4: UIContext Implementation — Panels, Text, and Rendering

**Files:**
- Modify: `src/ui.cpp`

Replace all stubs with real implementations for: `begin_frame`, `flush`, panel background rendering, text rendering, and the rect/text drawing primitives.

**Step 1: Implement UIContext core**

Replace the stub implementations in `src/ui.cpp` with the full implementations. The key code:

```cpp
// Helper: is color "use default" (all zeros)?
static bool is_default_color(Color c) {
    return c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0;
}

static Color pick_color(Color style_color, Color theme_color) {
    return is_default_color(style_color) ? theme_color : style_color;
}

// --- UIContext ---

UIContext::UIContext() = default;
UIContext::~UIContext() = default;

void UIContext::begin_frame(std::vector<Event>& events, const Renderer& renderer) {
    // Swap widget rects
    prev_rects_ = std::move(curr_rects_);
    curr_rects_.clear();
    batches_.clear();
    frame_events_ = &events;
    input_chars_.clear();

    screen_w_ = renderer.virtual_width();
    screen_h_ = renderer.virtual_height();

    // Read theme from... we need it set externally
    // theme_ is set by UIInputSystem before this, or in begin_frame

    // Process mouse state from events
    mouse_clicked_ = false;
    for (auto& e : events) {
        switch (e.type) {
            case EventType::MouseMove:
                mouse_pos_ = renderer.screen_to_virtual(e.mouse_move().position);
                break;
            case EventType::MousePress:
                if (e.mouse_button().button == MouseButton::Left) {
                    mouse_down_ = true;
                    mouse_clicked_ = true;
                    mouse_pos_ = renderer.screen_to_virtual(e.mouse_button().position);
                }
                break;
            case EventType::MouseRelease:
                if (e.mouse_button().button == MouseButton::Left) {
                    mouse_down_ = false;
                }
                break;
            case EventType::KeyPress:
            case EventType::KeyRepeat: {
                auto k = e.key().key;
                // Collect printable characters for text input
                if (k >= Key::Space && k <= Key::Z) {
                    char c = static_cast<char>(static_cast<int>(k));
                    if (!e.key().mods.shift && c >= 'A' && c <= 'Z')
                        c += 32; // lowercase
                    input_chars_.push_back(c);
                }
                break;
            }
            default: break;
        }
    }

    // Determine hot widget from previous frame rects
    hot_id_.clear();
    for (auto& wr : prev_rects_) {
        if (mouse_pos_.x >= wr.rect.x && mouse_pos_.x < wr.rect.x + wr.rect.w &&
            mouse_pos_.y >= wr.rect.y && mouse_pos_.y < wr.rect.y + wr.rect.h) {
            hot_id_ = wr.id;
            break; // first match (topmost)
        }
    }
}

void UIContext::flush(Renderer& renderer) {
    for (auto& batch : batches_) {
        if (!batch.instances.empty() && batch.texture) {
            renderer.submit_instances(batch.instances, *batch.texture, batch.z_order);
        }
    }
    batches_.clear();
    frame_events_ = nullptr;
}

Rect UIContext::resolve_placement(const PanelPlacement& p) const {
    return resolve_panel_placement(p, screen_w_, screen_h_);
}

void UIContext::draw_panel_bg(Rect rect) {
    draw_rect(rect, theme_->bg_color, theme_->z_order);
}

void UIContext::draw_rect(Rect rect, Color color, float z) {
    // We need a solid-color texture. Use the font texture with a solid white region.
    // Actually, we can use a glyph that's mostly solid, or we need a 1x1 white texture.
    // Simplest: use the font texture and pick a fully-white pixel's UV.
    // The space character has all transparent pixels, but other characters have white.
    // Better approach: render a filled rect using a tiny UV region from a white pixel.
    // For now, use the font's texture and find a known-white pixel.
    // The '|' character (index 92 in charset, char 124) has a solid vertical line.
    // Actually, let's just get the font texture and use UV (0,0) with tiny size.

    // Best approach: find a solid pixel in the font atlas.
    // The 'M' character (index 45) has row 0x...  lots of white pixels.
    // Use a 1x1 UV from a known white pixel in the atlas.

    // Get font info
    const Texture* tex = nullptr;
    float u = 0, v = 0, uw = 0, vh_val = 0;

    if (auto* bf = std::get_if<const BitmapFont*>(&theme_->font)) {
        if (*bf) {
            tex = &(*bf)->sheet().texture();
            // Use the '#' character (solid pixels at center) — just use a tiny UV
            // Actually, use region of '|' char and take a 1px wide strip
            auto gi = (*bf)->glyph_index('#');
            if (gi) {
                auto r = (*bf)->sheet().region(*gi);
                // Use center pixel of this glyph as our "white" source
                u = r.x + r.w * 0.5f;
                v = r.y + r.h * 0.3f; // somewhere in the middle where pixels are white
                uw = r.w * 0.01f; // tiny UV
                vh_val = r.h * 0.01f;
            }
        }
    } else if (auto* f = std::get_if<const Font*>(&theme_->font)) {
        if (*f) {
            tex = &(*f)->texture();
            // Font atlases typically have white pixels; use a tiny region
            u = 0.0f; v = 0.0f;
            uw = 0.001f; vh_val = 0.001f;
        }
    }

    if (!tex) return;

    SpriteInstance inst{
        .pos_x = rect.x, .pos_y = rect.y,
        .uv_x = u, .uv_y = v, .uv_w = uw, .uv_h = vh_val,
        .quad_w = rect.w, .quad_h = rect.h,
        .r = static_cast<float>(color.r) / 255.0f,
        .g = static_cast<float>(color.g) / 255.0f,
        .b = static_cast<float>(color.b) / 255.0f,
        .a = static_cast<float>(color.a) / 255.0f,
    };
    batches_.push_back({{inst}, tex, z});
}

void UIContext::draw_text_at(std::string_view text, float x, float y, Color color, float z) {
    if (auto* bf = std::get_if<const BitmapFont*>(&theme_->font)) {
        if (!*bf) return;
        auto& font = **bf;
        std::vector<SpriteInstance> glyphs;
        float gw = static_cast<float>(font.glyph_width());
        float gh = static_cast<float>(font.glyph_height());

        for (size_t i = 0; i < text.size(); i++) {
            auto gi = font.glyph_index(text[i]);
            if (!gi) continue;
            auto uv = font.sheet().region(*gi);
            glyphs.push_back({
                .pos_x = x + static_cast<float>(i) * gw,
                .pos_y = y,
                .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                .quad_w = gw, .quad_h = gh,
                .r = static_cast<float>(color.r) / 255.0f,
                .g = static_cast<float>(color.g) / 255.0f,
                .b = static_cast<float>(color.b) / 255.0f,
                .a = static_cast<float>(color.a) / 255.0f,
            });
        }
        if (!glyphs.empty()) {
            batches_.push_back({std::move(glyphs), &font.sheet().texture(), z});
        }
    } else if (auto* f = std::get_if<const Font*>(&theme_->font)) {
        if (!*f) return;
        auto& font = **f;
        std::vector<SpriteInstance> glyphs;
        float cx = x;

        for (char ch : text) {
            auto gm = font.glyph(ch);
            if (!gm) continue;
            glyphs.push_back({
                .pos_x = cx + gm->bearing_x,
                .pos_y = y + (font.line_height() - gm->bearing_y),
                .uv_x = gm->uv.x, .uv_y = gm->uv.y,
                .uv_w = gm->uv.w, .uv_h = gm->uv.h,
                .quad_w = gm->width, .quad_h = gm->height,
                .r = static_cast<float>(color.r) / 255.0f,
                .g = static_cast<float>(color.g) / 255.0f,
                .b = static_cast<float>(color.b) / 255.0f,
                .a = static_cast<float>(color.a) / 255.0f,
            });
            cx += gm->advance;
        }
        if (!glyphs.empty()) {
            batches_.push_back({std::move(glyphs), &font.texture(), z});
        }
    }
}

float UIContext::glyph_width() const {
    if (auto* bf = std::get_if<const BitmapFont*>(&theme_->font)) {
        return *bf ? static_cast<float>((*bf)->glyph_width()) : 8.0f;
    }
    if (auto* f = std::get_if<const Font*>(&theme_->font)) {
        // Approximate: use 'M' width or pixel_size * 0.6
        if (*f) {
            auto gm = (*f)->glyph('M');
            return gm ? gm->advance : 8.0f;
        }
    }
    return 8.0f;
}

float UIContext::glyph_height() const {
    if (auto* bf = std::get_if<const BitmapFont*>(&theme_->font)) {
        return *bf ? static_cast<float>((*bf)->glyph_height()) : 12.0f;
    }
    if (auto* f = std::get_if<const Font*>(&theme_->font)) {
        return *f ? (*f)->line_height() : 12.0f;
    }
    return 12.0f;
}

void UIContext::register_widget(std::string_view id, Rect rect) {
    curr_rects_.push_back({std::string(id), rect});
}

bool UIContext::is_hot(std::string_view id) const {
    return hot_id_ == id;
}

bool UIContext::is_clicked(std::string_view id) const {
    return is_hot(id) && mouse_clicked_;
}
```

**Step 2: Implement PanelBuilder text**

```cpp
void PanelBuilder::text(std::string_view text, TextStyle style) {
    float h = text_height();
    auto r = next_control_rect(h);
    Color color = pick_color(style.color, ctx_.theme_->text_color);
    ctx_.draw_text_at(text, r.x, r.y, color, z_base_ + 0.1f);
}
```

**Step 3: Build and verify**

Run: `cmake --build build/debug && ctest --test-dir build/debug --output-on-failure`
Expected: Compiles and tests pass. Visual testing comes in Task 6.

**Step 4: Commit**

```bash
git add src/ui.cpp
git commit -m "feat: implement UIContext core — panel rendering, text drawing, layout engine"
```

---

### Task 5: Interactive Controls

**Files:**
- Modify: `src/ui.cpp`

Implement button, checkbox, list, and text_input controls.

**Step 1: Implement button**

```cpp
bool PanelBuilder::button(std::string_view label, ButtonStyle style) {
    float h = text_height() + padding_ * 2;
    auto r = next_control_rect(h);

    std::string id = std::string(label); // use label as ID
    ctx_.register_widget(id, r);

    bool hovered = ctx_.is_hot(id);
    bool clicked = ctx_.is_clicked(id);

    Color bg = hovered
        ? pick_color(style.hover_color, ctx_.theme_->button_hover_color)
        : pick_color(style.color, ctx_.theme_->button_color);
    Color text_col = pick_color(style.text_color, ctx_.theme_->button_text_color);

    ctx_.draw_rect(r, bg, z_base_ + 0.05f);
    ctx_.draw_text_at(label, r.x + padding_, r.y + padding_, text_col, z_base_ + 0.1f);

    if (clicked && ctx_.frame_events_) {
        // Consume the mouse click event
        for (auto& e : *ctx_.frame_events_) {
            if (e.type == EventType::MousePress && !e.consumed) {
                e.consumed = true;
                break;
            }
        }
    }

    return clicked;
}
```

**Step 2: Implement checkbox**

```cpp
void PanelBuilder::checkbox(std::string_view label, bool& value, CheckboxStyle style) {
    float h = text_height() + padding_ * 2;
    auto r = next_control_rect(h);

    std::string id = std::string(label);
    ctx_.register_widget(id, r);

    if (ctx_.is_clicked(id)) {
        value = !value;
        if (ctx_.frame_events_) {
            for (auto& e : *ctx_.frame_events_) {
                if (e.type == EventType::MousePress && !e.consumed) {
                    e.consumed = true;
                    break;
                }
            }
        }
    }

    Color bg = value
        ? pick_color(style.checked_color, ctx_.theme_->checkbox_checked_color)
        : pick_color(style.color, ctx_.theme_->checkbox_color);
    Color text_col = pick_color(style.text_color, ctx_.theme_->text_color);

    // Draw check box indicator
    float box_size = text_height();
    Rect box_r = {r.x + padding_, r.y + padding_, box_size, box_size - padding_ * 2};
    ctx_.draw_rect(box_r, bg, z_base_ + 0.05f);
    if (value) {
        ctx_.draw_text_at("X", box_r.x + 1, r.y + padding_, text_col, z_base_ + 0.1f);
    }

    // Draw label
    float text_x = box_r.x + box_size + margin_;
    ctx_.draw_text_at(label, text_x, r.y + padding_, text_col, z_base_ + 0.1f);
}
```

**Step 3: Implement list**

```cpp
void PanelBuilder::list(std::string_view id, std::span<const std::string> items,
                         int& selected, ListStyle style) {
    std::string sid(id);
    float row_h = text_height() + padding_;
    int visible = static_cast<int>(style.visible_rows > 0 ? style.visible_rows : 8);
    float h = static_cast<float>(visible) * row_h + padding_ * 2;
    auto r = next_control_rect(h);

    Color bg = pick_color(style.color, ctx_.theme_->list_color);
    Color sel_bg = pick_color(style.selected_color, ctx_.theme_->list_selected_color);
    Color text_col = pick_color(style.text_color, ctx_.theme_->text_color);

    ctx_.draw_rect(r, bg, z_base_ + 0.05f);

    int& scroll = ctx_.scroll_offsets_[sid];
    scroll = std::clamp(scroll, 0, std::max(0, static_cast<int>(items.size()) - visible));

    // Handle scroll events
    if (ctx_.is_hot(sid) && ctx_.frame_events_) {
        for (auto& e : *ctx_.frame_events_) {
            if (e.type == EventType::MouseScroll && !e.consumed) {
                scroll -= static_cast<int>(e.mouse_scroll().dy);
                scroll = std::clamp(scroll, 0, std::max(0, static_cast<int>(items.size()) - visible));
                e.consumed = true;
            }
        }
    }

    // Draw visible items
    for (int i = 0; i < visible && (scroll + i) < static_cast<int>(items.size()); i++) {
        int item_idx = scroll + i;
        float iy = r.y + padding_ + static_cast<float>(i) * row_h;
        Rect item_r = {r.x + padding_, iy, r.w - padding_ * 2, row_h};

        std::string item_id = sid + ":" + std::to_string(item_idx);
        ctx_.register_widget(item_id, item_r);

        if (item_idx == selected) {
            ctx_.draw_rect(item_r, sel_bg, z_base_ + 0.06f);
        } else if (ctx_.is_hot(item_id)) {
            Color hover = sel_bg;
            hover.a = hover.a / 2; // half-opacity hover
            ctx_.draw_rect(item_r, hover, z_base_ + 0.06f);
        }

        if (ctx_.is_clicked(item_id)) {
            selected = item_idx;
            if (ctx_.frame_events_) {
                for (auto& e : *ctx_.frame_events_) {
                    if (e.type == EventType::MousePress && !e.consumed) {
                        e.consumed = true;
                        break;
                    }
                }
            }
        }

        ctx_.draw_text_at(items[item_idx], item_r.x + padding_, iy + padding_ * 0.5f,
                          text_col, z_base_ + 0.1f);
    }

    // Register the whole list for scroll detection
    ctx_.register_widget(sid, r);
}
```

**Step 4: Implement text_input**

```cpp
bool PanelBuilder::text_input(std::string_view id, std::string& value, TextInputStyle style) {
    std::string sid(id);
    float h = text_height() + padding_ * 2;
    auto r = next_control_rect(h);

    ctx_.register_widget(sid, r);

    bool is_focused = (ctx_.focused_input_id_ == sid);
    bool submitted = false;

    // Click to focus
    if (ctx_.is_clicked(sid)) {
        ctx_.focused_input_id_ = sid;
        is_focused = true;
        ctx_.cursor_positions_[sid] = value.size();
        if (ctx_.frame_events_) {
            for (auto& e : *ctx_.frame_events_) {
                if (e.type == EventType::MousePress && !e.consumed) {
                    e.consumed = true;
                    break;
                }
            }
        }
    }

    // Handle keyboard input when focused
    if (is_focused && ctx_.frame_events_) {
        auto& cursor = ctx_.cursor_positions_[sid];
        if (cursor > value.size()) cursor = value.size();

        for (auto& e : *ctx_.frame_events_) {
            if (e.consumed) continue;
            if (e.type != EventType::KeyPress && e.type != EventType::KeyRepeat) continue;

            auto k = e.key().key;
            if (k == Key::Enter) {
                submitted = true;
                ctx_.focused_input_id_.clear();
                e.consumed = true;
            } else if (k == Key::Backspace) {
                if (cursor > 0 && !value.empty()) {
                    value.erase(cursor - 1, 1);
                    cursor--;
                }
                e.consumed = true;
            } else if (k == Key::Delete) {
                if (cursor < value.size()) {
                    value.erase(cursor, 1);
                }
                e.consumed = true;
            } else if (k == Key::Left) {
                if (cursor > 0) cursor--;
                e.consumed = true;
            } else if (k == Key::Right) {
                if (cursor < value.size()) cursor++;
                e.consumed = true;
            } else if (k == Key::Escape) {
                ctx_.focused_input_id_.clear();
                e.consumed = true;
            } else if (k >= Key::Space && k <= Key::GraveAccent) {
                // Printable character
                char c = static_cast<char>(static_cast<int>(k));
                if (!e.key().mods.shift && c >= 'A' && c <= 'Z')
                    c += 32;
                value.insert(cursor, 1, c);
                cursor++;
                e.consumed = true;
            }
        }
    }

    Color bg = is_focused
        ? pick_color(style.active_color, ctx_.theme_->input_active_color)
        : pick_color(style.color, ctx_.theme_->input_color);
    Color text_col = pick_color(style.text_color, ctx_.theme_->text_color);

    ctx_.draw_rect(r, bg, z_base_ + 0.05f);

    // Draw text with cursor
    std::string display = value;
    if (is_focused) {
        auto cursor = ctx_.cursor_positions_[sid];
        if (cursor > display.size()) cursor = display.size();
        display.insert(cursor, "|");
    }
    ctx_.draw_text_at(display, r.x + padding_, r.y + padding_, text_col, z_base_ + 0.1f);

    return submitted;
}
```

**Step 5: Build and test**

Run: `cmake --build build/debug && ctest --test-dir build/debug --output-on-failure`
Expected: Compiles, all tests pass.

**Step 6: Commit**

```bash
git add src/ui.cpp
git commit -m "feat: implement button, checkbox, list, and text_input UI controls"
```

---

### Task 6: Systems, Auto-Registration, and Umbrella Header

**Files:**
- Modify: `src/ui.cpp`
- Modify: `src/game.cpp`
- Modify: `include/xebble/xebble.hpp`

Wire up UIInputSystem, UIFlushSystem, embedded font, and default theme in `run()`.

**Step 1: Implement UIInputSystem and UIFlushSystem**

In `src/ui.cpp`, replace the system stubs:

```cpp
void UIInputSystem::update(World& world, float) {
    auto& events = world.resource<EventQueue>().events;
    auto* renderer = world.resource<Renderer*>();
    auto& ui = world.resource<UIContext>();

    // Set the theme pointer
    if (world.has_resource<UITheme>()) {
        ui.theme_ = &world.resource<UITheme>();
    }

    ui.begin_frame(events, *renderer);
}

void UIFlushSystem::draw(World& world, Renderer& renderer) {
    auto& ui = world.resource<UIContext>();
    ui.flush(renderer);
}
```

**Step 2: Update run() for auto-registration**

In `src/game.cpp`, add includes:

```cpp
#include <xebble/ui.hpp>
#include <xebble/embedded_font.hpp>
```

After the existing auto-registration block (after the `SpriteRenderSystem` line), add:

```cpp
    // --- UI system auto-registration ---

    // Create embedded font and default theme if user hasn't provided one
    std::unique_ptr<BitmapFont> embedded_font_ptr;
    if (!world.has_resource<UITheme>()) {
        auto font_result = embedded_font::create_font(renderer->context());
        if (!font_result) {
            log(LogLevel::Error, "Failed to create embedded font: " + font_result.error().message);
            return 1;
        }
        embedded_font_ptr = std::move(*font_result);

        UITheme default_theme;
        default_theme.font = embedded_font_ptr.get();
        world.add_resource<UITheme>(default_theme);
    }

    // Store the embedded font so it outlives the game loop
    // (put it in a resource so it's kept alive)
    struct EmbeddedFontStorage {
        std::unique_ptr<BitmapFont> font;
    };
    if (embedded_font_ptr) {
        world.add_resource<EmbeddedFontStorage>({std::move(embedded_font_ptr)});
    }

    // Add UIContext resource
    world.add_resource<UIContext>(UIContext{});

    // Prepend UIInputSystem (runs before user systems in update)
    world.prepend_system<UIInputSystem>();

    // Append UIFlushSystem (runs after all render systems in draw)
    world.add_system<UIFlushSystem>();
```

**Step 3: Add to umbrella header**

In `include/xebble/xebble.hpp`, add after `#include <xebble/builtin_systems.hpp>`:

```cpp
#include <xebble/ui.hpp>
```

**Step 4: Build and test**

Run: `cmake --build build/debug && ctest --test-dir build/debug --output-on-failure`
Expected: All tests pass.

**Step 5: Commit**

```bash
git add src/ui.cpp src/game.cpp include/xebble/xebble.hpp
git commit -m "feat: wire up UI systems, auto-register embedded font and default theme in run()"
```

---

### Task 7: Rewrite Example HudSystem

**Files:**
- Modify: `examples/basic_tilemap/main.cpp`

Replace the manual sprite instance construction in HudSystem with UIContext calls.

**Step 1: Rewrite HudSystem**

Replace the entire HudSystem class with:

```cpp
class HudSystem : public xebble::System {
public:
    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ui = world.resource<xebble::UIContext>();
        auto& state = world.resource<GameState>();

        int player_x = 0, player_y = 0;
        world.each<PlayerTag, xebble::Position>([&](xebble::Entity, PlayerTag&, xebble::Position& pos) {
            player_x = static_cast<int>(pos.x) / TILE_SIZE;
            player_y = static_cast<int>(pos.y) / TILE_SIZE;
        });

        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 32}}, [&](auto& p) {
            p.text(std::format("Pos:({},{}) Items:{} [R]egen [Esc]ape",
                               player_x, player_y, state.items_collected),
                   {.color = {255, 255, 150}});
            if (!state.message.empty())
                p.text(state.message, {.color = {200, 200, 200}});
        });
    }
};
```

Remove the `stb_image_write.h` include and `font_gen.hpp` include since HudSystem no longer uses them directly. However, `font_gen.hpp` is still used for the bitmap font generation in `main()`. Keep it.

Remove the `tile_sheet_` and `font_` members and the `init()` override — they're no longer needed.

**Step 2: Clean up main()**

In `main()`, the font generation and manifest writing can stay (the example still needs a tileset). But you can remove the bitmap font from the manifest if the HUD now uses the embedded font. Actually, keep it — other systems might want it.

**Step 3: Build and test**

Run: `cmake --build build/debug && ctest --test-dir build/debug --output-on-failure`
Expected: Compiles, all tests pass.

Run the app manually to verify: HUD renders at the bottom with the embedded font.

**Step 4: Commit**

```bash
git add examples/basic_tilemap/main.cpp
git commit -m "refactor: rewrite HudSystem to use UIContext — no manual sprite instances"
```
