/// @file fov.hpp
/// @brief Field-of-view computation using recursive shadowcasting.
///
/// Provides `compute_fov()` — a pure function that marks which grid cells are
/// visible from an origin within a given radius.  The algorithm is a classic
/// **recursive shadowcasting** implementation (adapted from Björn Bergström's
/// description) that runs in O(radius²) time and handles all eight octants
/// symmetrically.
///
/// ## Design
///
/// - **No ECS dependency** — works with any map representation.
/// - **Callable opacity query** — you supply a lambda `bool(IVec2)` that
///   returns `true` when a cell blocks light.  This decouples the algorithm
///   from your tile enum or map type entirely.
/// - **Output via callable** — results are delivered through a `void(IVec2)`
///   callback.  Write to a `Grid<VisState>`, a `std::unordered_set`, a bitset,
///   or anything else you like.
/// - **`VisState` helper** — an optional three-state enum (`Unseen`, `Revealed`,
///   `Visible`) for the common roguelike pattern of tracking which tiles the
///   player has ever seen vs. currently sees.
///
/// ## Quick-start
///
/// @code
/// #include <xebble/fov.hpp>
/// #include <xebble/grid.hpp>
/// using namespace xebble;
///
/// enum class Tile { Floor, Wall };
/// Grid<Tile>    map(80, 25, Tile::Wall);
/// Grid<VisState> vis(80, 25, VisState::Unseen);
///
/// // Carve a room and some corridors, place the player…
/// IVec2 player{10, 12};
///
/// // Each turn: reset currently visible, then recompute.
/// vis.fill(VisState::Unseen);   // start fresh each turn
///
/// compute_fov(
///     player, /*radius=*/8,
///     [&](IVec2 p){ return map.in_bounds(p) && map[p] == Tile::Wall; },
///     [&](IVec2 p){
///         if (map.in_bounds(p))
///             vis[p] = VisState::Visible;
///     });
/// @endcode
///
/// ## Visibility states pattern
///
/// @code
/// // Keep a persistent "revealed" grid alongside the per-turn "visible" one.
/// Grid<bool>     revealed(80, 25, false);
/// Grid<VisState> vis(80, 25, VisState::Unseen);
///
/// auto update_fov = [&](IVec2 origin, int radius) {
///     // Mark everything currently visible as merely revealed.
///     for (int y = 0; y < vis.height(); ++y)
///         for (int x = 0; x < vis.width(); ++x) {
///             IVec2 p{x,y};
///             if (vis[p] == VisState::Visible) vis[p] = VisState::Revealed;
///         }
///
///     compute_fov(origin, radius,
///         [&](IVec2 p){ return !map.in_bounds(p) || map[p] == Tile::Wall; },
///         [&](IVec2 p){
///             if (!map.in_bounds(p)) return;
///             vis[p]      = VisState::Visible;
///             revealed[p] = true;
///         });
/// };
/// @endcode
#pragma once

#include <xebble/grid.hpp>

#include <functional>

namespace xebble {

// ---------------------------------------------------------------------------
// VisState — three-state visibility for roguelike tile rendering
// ---------------------------------------------------------------------------

/// @brief Visibility state for a single map cell.
///
/// Used with `compute_fov()` to implement the classic roguelike three-state
/// display: tiles the player has never seen are hidden, tiles seen in a
/// previous turn are drawn dimly, and currently visible tiles are fully lit.
///
/// @code
/// Grid<VisState> vis(map_w, map_h, VisState::Unseen);
///
/// // In your render system:
/// map.each([&](IVec2 p, Tile t) {
///     switch (vis[p]) {
///         case VisState::Unseen:   break;  // draw nothing
///         case VisState::Revealed: draw_tile_dim(p, t); break;
///         case VisState::Visible:  draw_tile_lit(p, t); break;
///     }
/// });
/// @endcode
enum class VisState : uint8_t {
    Unseen   = 0, ///< Never seen by the observer.
    Revealed = 1, ///< Seen in a previous turn (remembered but not currently lit).
    Visible  = 2, ///< Currently in line-of-sight.
};

// ---------------------------------------------------------------------------
// compute_fov — recursive shadowcasting
// ---------------------------------------------------------------------------

/// @brief Compute field of view from @p origin within @p radius using
///        recursive shadowcasting.
///
/// For every cell visible from @p origin, @p mark is called exactly once.
/// The origin cell itself is always marked visible.
///
/// @param origin   The observer's position (grid cell).
/// @param radius   Maximum sight distance in cells (Euclidean).
/// @param blocks   `bool(IVec2)` — return `true` if the cell is opaque.
/// @param mark     `void(IVec2)` — called for each visible cell.
void compute_fov(
        IVec2 origin,
        int   radius,
        const std::function<bool(IVec2)>& blocks,
        const std::function<void(IVec2)>& mark);

/// @brief Convenience overload: writes results directly into a `Grid<VisState>`.
///
/// Sets every in-bounds visible cell to `VisState::Visible`.
/// Out-of-bounds cells produced by the algorithm are silently ignored.
///
/// @param origin   Observer position.
/// @param radius   Sight radius.
/// @param blocks   Opacity predicate.
/// @param vis      Output grid — only in-bounds cells are written.
///
/// @code
/// Grid<VisState> vis(map_w, map_h, VisState::Revealed);  // reset each turn
/// compute_fov(player, 8,
///     [&](IVec2 p){ return !map.in_bounds(p) || map[p] == Tile::Wall; },
///     vis);
/// @endcode
/// @brief Convenience overload: writes results directly into a `Grid<VisState>`.
void compute_fov(
        IVec2 origin,
        int   radius,
        const std::function<bool(IVec2)>& blocks,
        Grid<VisState>& vis);

} // namespace xebble
