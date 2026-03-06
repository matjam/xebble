# Xebble Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Scaffold the Xebble C++ 2D Vulkan rendering API from an empty repo to a working example that opens a window, renders a tilemap with sprites and text, handles input, and scales pixel-perfectly.

**Architecture:** Single-pass batched Vulkan renderer. Offscreen framebuffer at virtual resolution, instanced quad rendering for tiles/sprites/text, nearest-neighbor blit to swapchain. GLFW for windowing, VMA for memory, asset manager with TOML manifests and ZIP archive support.

**Tech Stack:** C++23, CMake 4.2.3, vcpkg (at ~/vcpkg), Vulkan, GLFW, VMA, stb_image, FreeType, shaderc, GLM, toml++, minizip-ng. macOS via MoltenVK.

**Testing Strategy:** Unit tests for data structures, coordinate math, asset manifest parsing, and event queues. GPU/rendering code verified via the example app (Vulkan is not unit-testable in a meaningful way). Use Google Test via vcpkg.

**Important Notes:**
- vcpkg is installed at `~/vcpkg` — the toolchain file is `~/vcpkg/scripts/buildsystems/vcpkg.cmake`
- macOS arm64, CMake 4.2.3 already installed
- `glslc` is NOT installed system-wide — shaderc comes via vcpkg and we use its `glslc` or link `shaderc` as a library for build-time compilation
- All public API headers go in `include/xebble/`, all implementation in `src/`
- C++23 with C++20 as minimum baseline
- RAII everywhere, `std::expected<T, Error>` for fallible construction, move-only GPU resource types

---

## Task 1: Build System — CMake, vcpkg, Presets

**Files:**
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `vcpkg.json`
- Create: `src/CMakeLists.txt`
- Create: `shaders/CMakeLists.txt`
- Create: `examples/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `.gitignore`

**Step 1: Create `.gitignore`**

```gitignore
build/
out/
.cache/
.vs/
.vscode/
*.swp
*.swo
*~
.DS_Store
compile_commands.json
```

**Step 2: Create `vcpkg.json`**

```json
{
  "name": "xebble",
  "version-string": "0.1.0",
  "description": "2D rendering API for cross-platform Vulkan roguelike games",
  "dependencies": [
    "glfw3",
    "vulkan-headers",
    "vulkan-loader",
    "vulkan-memory-allocator",
    "stb",
    "freetype",
    "shaderc",
    "glm",
    "tomlplusplus",
    "minizip-ng",
    "gtest"
  ]
}
```

**Step 3: Create `CMakePresets.json`**

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "default",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "$env{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake",
        "CMAKE_CXX_STANDARD": "23",
        "CMAKE_CXX_STANDARD_REQUIRED": "ON",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "debug",
      "inherits": "default",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    },
    {
      "name": "release",
      "inherits": "default",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "debug",
      "configurePreset": "debug"
    },
    {
      "name": "release",
      "configurePreset": "release"
    }
  ],
  "testPresets": [
    {
      "name": "debug",
      "configurePreset": "debug",
      "output": { "outputOnFailure": true }
    }
  ]
}
```

**Step 4: Create root `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.25)
project(xebble VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(minizip-ng CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# shaderc for shader compilation
find_package(unofficial-shaderc CONFIG REQUIRED)

add_subdirectory(shaders)
add_subdirectory(src)
add_subdirectory(examples)

enable_testing()
add_subdirectory(tests)
```

**Step 5: Create `src/CMakeLists.txt`**

Start with a minimal library target and a single placeholder source file.

```cmake
add_library(xebble STATIC
    placeholder.cpp
)

target_include_directories(xebble PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(xebble PUBLIC
    Vulkan::Vulkan
    glfw
    GPUOpen::VulkanMemoryAllocator
    freetype
    glm::glm
    tomlplusplus::tomlplusplus
    MINIZIP::minizip-ng
)

# stb is header-only, add include path
target_include_directories(xebble PRIVATE ${Stb_INCLUDE_DIR})

target_compile_features(xebble PUBLIC cxx_std_23)
```

Create `src/placeholder.cpp`:
```cpp
// Placeholder — will be replaced by actual source files
namespace xebble {}
```

**Step 6: Create `shaders/CMakeLists.txt`**

```cmake
# Shader compilation using glslc from shaderc
find_program(GLSLC glslc HINTS ${shaderc_DIR}/../../tools/shaderc)

if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found — required for shader compilation")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(SHADERS
    sprite.vert
    sprite.frag
    blit.vert
    blit.frag
)

set(SPIRV_OUTPUTS "")
foreach(SHADER ${SHADERS})
    set(SHADER_SRC ${SHADER_SOURCE_DIR}/${SHADER})
    set(SHADER_SPV ${SHADER_BINARY_DIR}/${SHADER}.spv)
    add_custom_command(
        OUTPUT ${SHADER_SPV}
        COMMAND ${GLSLC} ${SHADER_SRC} -o ${SHADER_SPV}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling shader ${SHADER}"
    )
    list(APPEND SPIRV_OUTPUTS ${SHADER_SPV})
endforeach()

add_custom_target(xebble_shaders ALL DEPENDS ${SPIRV_OUTPUTS})
```

Create placeholder shaders so the build doesn't fail:

`shaders/sprite.vert`:
```glsl
#version 450

layout(location = 0) out vec2 fragTexCoord;

void main() {
    fragTexCoord = vec2(0.0);
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
```

`shaders/sprite.frag`:
```glsl
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0, 0.0, 1.0, 1.0);
}
```

`shaders/blit.vert`:
```glsl
#version 450

layout(location = 0) out vec2 fragTexCoord;

void main() {
    fragTexCoord = vec2(0.0);
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
```

`shaders/blit.frag`:
```glsl
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(0.0);
}
```

**Step 7: Create `examples/CMakeLists.txt`**

```cmake
add_executable(basic_tilemap basic_tilemap/main.cpp)
target_link_libraries(basic_tilemap PRIVATE xebble)
add_dependencies(basic_tilemap xebble_shaders)
```

Create `examples/basic_tilemap/main.cpp`:
```cpp
#include <cstdio>

int main() {
    std::printf("Xebble basic_tilemap example — placeholder\n");
    return 0;
}
```

**Step 8: Create `tests/CMakeLists.txt`**

```cmake
add_executable(xebble_tests
    test_types.cpp
)

target_link_libraries(xebble_tests PRIVATE
    xebble
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(xebble_tests)
```

Create `tests/test_types.cpp`:
```cpp
#include <gtest/gtest.h>

TEST(Placeholder, CompileCheck) {
    EXPECT_TRUE(true);
}
```

**Step 9: Configure and build to verify everything links**

Run:
```bash
cmake --preset debug
cmake --build build/debug
```

Expected: Clean build, all dependencies resolved via vcpkg.

**Step 10: Run tests**

Run:
```bash
ctest --preset debug
```

Expected: 1 test passes.

**Step 11: Commit**

```bash
git add -A
git commit -m "feat: scaffold build system with CMake, vcpkg, and shaderc shader compilation"
```

---

## Task 2: Common Types and Logging

**Files:**
- Create: `include/xebble/types.hpp`
- Create: `include/xebble/log.hpp`
- Create: `src/log.cpp`
- Modify: `src/CMakeLists.txt` — add `log.cpp`
- Modify: `tests/CMakeLists.txt` — add `test_types.cpp`
- Modify: `tests/test_types.cpp` — replace placeholder

**Step 1: Write tests for types**

Replace `tests/test_types.cpp`:

```cpp
#include <gtest/gtest.h>
#include <xebble/types.hpp>

using namespace xebble;

TEST(Vec2, DefaultConstruction) {
    Vec2 v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
}

TEST(Vec2, AggregateInit) {
    Vec2 v{3.0f, 4.0f};
    EXPECT_FLOAT_EQ(v.x, 3.0f);
    EXPECT_FLOAT_EQ(v.y, 4.0f);
}

TEST(Rect, AggregateInit) {
    Rect r{1.0f, 2.0f, 16.0f, 16.0f};
    EXPECT_FLOAT_EQ(r.x, 1.0f);
    EXPECT_FLOAT_EQ(r.w, 16.0f);
}

TEST(Color, DefaultWhite) {
    Color c{255, 255, 255, 255};
    EXPECT_EQ(c.r, 255);
    EXPECT_EQ(c.a, 255);
}

TEST(Error, Message) {
    Error e{"something went wrong"};
    EXPECT_EQ(e.message, "something went wrong");
}
```

**Step 2: Run tests, verify they fail (types.hpp doesn't exist)**

Run: `cmake --build build/debug 2>&1 | head -20`
Expected: Compilation error — `xebble/types.hpp` not found.

**Step 3: Create `include/xebble/types.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace xebble {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
};

struct Error {
    std::string message;
};

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

enum class MouseButton {
    Left = 0,
    Right = 1,
    Middle = 2,
};

struct Modifiers {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
};

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

} // namespace xebble
```

**Step 4: Create `include/xebble/log.hpp`**

```cpp
#pragma once

#include <xebble/types.hpp>
#include <functional>
#include <string_view>

namespace xebble {

using LogCallback = std::function<void(LogLevel, std::string_view)>;

void set_log_callback(LogCallback callback);
void log(LogLevel level, std::string_view message);

} // namespace xebble
```

**Step 5: Create `src/log.cpp`**

```cpp
#include <xebble/log.hpp>
#include <cstdio>

namespace xebble {

namespace {
    LogCallback g_log_callback = [](LogLevel level, std::string_view msg) {
        const char* prefix = "???";
        switch (level) {
            case LogLevel::Debug: prefix = "DEBUG"; break;
            case LogLevel::Info:  prefix = "INFO";  break;
            case LogLevel::Warn:  prefix = "WARN";  break;
            case LogLevel::Error: prefix = "ERROR"; break;
        }
        std::fprintf(stderr, "[xebble:%s] %.*s\n", prefix,
                     static_cast<int>(msg.size()), msg.data());
    };
}

void set_log_callback(LogCallback callback) {
    g_log_callback = std::move(callback);
}

void log(LogLevel level, std::string_view message) {
    if (g_log_callback) {
        g_log_callback(level, message);
    }
}

} // namespace xebble
```

**Step 6: Update `src/CMakeLists.txt`**

Replace `placeholder.cpp` with `log.cpp` in the source list. Remove `placeholder.cpp` file.

**Step 7: Build and run tests**

Run:
```bash
cmake --build build/debug
ctest --preset debug
```

Expected: All type tests pass.

**Step 8: Commit**

```bash
git add -A
git commit -m "feat: add common types (Vec2, Rect, Color, Key, etc.) and logging system"
```

---

## Task 3: Event System

**Files:**
- Create: `include/xebble/event.hpp`
- Create: `tests/test_event.cpp`
- Modify: `tests/CMakeLists.txt` — add `test_event.cpp`

**Step 1: Write tests for events**

Create `tests/test_event.cpp`:

```cpp
#include <gtest/gtest.h>
#include <xebble/event.hpp>

using namespace xebble;

TEST(Event, KeyPressEvent) {
    Event e = Event::key_press(Key::A, {.shift = true});
    EXPECT_EQ(e.type, EventType::KeyPress);
    EXPECT_EQ(e.key().key, Key::A);
    EXPECT_TRUE(e.key().mods.shift);
}

TEST(Event, MouseMoveEvent) {
    Event e = Event::mouse_move({100.0f, 200.0f});
    EXPECT_EQ(e.type, EventType::MouseMove);
    EXPECT_FLOAT_EQ(e.mouse_move().position.x, 100.0f);
    EXPECT_FLOAT_EQ(e.mouse_move().position.y, 200.0f);
}

TEST(Event, MouseButtonEvent) {
    Event e = Event::mouse_press(MouseButton::Left, {}, {50.0f, 60.0f});
    EXPECT_EQ(e.type, EventType::MousePress);
    EXPECT_EQ(e.mouse_button().button, MouseButton::Left);
    EXPECT_FLOAT_EQ(e.mouse_button().position.x, 50.0f);
}

TEST(Event, ScrollEvent) {
    Event e = Event::mouse_scroll(0.0f, -3.0f);
    EXPECT_EQ(e.type, EventType::MouseScroll);
    EXPECT_FLOAT_EQ(e.mouse_scroll().dy, -3.0f);
}

TEST(Event, ResizeEvent) {
    Event e = Event::window_resize(1920, 1080);
    EXPECT_EQ(e.type, EventType::WindowResize);
    EXPECT_EQ(e.resize().width, 1920u);
    EXPECT_EQ(e.resize().height, 1080u);
}

TEST(EventQueue, PushAndIterate) {
    std::vector<Event> queue;
    queue.push_back(Event::key_press(Key::W, {}));
    queue.push_back(Event::key_press(Key::A, {}));
    queue.push_back(Event::mouse_move({10.0f, 20.0f}));

    EXPECT_EQ(queue.size(), 3u);

    int key_count = 0;
    for (auto& e : queue) {
        switch (e.type) {
            case EventType::KeyPress: key_count++; break;
            default: break;
        }
    }
    EXPECT_EQ(key_count, 2);
}
```

**Step 2: Run tests, verify failure**

Expected: `xebble/event.hpp` not found.

**Step 3: Create `include/xebble/event.hpp`**

```cpp
#pragma once

#include <xebble/types.hpp>
#include <variant>

namespace xebble {

enum class EventType {
    KeyPress, KeyRelease, KeyRepeat,
    MousePress, MouseRelease,
    MouseMove, MouseScroll,
    WindowResize, WindowFocusGained, WindowFocusLost, WindowClose,
};

struct KeyData {
    Key key;
    Modifiers mods;
};

struct MouseButtonData {
    MouseButton button;
    Modifiers mods;
    Vec2 position;
};

struct MouseMoveData {
    Vec2 position;
};

struct MouseScrollData {
    float dx = 0.0f;
    float dy = 0.0f;
};

struct ResizeData {
    uint32_t width = 0;
    uint32_t height = 0;
};

class Event {
public:
    EventType type;

    // Factory methods
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

    // Typed accessors
    const KeyData& key() const { return std::get<KeyData>(data_); }
    const MouseButtonData& mouse_button() const { return std::get<MouseButtonData>(data_); }
    const MouseMoveData& mouse_move() const { return std::get<MouseMoveData>(data_); }
    const MouseScrollData& mouse_scroll() const { return std::get<MouseScrollData>(data_); }
    const ResizeData& resize() const { return std::get<ResizeData>(data_); }

private:
    Event() = default;
    std::variant<std::monostate, KeyData, MouseButtonData, MouseMoveData, MouseScrollData, ResizeData> data_;
};

} // namespace xebble
```

**Step 4: Update `tests/CMakeLists.txt`**

Add `test_event.cpp` to the `xebble_tests` sources.

**Step 5: Build and run tests**

Run:
```bash
cmake --build build/debug && ctest --preset debug
```

Expected: All tests pass.

**Step 6: Commit**

```bash
git add -A
git commit -m "feat: add event system with typed event queue"
```

---

## Task 4: Window (GLFW Wrapper)

**Files:**
- Create: `include/xebble/window.hpp`
- Create: `src/window.cpp`
- Modify: `src/CMakeLists.txt` — add `window.cpp`
- Modify: `examples/basic_tilemap/main.cpp` — open a window

**Step 1: Create `include/xebble/window.hpp`**

```cpp
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
```

**Step 2: Create `src/window.cpp`**

Implement using GLFW. Key points:
- Initialize GLFW with `GLFW_NO_API` (no OpenGL context — we use Vulkan)
- Set up GLFW callbacks for key, mouse, scroll, resize, focus, close
- Callbacks push `Event` objects into a `std::vector<Event>`
- `poll_events()` clears the vector, then calls `glfwPollEvents()` which fills it via callbacks
- `content_scale()` uses `glfwGetWindowContentScale()`
- Use GLFW user pointer to get back to the Impl from C callbacks

Full implementation — see design doc for API surface. The Impl struct holds `GLFWwindow*`, the event vector, and handles GLFW init/teardown via a static ref count (multiple windows possible but GLFW init is global).

**Step 3: Update `src/CMakeLists.txt`**

Add `window.cpp` to sources.

**Step 4: Update `examples/basic_tilemap/main.cpp`**

```cpp
#include <xebble/window.hpp>
#include <cstdio>

int main() {
    auto window_result = xebble::Window::create({
        .title = "Xebble - Basic Tilemap",
        .width = 1280,
        .height = 720,
    });

    if (!window_result) {
        std::fprintf(stderr, "Failed to create window: %s\n",
                     window_result.error().message.c_str());
        return 1;
    }

    auto& window = *window_result;

    while (!window.should_close()) {
        window.poll_events();
        for (auto& event : window.events()) {
            switch (event.type) {
                case xebble::EventType::KeyPress:
                    std::printf("Key pressed: %d\n", static_cast<int>(event.key().key));
                    break;
                case xebble::EventType::WindowResize:
                    std::printf("Resized: %ux%u\n", event.resize().width, event.resize().height);
                    break;
                default:
                    break;
            }
        }
    }

    return 0;
}
```

**Step 5: Build and run example**

Run:
```bash
cmake --build build/debug
./build/debug/examples/basic_tilemap
```

Expected: A window opens titled "Xebble - Basic Tilemap". Key presses print to stdout. Closing the window exits cleanly.

**Step 6: Commit**

```bash
git add -A
git commit -m "feat: add GLFW window wrapper with event queue"
```

---

## Task 5: Vulkan Context — Instance, Device, Queues, VMA

**Files:**
- Create: `src/vulkan/context.hpp`
- Create: `src/vulkan/context.cpp`
- Modify: `src/CMakeLists.txt` — add vulkan sources

**Step 1: Create `src/vulkan/context.hpp`**

The VulkanContext owns:
- `VkInstance` (with validation layers in debug builds)
- `VkSurfaceKHR` (created from GLFW window)
- `VkPhysicalDevice` (auto-selected, prefer discrete GPU)
- `VkDevice` with graphics + present queue
- `VmaAllocator`
- `VkCommandPool`

Exposed as a move-only RAII class. All Vulkan handles destroyed in reverse order in destructor.

```cpp
#pragma once

#include <xebble/types.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <expected>
#include <memory>

struct GLFWwindow;

namespace xebble::vk {

class Context {
public:
    static std::expected<Context, Error> create(GLFWwindow* window);

    ~Context();
    Context(Context&&) noexcept;
    Context& operator=(Context&&) noexcept;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    VkInstance instance() const;
    VkPhysicalDevice physical_device() const;
    VkDevice device() const;
    VkQueue graphics_queue() const;
    VkQueue present_queue() const;
    uint32_t graphics_queue_family() const;
    uint32_t present_queue_family() const;
    VkSurfaceKHR surface() const;
    VmaAllocator allocator() const;
    VkCommandPool command_pool() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Context() = default;
};

} // namespace xebble::vk
```

**Step 2: Implement `src/vulkan/context.cpp`**

Key implementation details:
- Enable `VK_LAYER_KHRONOS_validation` in debug builds
- Enable required instance extensions from GLFW (`glfwGetRequiredInstanceExtensions`)
- On macOS, enable `VK_KHR_portability_enumeration` and `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`
- Physical device selection: score devices, prefer discrete GPU, require graphics + present queue support
- Enable `VK_KHR_portability_subset` device extension if available (MoltenVK requirement)
- Create VMA allocator with the device
- Create a command pool for the graphics queue family

**Step 3: Build and verify compilation**

Run:
```bash
cmake --build build/debug
```

Expected: Compiles cleanly. (No runtime test yet — needs a window to create a surface.)

**Step 4: Commit**

```bash
git add -A
git commit -m "feat: add Vulkan context with instance, device, queues, and VMA"
```

---

## Task 6: Vulkan Swapchain

**Files:**
- Create: `src/vulkan/swapchain.hpp`
- Create: `src/vulkan/swapchain.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Create `src/vulkan/swapchain.hpp`**

```cpp
#pragma once

#include <xebble/types.hpp>
#include <vulkan/vulkan.h>
#include <expected>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace xebble::vk {

class Context;

class Swapchain {
public:
    static std::expected<Swapchain, Error> create(
        const Context& ctx, GLFWwindow* window, bool vsync);

    ~Swapchain();
    Swapchain(Swapchain&&) noexcept;
    Swapchain& operator=(Swapchain&&) noexcept;
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    std::expected<uint32_t, Error> acquire_next_image(VkSemaphore signal_semaphore);
    VkResult present(uint32_t image_index, VkSemaphore wait_semaphore);

    std::expected<void, Error> recreate(GLFWwindow* window, bool vsync);

    VkSwapchainKHR handle() const;
    VkFormat image_format() const;
    VkExtent2D extent() const;
    const std::vector<VkImageView>& image_views() const;
    uint32_t image_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Swapchain() = default;
};

} // namespace xebble::vk
```

**Step 2: Implement `src/vulkan/swapchain.cpp`**

Key details:
- Query surface capabilities, formats (prefer `B8G8R8A8_SRGB`), present modes
- Vsync → `VK_PRESENT_MODE_FIFO_KHR`, no vsync → `VK_PRESENT_MODE_MAILBOX_KHR` fallback to FIFO
- Create image views for each swapchain image
- `recreate()` destroys old image views and swapchain, queries new extent, creates fresh
- Handle `VK_ERROR_OUT_OF_DATE_KHR` from acquire/present

**Step 3: Build and verify**

Run: `cmake --build build/debug`

**Step 4: Commit**

```bash
git add -A
git commit -m "feat: add Vulkan swapchain with recreation support"
```

---

## Task 7: Vulkan Buffers, Descriptors, Command Buffers

**Files:**
- Create: `src/vulkan/buffer.hpp`
- Create: `src/vulkan/buffer.cpp`
- Create: `src/vulkan/descriptor.hpp`
- Create: `src/vulkan/descriptor.cpp`
- Create: `src/vulkan/command.hpp`
- Create: `src/vulkan/command.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Buffer RAII wrapper**

`buffer.hpp` / `buffer.cpp`: A `Buffer` class wrapping `VkBuffer` + `VmaAllocation`.

```cpp
class Buffer {
public:
    static std::expected<Buffer, Error> create(
        VmaAllocator allocator,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VmaMemoryUsage memory_usage);

    void upload(const void* data, VkDeviceSize size);

    VkBuffer handle() const;
    VkDeviceSize size() const;
    // ... RAII move semantics, destructor frees via VMA
};
```

**Step 2: Descriptor pool and set layout helpers**

`descriptor.hpp` / `descriptor.cpp`: Helpers to create descriptor pools, set layouts, and allocate/write descriptor sets.

**Step 3: Command buffer helpers**

`command.hpp` / `command.cpp`: Helpers for one-shot command buffer execution (useful for texture uploads), and per-frame command buffer management.

**Step 4: Build and verify**

Run: `cmake --build build/debug`

**Step 5: Commit**

```bash
git add -A
git commit -m "feat: add Vulkan buffer, descriptor, and command buffer helpers"
```

---

## Task 8: Vulkan Pipeline and Shader Loading

**Files:**
- Create: `src/vulkan/pipeline.hpp`
- Create: `src/vulkan/pipeline.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Pipeline builder**

Create a pipeline builder that:
- Loads SPIR-V from compiled shader files
- Creates shader modules
- Configures vertex input for instanced quad rendering (per-instance: position vec2, uv_offset vec2, uv_size vec2, color vec4)
- Sets up the graphics pipeline with alpha blending enabled
- Creates the pipeline layout with push constants / descriptor set layouts
- Returns an RAII wrapper that destroys pipeline + layout + shader modules

Two pipelines needed:
1. **Sprite pipeline** — instanced quads with texture sampling, alpha blending, depth test off
2. **Blit pipeline** — fullscreen quad, no vertex input, texture sampling, no blending

**Step 2: Build and verify**

Run: `cmake --build build/debug`

**Step 3: Commit**

```bash
git add -A
git commit -m "feat: add Vulkan pipeline builder with SPIR-V loading"
```

---

## Task 9: Texture System

**Files:**
- Create: `include/xebble/texture.hpp`
- Create: `src/texture.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Create texture class**

```cpp
class Texture {
public:
    static std::expected<Texture, Error> load(
        vk::Context& ctx,
        const std::filesystem::path& path);

    static std::expected<Texture, Error> load_from_memory(
        vk::Context& ctx,
        const uint8_t* data, size_t size);

    static std::expected<Texture, Error> create_empty(
        vk::Context& ctx,
        uint32_t width, uint32_t height,
        VkFormat format);

    uint32_t width() const;
    uint32_t height() const;
    VkImageView image_view() const;
    VkSampler sampler() const;
    // ... RAII
};
```

Key details:
- Uses stb_image to load PNG/BMP/etc
- Creates `VkImage` + `VmaAllocation`, `VkImageView`, `VkSampler`
- Uploads via staging buffer + one-shot command buffer
- Nearest-neighbor sampler by default (pixel-perfect)
- `load_from_memory()` for loading from ZIP archives later
- `create_empty()` for the offscreen framebuffer render target

**Step 2: Build and verify**

**Step 3: Commit**

```bash
git add -A
git commit -m "feat: add texture loading with stb_image and VMA"
```

---

## Task 10: Shaders — Proper Implementation

**Files:**
- Modify: `shaders/sprite.vert`
- Modify: `shaders/sprite.frag`
- Modify: `shaders/blit.vert`
- Modify: `shaders/blit.frag`

**Step 1: Implement `sprite.vert`**

```glsl
#version 450

// Per-instance data
layout(location = 0) in vec2 inPosition;    // Quad position in virtual pixels
layout(location = 1) in vec2 inUVOffset;    // Top-left UV in spritesheet
layout(location = 2) in vec2 inUVSize;      // UV width/height
layout(location = 3) in vec2 inQuadSize;    // Quad width/height in virtual pixels
layout(location = 4) in vec4 inColor;       // Color tint / text color

// Push constant: orthographic projection
layout(push_constant) uniform PushConstants {
    mat4 projection;
} pc;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;

// Generate quad vertices from gl_VertexIndex (0-5 for two triangles)
vec2 positions[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    vec2 pos = positions[gl_VertexIndex];
    vec2 worldPos = inPosition + pos * inQuadSize;
    gl_Position = pc.projection * vec4(worldPos, 0.0, 1.0);
    fragTexCoord = inUVOffset + pos * inUVSize;
    fragColor = inColor;
}
```

**Step 2: Implement `sprite.frag`**

```glsl
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);
    if (texColor.a < 0.01) discard;
    outColor = texColor * fragColor;
}
```

**Step 3: Implement `blit.vert`**

```glsl
#version 450

layout(location = 0) out vec2 fragTexCoord;

// Fullscreen triangle trick — 3 vertices, no vertex buffer
void main() {
    fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);
    fragTexCoord.y = 1.0 - fragTexCoord.y; // Flip Y for Vulkan
}
```

**Step 4: Implement `blit.frag`**

```glsl
#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(set = 0, binding = 0) uniform sampler2D texSampler;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(texSampler, fragTexCoord);
}
```

**Step 5: Build and verify shaders compile**

Run:
```bash
cmake --build build/debug
```

Expected: SPIR-V files generated in `build/debug/shaders/`.

**Step 6: Commit**

```bash
git add -A
git commit -m "feat: implement sprite and blit shaders"
```

---

## Task 11: Renderer Core

**Files:**
- Create: `include/xebble/renderer.hpp`
- Create: `src/renderer.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Create `include/xebble/renderer.hpp`**

As specified in the design doc. The Renderer class is the main public interface.

**Step 2: Implement `src/renderer.cpp`**

The Renderer::Impl holds:
- `vk::Context`
- `vk::Swapchain`
- Offscreen framebuffer (VkImage + VkImageView + VkFramebuffer at virtual resolution)
- Sprite pipeline + blit pipeline
- Per-frame sync objects (2 frames in flight): fences, image-available semaphores, render-finished semaphores
- Per-frame command buffers
- Instance buffers for sprites/tiles (dynamic, host-visible via VMA)
- Descriptor sets for textures
- Projection matrix (orthographic)
- Frame timing via `std::chrono::steady_clock`

`begin_frame()`:
1. Wait for fence of current frame-in-flight
2. Acquire swapchain image (recreate if out of date)
3. Reset fence, reset command buffer
4. Begin command buffer
5. Begin offscreen render pass (clear with background color)
6. Update delta_time / elapsed_time / frame_count
7. Return true (or false if swapchain recreation failed)

`draw(TileMap)` / `draw(Sprite)` / `draw(TextBlock)`:
- Accumulate draw commands into per-frame batches (don't submit yet)

`end_frame()`:
1. Sort accumulated sprite instances by z-order, then by texture
2. Upload instance data to GPU buffers
3. Issue draw calls: tilemaps first, then sprites, then text
4. End offscreen render pass
5. Begin blit render pass (swapchain framebuffer)
6. Calculate viewport for letterboxing
7. Draw fullscreen triangle sampling offscreen texture
8. End blit render pass
9. End command buffer, submit to graphics queue
10. Present

**Step 3: Build and verify**

**Step 4: Commit**

```bash
git add -A
git commit -m "feat: add core renderer with offscreen framebuffer and pixel-perfect blit"
```

---

## Task 12: SpriteSheet

**Files:**
- Create: `include/xebble/spritesheet.hpp`
- Create: `src/spritesheet.cpp`
- Create: `tests/test_spritesheet.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write tests for region calculation**

```cpp
#include <gtest/gtest.h>
#include <xebble/spritesheet.hpp>

using namespace xebble;

TEST(SpriteSheetRegion, LinearIndex) {
    // 128x128 sheet with 16x16 tiles = 8 columns, 8 rows
    auto region = SpriteSheet::calculate_region(128, 128, 16, 16, 0);
    EXPECT_FLOAT_EQ(region.x, 0.0f);
    EXPECT_FLOAT_EQ(region.y, 0.0f);
    EXPECT_FLOAT_EQ(region.w, 16.0f / 128.0f);
    EXPECT_FLOAT_EQ(region.h, 16.0f / 128.0f);
}

TEST(SpriteSheetRegion, SecondRow) {
    auto region = SpriteSheet::calculate_region(128, 128, 16, 16, 8); // First tile of second row
    EXPECT_FLOAT_EQ(region.x, 0.0f);
    EXPECT_FLOAT_EQ(region.y, 16.0f / 128.0f);
}

TEST(SpriteSheetRegion, ColRow) {
    auto region = SpriteSheet::calculate_region(128, 128, 16, 16, 3, 2); // col=3, row=2
    EXPECT_FLOAT_EQ(region.x, 48.0f / 128.0f);
    EXPECT_FLOAT_EQ(region.y, 32.0f / 128.0f);
}
```

Note: `calculate_region` is a static method that doesn't need a GPU — it's pure math on texture dimensions. The full `SpriteSheet` class wraps this with a `Texture`.

**Step 2: Implement SpriteSheet**

The class holds a `Texture`, tile dimensions, and sheet dimensions. `region()` returns UV coordinates in [0,1] normalized space. `calculate_region()` is a static helper for testing without GPU.

**Step 3: Build and run tests**

**Step 4: Commit**

```bash
git add -A
git commit -m "feat: add spritesheet with region calculation"
```

---

## Task 13: Sprite System

**Files:**
- Create: `include/xebble/sprite.hpp`
- Create: `src/sprite.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Create sprite types**

As specified in the design doc. The `Sprite` struct is a simple data type. Animation advancement logic lives in the renderer — when `draw(sprite)` is called, if the sprite has an `AnimationDef`, the renderer uses its `delta_time()` to compute the current frame.

**Step 2: Build and verify**

**Step 3: Commit**

```bash
git add -A
git commit -m "feat: add sprite and animation types"
```

---

## Task 14: TileMap

**Files:**
- Create: `include/xebble/tilemap.hpp`
- Create: `src/tilemap.cpp`
- Create: `tests/test_tilemap.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write tests for tilemap data**

```cpp
TEST(TileMap, SetAndGetTile) {
    // Test the data layer without GPU — TileMap stores tile indices in a flat vector
    TileMapData data(40, 22, 3);
    data.set_tile(0, 5, 5, 42);
    EXPECT_EQ(data.tile_at(0, 5, 5), 42u);
    EXPECT_EQ(data.tile_at(0, 0, 0), std::nullopt);
}

TEST(TileMap, ClearTile) {
    TileMapData data(10, 10, 1);
    data.set_tile(0, 3, 3, 10);
    data.clear_tile(0, 3, 3);
    EXPECT_EQ(data.tile_at(0, 3, 3), std::nullopt);
}

TEST(TileMap, ClearLayer) {
    TileMapData data(10, 10, 2);
    data.set_tile(0, 0, 0, 1);
    data.set_tile(1, 0, 0, 2);
    data.clear_layer(0);
    EXPECT_EQ(data.tile_at(0, 0, 0), std::nullopt);
    EXPECT_EQ(data.tile_at(1, 0, 0), 2u);
}
```

Separate `TileMapData` (pure data, testable) from `TileMap` (owns GPU resources, references a SpriteSheet).

**Step 2: Implement TileMapData and TileMap**

**Step 3: Build and run tests**

**Step 4: Commit**

```bash
git add -A
git commit -m "feat: add tilemap with N-layer support"
```

---

## Task 15: Bitmap Font

**Files:**
- Create: `include/xebble/font.hpp`
- Create: `src/font.cpp`
- Create: `tests/test_font.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write tests for glyph lookup**

```cpp
TEST(BitmapFontData, GlyphLookup) {
    // charset maps characters to indices in the spritesheet
    BitmapFontData data(8, 8, " !\"#$%&'()*+,-./0123456789");
    auto region = data.glyph_index('A');
    EXPECT_EQ(region, std::nullopt); // 'A' not in charset

    auto space = data.glyph_index(' ');
    EXPECT_EQ(space, 0u);

    auto zero = data.glyph_index('0');
    EXPECT_EQ(zero, 16u); // 16th character in the charset
}
```

**Step 2: Implement BitmapFont**

`BitmapFont` wraps a `SpriteSheet` where each tile is a glyph. The charset string maps character → tile index. `BitmapFontData` is the testable data layer.

**Step 3: Implement FreeType Font**

`Font` loads a TTF via FreeType, rasterizes glyphs into a texture atlas on creation. Stores glyph metrics (advance, bearing, size) for layout. The atlas is a single `Texture`.

**Step 4: Create `TextBlock` struct in `font.hpp`**

As per design doc.

**Step 5: Build and run tests**

**Step 6: Commit**

```bash
git add -A
git commit -m "feat: add bitmap font and FreeType font with text rendering"
```

---

## Task 16: Asset Manager

**Files:**
- Create: `include/xebble/asset_manager.hpp`
- Create: `src/asset_manager.cpp`
- Create: `tests/test_asset_manager.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write tests for manifest parsing**

```cpp
TEST(ManifestParser, ParseSpriteSheet) {
    auto toml_str = R"(
        [spritesheets.dungeon]
        path = "sprites/dungeon.png"
        tile_width = 16
        tile_height = 16
    )";
    auto manifest = parse_manifest(toml_str);
    ASSERT_TRUE(manifest.spritesheets.contains("dungeon"));
    EXPECT_EQ(manifest.spritesheets["dungeon"].path, "sprites/dungeon.png");
    EXPECT_EQ(manifest.spritesheets["dungeon"].tile_width, 16u);
}

TEST(ManifestParser, ParseBitmapFont) {
    auto toml_str = R"(
        [bitmap_fonts.default]
        path = "fonts/cp437.png"
        glyph_width = 8
        glyph_height = 8
        charset = " !#$"
    )";
    auto manifest = parse_manifest(toml_str);
    ASSERT_TRUE(manifest.bitmap_fonts.contains("default"));
    EXPECT_EQ(manifest.bitmap_fonts["default"].glyph_width, 8u);
}
```

**Step 2: Implement manifest parser**

Parse TOML using toml++. Extract spritesheet, bitmap_font, and font entries into typed structs.

**Step 3: Implement ZIP archive reader**

Use minizip-ng to open a ZIP file and read entries by path. Wrap in a simple `ArchiveReader` class.

**Step 4: Implement file resolver**

Given a relative path, check directory first, then ZIP archive. Return `std::vector<uint8_t>`.

**Step 5: Implement AssetManager**

On `create()`:
1. Parse manifest
2. For each declared asset, resolve the file (directory or ZIP), load it
3. Store in `std::unordered_map<std::string, std::any>` keyed by manifest name
4. `get<T>()` does `std::any_cast<const T&>`

**Step 6: Build and run tests**

**Step 7: Commit**

```bash
git add -A
git commit -m "feat: add asset manager with TOML manifest and ZIP archive support"
```

---

## Task 17: Game Loop Framework

**Files:**
- Create: `include/xebble/game.hpp`
- Create: `src/game.cpp`
- Modify: `src/CMakeLists.txt`

**Step 1: Create `include/xebble/game.hpp`**

As specified in the design doc: `Game` base class + `GameConfig` + `run()` function.

**Step 2: Implement `src/game.cpp`**

The `run()` function:
```cpp
int run(std::unique_ptr<Game> game, const GameConfig& config) {
    auto window = Window::create(config.window);
    if (!window) return 1;

    auto renderer = Renderer::create(*window, config.renderer);
    if (!renderer) return 1;

    auto assets = AssetManager::create(*renderer, config.assets);
    if (!assets) return 1;

    game->init(*renderer, *assets);
    game->layout(config.renderer.virtual_width, config.renderer.virtual_height);

    float accumulator = 0.0f;

    while (!window->should_close()) {
        window->poll_events();
        for (auto& event : window->events()) {
            if (event.type == EventType::WindowResize) {
                game->layout(event.resize().width, event.resize().height);
            }
            game->on_event(event);
        }

        accumulator += renderer->delta_time();
        while (accumulator >= config.fixed_timestep) {
            game->update(config.fixed_timestep);
            accumulator -= config.fixed_timestep;
        }

        if (renderer->begin_frame()) {
            game->draw(*renderer);
            renderer->end_frame();
        }
    }

    game->shutdown();
    return 0;
}
```

**Step 3: Build and verify**

**Step 4: Commit**

```bash
git add -A
git commit -m "feat: add game loop framework with fixed timestep update"
```

---

## Task 18: Umbrella Header

**Files:**
- Create: `include/xebble/xebble.hpp`

**Step 1: Create the umbrella header**

```cpp
#pragma once

#include <xebble/types.hpp>
#include <xebble/log.hpp>
#include <xebble/event.hpp>
#include <xebble/window.hpp>
#include <xebble/renderer.hpp>
#include <xebble/texture.hpp>
#include <xebble/spritesheet.hpp>
#include <xebble/sprite.hpp>
#include <xebble/tilemap.hpp>
#include <xebble/font.hpp>
#include <xebble/asset_manager.hpp>
#include <xebble/game.hpp>
```

**Step 2: Commit**

```bash
git add -A
git commit -m "feat: add xebble.hpp umbrella header"
```

---

## Task 19: Working Example

**Files:**
- Modify: `examples/basic_tilemap/main.cpp`
- Create: `examples/basic_tilemap/assets/manifest.toml`
- Create: `examples/basic_tilemap/assets/` (with test spritesheet + font)

**Step 1: Generate a test spritesheet**

Write a small C++ program or use the example itself to programmatically generate a simple 128x128 PNG with colored 16x16 tiles (checkerboard pattern, numbered). Alternatively, create a minimal bitmap in code using stb_image_write. This avoids requiring external art assets for the example.

**Step 2: Create `examples/basic_tilemap/assets/manifest.toml`**

```toml
[spritesheets.tiles]
path = "tiles.png"
tile_width = 16
tile_height = 16

[bitmap_fonts.default]
path = "font.png"
glyph_width = 8
glyph_height = 8
charset = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
```

**Step 3: Implement the example using the Game class**

```cpp
#include <xebble/xebble.hpp>

class BasicTilemap : public xebble::Game {
    const xebble::SpriteSheet* tiles_ = nullptr;
    const xebble::BitmapFont* font_ = nullptr;
    std::optional<xebble::TileMap> tilemap_;
    xebble::Sprite player_;

public:
    void init(xebble::Renderer& renderer, xebble::AssetManager& assets) override {
        tiles_ = &assets.get<xebble::SpriteSheet>("tiles");
        font_ = &assets.get<xebble::BitmapFont>("default");

        tilemap_ = xebble::TileMap::create(renderer, *tiles_, 40, 22, 2).value();

        // Fill ground layer with grass tiles
        for (uint32_t y = 0; y < 22; y++)
            for (uint32_t x = 0; x < 40; x++)
                tilemap_->set_tile(0, x, y, 0);

        // Place some wall tiles on layer 1
        for (uint32_t x = 5; x < 15; x++) {
            tilemap_->set_tile(1, x, 5, 1);
            tilemap_->set_tile(1, x, 10, 1);
        }

        player_ = {
            .position = {160.0f, 120.0f},
            .z_order = 1.0f,
            .sheet = tiles_,
            .source = 4u,
        };
    }

    void update(float dt) override {
        // Player movement would go here
    }

    void on_event(const xebble::Event& event) override {
        if (event.type == xebble::EventType::KeyPress) {
            switch (event.key().key) {
                case xebble::Key::W: player_.position.y -= 16.0f; break;
                case xebble::Key::S: player_.position.y += 16.0f; break;
                case xebble::Key::A: player_.position.x -= 16.0f; break;
                case xebble::Key::D: player_.position.x += 16.0f; break;
                default: break;
            }
        }
    }

    void draw(xebble::Renderer& renderer) override {
        renderer.draw(*tilemap_);
        renderer.draw(player_);
        renderer.draw(xebble::TextBlock{
            .text = "WASD to move",
            .position = {4.0f, 4.0f},
            .font = font_,
        });
    }

    void layout(uint32_t w, uint32_t h) override {}
};

int main() {
    return xebble::run(std::make_unique<BasicTilemap>(), {
        .window = {.title = "Xebble - Basic Tilemap", .width = 1280, .height = 720},
        .renderer = {.virtual_width = 640, .virtual_height = 360},
        .assets = {.directory = "examples/basic_tilemap/assets/",
                   .manifest = "examples/basic_tilemap/assets/manifest.toml"},
    });
}
```

**Step 4: Build and run**

Run:
```bash
cmake --build build/debug
./build/debug/examples/basic_tilemap
```

Expected: Window opens showing a tilemap with colored tiles, a player sprite that moves with WASD, and "WASD to move" text in the top-left corner. The rendering should be pixel-perfect with nearest-neighbor scaling.

**Step 5: Commit**

```bash
git add -A
git commit -m "feat: add working basic_tilemap example"
```

---

## Task 20: Final Verification and Cleanup

**Step 1: Run all tests**

```bash
cmake --build build/debug && ctest --preset debug
```

Expected: All tests pass.

**Step 2: Build release**

```bash
cmake --preset release && cmake --build build/release
```

Expected: Clean release build.

**Step 3: Run the example in release mode**

```bash
./build/release/examples/basic_tilemap
```

Expected: Same behavior as debug, possibly faster.

**Step 4: Clean up any placeholder files**

Remove `src/placeholder.cpp` if it still exists, any TODO comments, etc.

**Step 5: Commit**

```bash
git add -A
git commit -m "chore: final cleanup and verification"
```
