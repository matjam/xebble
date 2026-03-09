# Xebble TODO

Future features beyond the core renderer and ECS. The goal is for Xebble to handle all the annoying infrastructure so the game developer can focus entirely on gameplay and UI.

---

## Audio ✓ DONE

- [x] Integrate a thin audio wrapper (miniaudio header-only + libxmp + vendored libsidplayfp) for sound effects and music streaming
- [x] `play_sound(asset)` / `play_music(asset, loop)` / `set_volume()` / master + sfx + music volume controls
- [x] Expose audio assets through `AssetManager` and the TOML manifest (`[sounds]` / `[music]` tables)
- [x] `AudioEngine` injected as a World resource; `update()` called per frame by `xebble::run()`
- [x] Example `ex17_audio` demonstrates in-memory WAV beep, tracker music, SID chiptune, volume controls

## Configuration ✓ DONE

- [x] `Config` class loads a `game.toml` with `[window]`, `[renderer]`, `[audio]`, `[assets]`, `[game]` sections
- [x] Engine sections map to existing config structs; `[game]` exposed as raw `toml::table` via `Config::game_value<T>()`
- [x] `run()` overloads accept a filesystem path to auto-load config and inject `Config` as a World resource
- [x] Audio volumes from `[audio]` applied to `AudioEngine` on startup
- [x] Missing config files return defaults (not an error)

## Input

- [ ] Gamepad / controller support via GLFW joystick API; add `GamepadButton` and `GamepadAxis` event types to `EventType`
- [x] `InputMap` — named action layer (action → key/button binding, rebindable, serializable); replaces per-game "is confirm pressed" boilerplate

## Serialization

- [x] Support non-trivially-copyable components via a custom hook (`serialize(BinaryWriter&)` / `static T deserialize(BinaryReader&)` opt-in interface), so components containing `string`, `vector`, etc. can participate in save games
- [ ] Schema versioning / migration path for save files — currently version=1 with no forward compatibility; old saves break when components change

## UI Widgets

- [x] `progress_bar(value, max, style)` widget — health bars, loading indicators; fill + background + optional text overlay
- [x] `separator(style)` widget — horizontal divider line between widget groups
- [x] `message_log(id, log, style)` widget — scrollable `MessageLog` view with per-message colors, mouse-scroll, auto-scroll
- [x] `radio_button(label, selected, value)` widget — mutually exclusive option selection within a group
- [x] `slider(id, label, value, min, max)` widget — horizontal slider for floating-point values with click-and-drag support
- [x] `tooltip(text)` — hover tooltip on the preceding widget, positioned near the mouse cursor
- [x] `modal(id, size, fn)` — modal dialog with semi-transparent backdrop that blocks underlying input
- [ ] Nested panels / tabs

## UI Layout

- [ ] Auto-size panel to its content
- [ ] Side-by-side panels that respond to content size
- [ ] Configurable column/row layout with padding that doesn't require manual positioning

## Shader Effects

Named shader effects as first-class assets, attachable to sprites. Layered
design: start with named fragment shader effects (glow, dissolve, palette swap),
designed so a full material system can be added later.

### Architecture overview

The current renderer has a single hardcoded sprite pipeline (`sprite.vert` +
`sprite.frag`). The plan is to support multiple pipelines -- one per registered
shader effect -- that differ only in their fragment shader. The vertex shader,
vertex input layout, and descriptor set layout remain shared.

### New types

- **`ShaderEffect`** (`include/xebble/shader_effect.hpp`) -- owns a named
  VkPipeline compiled from a custom fragment shader. Has a name string and a
  push constant block for per-effect uniforms (`time`, `intensity`). Move-only.

- **`ShaderEffectParams`** -- 16-byte struct pushed to the fragment stage:
  `float time` (elapsed seconds), `float intensity` (per-sprite 0-1), 8 bytes
  padding.

### Pipeline factory

Add `Pipeline::create_sprite_effect_pipeline()` to `src/vulkan/pipeline.cpp`.
Takes the default vertex SPIR-V + a custom fragment SPIR-V blob (compiled at
runtime via shaderc). Shares the same vertex input layout, descriptor set
layout, and push constant range as the default sprite pipeline. Push constant
range extended: 64 bytes `mat4 projection` (vertex) + 16 bytes effect params
(fragment).

### Renderer changes

- Store registered effects: `unordered_map<string, ShaderEffect>`
- `register_shader_effect(name, glsl_source)` -- compiles GLSL to SPIR-V via
  shaderc, creates the pipeline, stores it
- `DrawBatch` gains a `VkPipeline pipeline` field (VK_NULL_HANDLE = default)
- Draw loop: track last-bound pipeline, rebind + re-push constants only when
  the batch's pipeline differs
- Sorting becomes `(z_order, pipeline_ptr, texture)` to minimize pipeline
  switches
- Push `ShaderEffectParams` to the fragment stage before each pipeline-group

### Sprite component

`Sprite` gets `const ShaderEffect* effect = nullptr` and
`float effect_intensity = 1.0f`.

### Built-in systems

`SpriteRenderSystem` sort key becomes `(z_order, effect, texture)`. Batch
pipeline field set from the sprite's effect pointer.

### Asset manager

New manifest section:
```toml
[shaders]
glow     = "shaders/glow.frag"
dissolve = "shaders/dissolve.frag"
```

Loads each `.frag` file, compiles via shaderc, calls
`renderer.register_shader_effect()`.

### Custom fragment shader contract

```glsl
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D texSampler;
layout(push_constant) uniform EffectParams {
    layout(offset = 64) float time;
    layout(offset = 68) float intensity;
    layout(offset = 72) float pad0;
    layout(offset = 76) float pad1;
} effect;
layout(location = 0) out vec4 outColor;
```

### Usage

```cpp
renderer->register_shader_effect("glow", glow_frag_source);
auto* glow = renderer->shader_effect("glow");
world.build_entity()
    .with(Position{100, 100})
    .with(Sprite{&sheet, 0, 1.0f, {}, 1.0f, 0.0f, 0.5f, 0.5f, glow, 0.8f})
    .build();
```

### Future: full material system

Material = ShaderEffect + parameter values + additional texture bindings
(normal maps, palette LUTs, noise). Per-material UBOs at descriptor set 1.
Hot-reload of shader source during development.

### Files to modify

| File | Change |
|---|---|
| `include/xebble/shader_effect.hpp` | New: ShaderEffect class |
| `include/xebble/components.hpp` | Add effect ptr + intensity to Sprite |
| `include/xebble/renderer.hpp` | Add register/get shader_effect methods |
| `include/xebble/asset_manager.hpp` | Add ShaderEntry to manifest |
| `src/vulkan/pipeline.hpp` | Add create_sprite_effect_pipeline() |
| `src/vulkan/pipeline.cpp` | Implement effect pipeline creation |
| `src/renderer.cpp` | Effect storage, per-batch pipeline switching |
| `src/builtin_systems.cpp` | Effect-aware sort key, batch pipeline field |
| `src/asset_manager.cpp` | Load and compile shader assets |

## Animation

- [ ] `AnimationClip` — sequence of tile indices with per-frame durations
- [ ] `Animator` component / system — drives `Sprite::tile_index` from the active clip, handles looping and one-shot clips
- [ ] Animation assets exposed through `AssetManager`

## Particles

- [ ] CPU-side particle emitter: spawn N sprites per second, apply velocity + gravity + colour fade
- [ ] Particle presets sufficient for common roguelike VFX (blood splatter, sparks, smoke, magic)

## Tweening / Easing

- [ ] `tween.hpp` — common easing functions (linear, ease-in, ease-out, ease-in-out, bounce, elastic)
- [ ] `Tween<T>` — tracks elapsed time against a duration, interpolates a value, fires a completion callback

## Camera

- [ ] Smooth follow with configurable lag / lerp factor
- [ ] Zoom (independent of virtual resolution)
- [ ] Camera shake (trauma-based or impulse-based)
- [ ] Bounds clamping — prevent the camera from scrolling beyond map edges

## Collision / Spatial Queries

- [ ] Entity-vs-entity AABB overlap query
- [ ] `Grid<vector<Entity>>` spatial hash helper for broad-phase lookups
- [ ] Tile-map collision helpers (e.g. `is_solid_at(world_pos)`)

## Asset Loading

- [ ] Async / background asset loading with progress reporting for loading screens
- [ ] Hot reload — watch asset files for changes and reload individual assets without restarting (development builds only)

## Profiling / Instrumentation

- [ ] `XEBBLE_PROFILE_SCOPE("name")` macro — lightweight scope timer usable in both development and release builds
- [ ] Per-system timing breakdown visible in the debug overlay
- [ ] Optional Tracy / Optick integration behind a CMake flag

## ECS / World Queries

- [ ] `world.find<T>(predicate)` → `optional<Entity>` — single-entity lookup without writing a manual loop
- [ ] Typed event bus / message passing between systems (complement to the raw `EventQueue`)
- [ ] Deterministic replay infrastructure — record all input + initial RNG seed, play back without modification

## Tilemap

- [ ] TMX (Tiled) / LDtk tilemap import — most roguelike devs use external level editors; internal format only is a friction point

---

## Deferred (tackle last)

These are deliberately parked until the feature set above is closer to complete.

- [ ] **Windows platform** — `window_windows.cpp` is a stub; implement display mode enumeration and all platform hooks
- [ ] **Networking** — lock-step or state-sync primitives; the ECS serialization is a natural foundation for deterministic rollback
- [ ] **Standalone documentation site** — API reference, architecture guide, "how to ship a game with Xebble"; doc comments in headers are sufficient until the feature set stabilises
