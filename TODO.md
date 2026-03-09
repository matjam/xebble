# Xebble TODO

Future features beyond the core renderer and ECS. The goal is for Xebble to handle all the annoying infrastructure so the game developer can focus entirely on gameplay and UI.

---

## Audio ✓ DONE

- [x] Integrate a thin audio wrapper (miniaudio header-only + libxmp + vendored libsidplayfp) for sound effects and music streaming
- [x] `play_sound(asset)` / `play_music(asset, loop)` / `set_volume()` / master + sfx + music volume controls
- [x] Expose audio assets through `AssetManager` and the TOML manifest (`[sounds]` / `[music]` tables)
- [x] `AudioEngine` injected as a World resource; `update()` called per frame by `xebble::run()`
- [x] Example `ex17_audio` demonstrates in-memory WAV beep, tracker music, SID chiptune, volume controls

## Input

- [ ] Gamepad / controller support via GLFW joystick API; add `GamepadButton` and `GamepadAxis` event types to `EventType`
- [x] `InputMap` — named action layer (action → key/button binding, rebindable, serializable); replaces per-game "is confirm pressed" boilerplate

## Serialization

- [x] Support non-trivially-copyable components via a custom hook (`serialize(BinaryWriter&)` / `static T deserialize(BinaryReader&)` opt-in interface), so components containing `string`, `vector`, etc. can participate in save games
- [ ] Schema versioning / migration path for save files — currently version=1 with no forward compatibility; old saves break when components change

## UI Widgets

- [ ] `progress_bar(value, max, style)` widget — health bars, loading indicators; used in almost every game
- [ ] Scrollable text / log view widget — integrate `MessageLog` directly into `UIContext`
- [ ] Tooltip support
- [ ] Modal dialog with backdrop
- [ ] Number input / slider widget
- [ ] Nested panels / tabs

## UI Layout

- [ ] Auto-size panel to its content
- [ ] Side-by-side panels that respond to content size
- [ ] Configurable column/row layout with padding that doesn't require manual positioning

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
