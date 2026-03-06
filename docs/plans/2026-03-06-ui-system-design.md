# UI System Design

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:writing-plans to create implementation plan from this design.

**Goal:** Immediate-mode UI system for xebble so games can draw HUDs, panels, menus, popups, settings, and load/save screens with zero custom rendering code.

**Architecture:** `UIContext` resource with imgui-style immediate-mode API. Systems declare panels and controls in `draw()`. Input processed against previous frame's layout. Auto-registered by `run()` with embedded default font.

---

## Core Architecture

`UIContext` is a resource, not a system. Any system grabs it from the world and declares UI in its `draw()` method. Two built-in systems handle the lifecycle:

- **`UIInputSystem`** — update system, runs before user systems. Processes events against previous frame's widget layout rects. Marks consumed events.
- **`UIFlushSystem`** — draw system, runs after all other draw systems. Submits accumulated UI sprite instances to the renderer.

### Frame Lifecycle

1. `UIInputSystem::update()` — processes input against last frame's layout, marks consumed events
2. User update systems — see filtered events (consumed events skipped)
3. User draw systems — use `UIContext` to declare panels/controls
4. Built-in render systems — tilemap, sprites
5. `UIFlushSystem::draw()` — submits all UI visuals on top

---

## Panel Positioning

Panels use anchor + size + offset. No manual rects needed.

```cpp
enum class Anchor {
    TopLeft, Top, TopRight,
    Left, Center, Right,
    BottomLeft, Bottom, BottomRight,
};

struct PanelPlacement {
    Anchor anchor = Anchor::TopLeft;
    Vec2 size;        // 0.0-1.0 = fraction of screen, >1.0 = pixels
    Vec2 offset = {}; // pixel displacement from anchor point
};
```

The anchor determines which screen point the panel attaches to and which corner of the panel aligns there. `Anchor::TopRight` with offset `{-8, 8}` places the panel's top-right corner 8px inward from the screen's top-right corner.

---

## Interior Layout

Automatic vertical stack inside panels.

- Controls flow top-to-bottom, each taking full panel width
- `p.horizontal([&](auto& row) { ... })` switches to left-to-right for that row
- Controls in horizontal groups split available width equally by default; a control can request a fixed width
- Padding (inside control) and margin (between controls) default from `UITheme`, overridable per-control

---

## Controls (v1)

All controls are methods on the panel context:

```cpp
// Text — static label
p.text("HP: 42/50");
p.text("DANGER", {.color = {255, 0, 0}});

// Button — returns true on click
if (p.button("Inventory")) { ... }

// Checkbox — toggles a bool reference
p.checkbox("Fullscreen", fullscreen_enabled);

// List — scrollable, selects into index reference
p.list("saves", save_names, selected_index);

// Text input — edits string reference, returns true on Enter
if (p.text_input("name", player_name)) { ... }
```

Widget identity uses string IDs (first argument to interactive controls). The `UIContext` uses these to track hot/active state across frames.

---

## Style Overrides

Optional trailing structs on control calls. Only specify what differs from the theme:

```cpp
struct TextStyle { Color color; };
struct ButtonStyle { Color color, hover_color, text_color; };
```

---

## Input Consumption

`UIInputSystem` runs before user update systems. It processes the `EventQueue` against the previous frame's widget rects (same one-frame-latency pattern as imgui — imperceptible at 60fps).

When a control consumes an event (button click, text input keystroke), the event is marked `consumed = true`. Game input systems check this flag and skip consumed events.

The `Event` struct gains a `bool consumed = false` field.

---

## UITheme

```cpp
struct UITheme {
    std::variant<const BitmapFont*, const Font*> font;
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
```

Default theme auto-registered by `run()` with an embedded 8x12 pixel font. User can override by adding their own `UITheme` resource before calling `run()`.

---

## Embedded Default Font

An 8x12 pixel font embedded as a `constexpr` byte array (public domain CP437-style). `run()` creates a texture and `BitmapFont` from it automatically. No file dependencies.

---

## Auto-registration in run()

`run()` performs these steps:

1. Creates embedded font texture and `BitmapFont`
2. Adds default `UITheme` if user hasn't provided one (using the embedded font)
3. Creates and adds `UIContext` resource
4. Prepends `UIInputSystem` before user systems
5. Appends `UIFlushSystem` after all render systems

System registration order:
1. `UIInputSystem` (update only)
2. User systems
3. `TileMapRenderSystem` (draw only)
4. `SpriteRenderSystem` (draw only)
5. `UIFlushSystem` (draw only)

---

## Files

New:
- `include/xebble/ui.hpp` — Anchor, PanelPlacement, UITheme, UIContext, PanelBuilder, style structs
- `include/xebble/embedded_font.hpp` — constexpr byte array for default 8x12 pixel font
- `src/ui.cpp` — UIContext implementation, layout engine, control rendering
- `src/embedded_font.cpp` — font data (if too large for constexpr header)

Modified:
- `include/xebble/event.hpp` — add `bool consumed` field to Event
- `src/game.cpp` — auto-register UIContext, UITheme, UIInputSystem, UIFlushSystem
- `include/xebble/xebble.hpp` — add ui.hpp to umbrella header

---

## Example Impact

The roguelike demo's `HudSystem` replaces manual `SpriteInstance` construction with:

```cpp
void draw(xebble::World& world, xebble::Renderer& renderer) override {
    auto& ui = world.resource<xebble::UIContext>();
    auto& state = world.resource<GameState>();

    ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 40}}, [&](auto& p) {
        p.text(std::format("Pos:({},{}) Items:{}", player_x, player_y, state.items_collected),
               {.color = {255, 255, 150}});
        p.text(state.message);
    });
}
```
