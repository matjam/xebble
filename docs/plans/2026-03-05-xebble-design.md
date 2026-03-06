# Xebble Design Document

A C++ 2D rendering API for cross-platform Vulkan roguelike games.

## Overview

Xebble provides efficient 2D rendering for roguelike games using Vulkan. It handles spritesheet-based tilemaps, animated sprites, text rendering (bitmap and TrueType), pixel-accurate scaling with DPI awareness, asset management, and a structured game loop.

## Platform Support

- Windows (native Vulkan)
- macOS (Vulkan via MoltenVK)
- Linux (Wayland + X11 via GLFW)

## Technology Stack

- **Language:** C++23 (C++20 baseline)
- **Build:** CMake 4.2.3, vcpkg
- **Windowing:** GLFW
- **Graphics:** Vulkan, VMA (Vulkan Memory Allocator)
- **Image loading:** stb_image
- **Fonts:** FreeType (TrueType), spritesheet-based bitmap fonts
- **Shader compilation:** shaderc (via vcpkg, at build time)
- **Math:** GLM
- **Config:** toml++ (asset manifests)
- **Archive:** miniz or libzip (ZIP-based asset archives)

## Core Design Principles

- Modern C++23, RAII everywhere, value semantics
- No raw pointers in the public API
- `std::expected<T, Error>` for fallible construction
- Move-only types for GPU resource owners
- Consumer works in virtual pixel coordinates — library handles all DPI/scaling

## Architecture: Single-Pass Batched Renderer

### Rendering Pipeline

1. `begin_frame()` — acquire swapchain image, begin command buffer, update delta time
2. **Offscreen pass** (renders to virtual-resolution framebuffer):
   - Tilemap layers rendered back-to-front as instanced quads (one draw call per layer)
   - Sprites sorted by z-order, batched by texture, drawn as instanced quads
   - Text glyphs rendered as sprite-like quads from font atlases
3. **Blit pass** (scales to swapchain):
   - Fullscreen quad samples offscreen texture with nearest-neighbor filtering
   - Letterboxing/pillarboxing for aspect ratio mismatch
4. `end_frame()` — submit command buffer, present

### Shaders

- `sprite.vert` / `sprite.frag` — shared for tiles, sprites, and text glyphs
- `blit.vert` / `blit.frag` — fullscreen quad for pixel-perfect scaling

### Buffer Strategy

- Tile instance buffer: updated per-frame with visible tiles only (camera culling). 16 bytes per tile (vec2 position, vec2 UV).
- Sprite instance buffer: rebuilt per-frame, sorted by z-order, batched by texture.
- Uniform buffer: projection matrix + per-frame data. Double-buffered for frames in flight.
- All allocations via VMA.

### Vulkan Internals

- Double-buffered (2 frames in flight)
- Automatic swapchain recreation on resize or `VK_ERROR_OUT_OF_DATE_KHR`
- Offscreen framebuffer recreated independently on virtual resolution change
- RAII wrappers for all Vulkan handles

## Public API

### Window

```cpp
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
    bool should_close() const;
    void poll_events();
    std::span<const Event> events() const;
    float content_scale() const;
    std::pair<uint32_t, uint32_t> framebuffer_size() const;
    std::pair<uint32_t, uint32_t> window_size() const;
};
```

### Events

```cpp
enum class EventType {
    KeyPress, KeyRelease, KeyRepeat,
    MousePress, MouseRelease,
    MouseMove, MouseScroll,
    WindowResize, WindowFocusGained, WindowFocusLost, WindowClose
};

struct KeyData { Key key; Modifiers mods; };
struct MouseButtonData { MouseButton button; Modifiers mods; Vec2 position; };
struct MouseMoveData { Vec2 position; };
struct MouseScrollData { float dx, dy; };
struct ResizeData { uint32_t width, height; };

class Event {
public:
    EventType type;
    const KeyData& key() const;
    const MouseButtonData& mouse_button() const;
    const MouseMoveData& mouse_move() const;
    const MouseScrollData& mouse_scroll() const;
    const ResizeData& resize() const;
};
```

Mouse positions are reported in virtual pixels. The library maps from screen coordinates through DPI scale and viewport transform.

### Renderer

```cpp
struct RendererConfig {
    uint32_t virtual_width = 640;
    uint32_t virtual_height = 360;
    bool vsync = true;
    bool nearest_filter = true;
};

struct Resolution {
    uint32_t width, height;
    std::string label;
};

class Renderer {
public:
    static std::expected<Renderer, Error> create(Window& window, const RendererConfig& config);
    std::vector<Resolution> available_resolutions() const;
    void set_virtual_resolution(uint32_t width, uint32_t height);
    Resolution current_resolution() const;
    bool begin_frame();
    void end_frame();
    void draw(const TileMap& tilemap);
    void draw(const Sprite& sprite);
    void draw(std::span<const Sprite> sprites);
    void draw(const TextBlock& text);
    void set_border_color(Color color);
    float delta_time() const;
    float elapsed_time() const;
    uint64_t frame_count() const;
};
```

### SpriteSheet & Sprites

```cpp
class SpriteSheet {
public:
    static std::expected<SpriteSheet, Error> load(
        Renderer& renderer,
        const std::filesystem::path& image_path,
        uint32_t tile_width, uint32_t tile_height);
    Rect region(uint32_t col, uint32_t row) const;
    Rect region(uint32_t index) const;
    uint32_t columns() const;
    uint32_t rows() const;
    uint32_t tile_width() const;
    uint32_t tile_height() const;
};

struct AnimationDef {
    std::vector<uint32_t> frames;
    float frame_duration;
    bool looping = true;
};

struct Sprite {
    Vec2 position;
    float z_order = 0.0f;
    const SpriteSheet* sheet;
    std::variant<uint32_t, AnimationDef> source;
};
```

Sprite animations are advanced automatically by the renderer using `delta_time()` when `draw()` is called.

### TileMap

```cpp
class TileMap {
public:
    static std::expected<TileMap, Error> create(
        Renderer& renderer, const SpriteSheet& sheet,
        uint32_t width, uint32_t height, uint32_t layer_count);
    void set_tile(uint32_t layer, uint32_t x, uint32_t y, uint32_t tile_index);
    void set_tile(uint32_t layer, uint32_t x, uint32_t y, std::nullopt_t);
    std::optional<uint32_t> tile_at(uint32_t layer, uint32_t x, uint32_t y) const;
    void set_layer(uint32_t layer, std::span<const uint32_t> tile_indices);
    void clear_layer(uint32_t layer);
    void set_offset(Vec2 offset);
    Vec2 offset() const;
    uint32_t layer_count() const;
    uint32_t width() const;
    uint32_t height() const;
};
```

Arbitrary N layers, rendered back-to-front with alpha blending.

### Fonts & Text

```cpp
class BitmapFont {
public:
    static std::expected<BitmapFont, Error> load(
        Renderer& renderer,
        const std::filesystem::path& image_path,
        uint32_t glyph_width, uint32_t glyph_height,
        std::string_view charset);
};

class Font {
public:
    static std::expected<Font, Error> load(
        Renderer& renderer,
        const std::filesystem::path& font_path,
        uint32_t pixel_size);
};

struct TextBlock {
    std::string text;
    Vec2 position;
    float z_order = 0.0f;
    Color color = {255, 255, 255, 255};
    std::variant<const BitmapFont*, const Font*> font;
};
```

### Asset Manager

```cpp
struct AssetConfig {
    std::filesystem::path directory;
    std::filesystem::path archive;
    std::filesystem::path manifest;
};

class AssetManager {
public:
    static std::expected<AssetManager, Error> create(
        Renderer& renderer, const AssetConfig& config);

    template<typename T>
    const T& get(std::string_view name) const;

    bool has(std::string_view name) const;

    std::expected<std::vector<uint8_t>, Error> read_raw(std::string_view path) const;
};
```

- Parses a TOML manifest that maps logical names to files and their loading parameters
- Directory checked first, then ZIP archive fallback (loose files override packed ones)
- All assets loaded on `create()` and cached
- `read_raw()` for custom asset types

#### Manifest Format (TOML)

```toml
[spritesheets.dungeon_tiles]
path = "sprites/dungeon.png"
tile_width = 16
tile_height = 16

[bitmap_fonts.default]
path = "fonts/cp437.png"
glyph_width = 8
glyph_height = 8
charset = " !\"#$%&'()*+,-./0123456789..."

[fonts.ui]
path = "fonts/roboto.ttf"
pixel_size = 16
```

### Game Loop Framework

```cpp
class Game {
public:
    virtual ~Game() = default;
    virtual void init(Renderer& renderer, AssetManager& assets) = 0;
    virtual void update(float dt) = 0;
    virtual void draw(Renderer& renderer) = 0;
    virtual void layout(uint32_t width, uint32_t height) = 0;
    virtual void on_event(const Event& event) {}
    virtual void shutdown() {}
};

struct GameConfig {
    WindowConfig window;
    RendererConfig renderer;
    AssetConfig assets;
    float fixed_timestep = 1.0f / 60.0f;
};

int run(std::unique_ptr<Game> game, const GameConfig& config);
```

The `run()` function:
1. Creates Window, Renderer, AssetManager
2. Calls `game->init()`
3. Runs the loop: poll events -> dispatch `on_event()` -> accumulate time -> `update(dt)` at fixed timestep -> `begin_frame()` -> `draw()` -> `end_frame()`
4. Calls `game->shutdown()`
5. RAII cleanup

Fixed timestep for `update()`, vsync'd or uncapped for `draw()`. Window/Renderer/AssetManager remain usable standalone for consumers who want full control.

### Common Types

```cpp
struct Vec2 { float x = 0, y = 0; };
struct Rect { float x, y, w, h; };
struct Color { uint8_t r, g, b, a; };
enum class Key { /* GLFW key mappings */ };
enum class MouseButton { Left, Right, Middle };
enum class Action { Press, Release, Repeat };
struct Modifiers { bool shift, ctrl, alt, super; };
struct Error { std::string message; };
```

### Logging

User-configurable log callback for library diagnostics:

```cpp
void set_log_callback(std::function<void(LogLevel, std::string_view)> callback);
```

## Project Structure

```
xebble/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── include/xebble/
│   ├── xebble.hpp
│   ├── renderer.hpp
│   ├── window.hpp
│   ├── tilemap.hpp
│   ├── spritesheet.hpp
│   ├── sprite.hpp
│   ├── font.hpp
│   ├── texture.hpp
│   ├── asset_manager.hpp
│   ├── game.hpp
│   ├── event.hpp
│   └── types.hpp
├── src/
│   ├── CMakeLists.txt
│   ├── renderer.cpp
│   ├── window.cpp
│   ├── tilemap.cpp
│   ├── spritesheet.cpp
│   ├── sprite.cpp
│   ├── font.cpp
│   ├── texture.cpp
│   ├── asset_manager.cpp
│   ├── game.cpp
│   └── vulkan/
│       ├── context.hpp/cpp
│       ├── swapchain.hpp/cpp
│       ├── pipeline.hpp/cpp
│       ├── buffer.hpp/cpp
│       ├── descriptor.hpp/cpp
│       └── command.hpp/cpp
├── shaders/
│   ├── CMakeLists.txt
│   ├── sprite.vert
│   ├── sprite.frag
│   ├── blit.vert
│   └── blit.frag
├── examples/
│   ├── CMakeLists.txt
│   └── basic_tilemap/
│       └── main.cpp
└── docs/
    └── plans/
```

## Out of Scope (Initial)

- Audio
- Gamepad input
- Hot-reloading
- Post-processing effects
- Per-layer blend modes
- C API wrapper
