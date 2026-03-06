# Xebble TODO

Future features beyond the core renderer and ECS. The goal is for Xebble to handle all the annoying infrastructure so the game developer can focus entirely on gameplay and UI.

---

## Scene Stack

A stack-based scene manager where each scene is its own World. The topmost scene receives input and updates; scenes below can optionally continue drawing (for overlays like pause menus or inventory screens). Scenes are defined as factories that produce a World, and systems request transitions (push, pop, replace) through a resource that `run()` processes between ticks. Data is passed between scenes via a payload on the transition command. `run()` accepts a `SceneRouter` instead of a raw World, with a map of named scene factories and an initial scene name.

## UI System

A retained-mode widget tree for building menus, panels, buttons, edit boxes, dropdowns, scrollable lists, and other interface elements. Widgets are drawn using tiles from the game's spritesheet via a theme that maps widget parts (panel borders, button states, cursors, scrollbars) to tile indices. The theme is defined in the TOML asset manifest alongside spritesheets and fonts. The UI tree lives as a resource on a World, with systems handling input routing and rendering. This is a first-class Xebble module parallel to the ECS, not built on top of it — UI widgets form a tree with layout and focus semantics that don't map well to entity-component patterns.

## FOV / Visibility

Field-of-view computation using shadowcasting. Given a grid, an origin, and a radius, returns the set of visible positions. Exposed as pure functions that take a callable for opacity queries, so they work with any map representation. Should integrate with tilemap rendering to support common roguelike visibility states: unseen (hidden), revealed but not currently visible (dimmed), and visible (fully lit).

## Pathfinding

A* shortest-path and Dijkstra map computation. Dijkstra maps are a roguelike staple used for AI movement (move toward player), fleeing (move away from threat), autoexplore (move toward nearest unseen tile), and heat maps. Exposed as pure functions that take a grid size and a cost callable. The user provides the walkability/cost logic; Xebble provides the algorithms. Returns paths as coordinate vectors and Dijkstra maps as grids with downhill-step queries.

## Grid Utilities

A collection of 2D grid primitives: adjacency iteration (4-way, 8-way), Bresenham line drawing, distance metrics (Chebyshev, Manhattan, Euclidean), flood fill, rectangle operations, and coordinate math. These are individually trivial but collectively tedious to rewrite for every project. Exposed as free functions and a `Grid<T>` container with built-in neighbor iteration. No ECS dependency.

## Turn Scheduling

An abstract turn scheduler supporting common roguelike turn models: simple alternating (player acts, then all monsters), energy-based (entities accumulate energy and act when they hit a threshold), and initiative-based (sorted queue by speed). The scheduler is a value type that systems use to determine who acts next. It doesn't know about the ECS directly — systems query it and drive entity updates accordingly.

## Random Number Generation

A seedable, deterministic RNG with roguelike-friendly conveniences: dice notation parsing (`3d6+2`), weighted random selection from tables, shuffling, and range sampling. Uses a fast PRNG (PCG or xoshiro) under the hood. Crucially, the RNG state is serializable so it can be saved and restored for deterministic replays or save files. A value type — the user decides whether to store it as a World resource, pass it around, or keep multiple independent generators.

## Save / Load (Serialization)

World serialization — the ability to snapshot all entities, components, and resources to a binary blob and restore them later. This makes save/load trivial: serialize the World, write to disk, deserialize to resume. Requires components to opt into serialization via a trait or registration step. This is painful to bolt on after the fact, so the registration mechanism should be designed early even if full implementation comes later.

## Procedural Generation Primitives

Building blocks for dungeon and map generation: BSP tree splitting, cellular automata step functions, drunk walk (random walk with constraints), Poisson disk sampling, Voronoi diagrams, and wave function collapse on a grid. These are not complete dungeon generators — they're the Lego bricks that every roguelike procgen system is built from. Exposed as pure functions and small utility types with no ECS dependency.

## Message Log

A scrollable, color-coded message log for game messages ("The kobold hits you for 5 damage"). Supports message history, automatic deduplication with counts ("The kobold hits you x3"), scrollback, and filtering by category. This is UI-adjacent but specific enough to roguelikes to warrant its own component rather than being built from generic UI widgets every time.
