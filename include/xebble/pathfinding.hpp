/// @file pathfinding.hpp
/// @brief A* shortest-path and Dijkstra map computation for roguelikes.
///
/// Both algorithms are pure functions with no ECS dependency. You supply the
/// grid dimensions and a cost/walkability callable; Xebble supplies the search.
///
/// ## A* (`find_path`)
///
/// Returns the shortest walkable path between two cells as a vector of
/// coordinates (including start and goal), or an empty vector if no path
/// exists. The heuristic is Chebyshev distance (8-directional movement).
/// Pass a 4-neighbour cost function to restrict movement to cardinal directions.
///
/// @code
/// #include <xebble/pathfinding.hpp>
/// using namespace xebble;
///
/// Grid<bool> walls(80, 25, false);
/// // … carve dungeon …
///
/// auto path = find_path(
///     {1, 1}, {30, 12},
///     80, 25,
///     [&](IVec2 from, IVec2 to) -> float {
///         // Return a negative value to mark a cell unwalkable.
///         if (!walls.in_bounds(to) || walls[to]) return -1.0f;
///         return 1.0f;  // uniform cost
///     });
///
/// if (!path.empty()) {
///     // path[0] == start, path.back() == goal
///     follow_path(entity, path);
/// }
/// @endcode
///
/// ## Dijkstra maps (`dijkstra_map`)
///
/// A Dijkstra map is a distance grid — every cell holds the minimum cost to
/// reach it from any of a set of goal cells. It is a roguelike staple:
///
/// - **Move toward player**: goals = {player_pos}; monsters step to lowest-cost neighbour.
/// - **Flee**: negate the map values and step to the new lowest value.
/// - **Autoexplore**: goals = all unseen cells; player steps toward nearest unseen tile.
/// - **Heat maps**: goals = fire/noise sources; propagates outward.
///
/// @code
/// // Build a Dijkstra map with the player as the single goal.
/// auto dmap = dijkstra_map(
///     80, 25,
///     {player_pos},
///     [&](IVec2 from, IVec2 to) -> float {
///         if (!map.in_bounds(to) || map[to] == Tile::Wall) return -1.0f;
///         return 1.0f;
///     });
///
/// // Monster AI: step to the lowest-cost walkable neighbour.
/// IVec2 next = dijkstra_step(monster_pos, dmap, 80, 25,
///     [&](IVec2, IVec2 to) -> float {
///         return map[to] == Tile::Wall ? -1.0f : 1.0f;
///     });
/// monster_pos = next;
///
/// // Flee map: negate and step.
/// auto flee = dmap;
/// for (auto& v : flee.data()) if (v < PathCostInfinity) v = -v;
/// IVec2 flee_step = dijkstra_step(monster_pos, flee, 80, 25,
///     [&](IVec2, IVec2 to) -> float {
///         return map[to] == Tile::Wall ? -1.0f : 1.0f;
///     });
/// @endcode
#pragma once

#include <xebble/grid.hpp>

#include <functional>
#include <limits>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Sentinel value stored in Dijkstra maps for unreachable cells.
inline constexpr float PathCostInfinity = std::numeric_limits<float>::infinity();

// ---------------------------------------------------------------------------
// Cost function type
// ---------------------------------------------------------------------------

/// @brief Callable signature for movement cost queries.
///
/// Return a **positive** value for the cost of entering `to` from `from`.
/// Return a **negative** value (e.g. `-1.0f`) to mark `to` as impassable.
///
/// @code
/// // Uniform-cost 8-directional movement:
/// auto cost = [&](IVec2 /*from*/, IVec2 to) -> float {
///     if (!map.in_bounds(to) || map[to] == Tile::Wall) return -1.0f;
///     return 1.0f;
/// };
///
/// // Diagonal moves cost more:
/// auto cost = [&](IVec2 from, IVec2 to) -> float {
///     if (!map.in_bounds(to) || map[to] == Tile::Wall) return -1.0f;
///     bool diag = (to.x != from.x) && (to.y != from.y);
///     return diag ? 1.414f : 1.0f;
/// };
///
/// // Terrain costs (swamp = slow, road = fast):
/// auto cost = [&](IVec2 /*from*/, IVec2 to) -> float {
///     if (!map.in_bounds(to) || map[to] == Tile::Wall) return -1.0f;
///     if (map[to] == Tile::Swamp) return 3.0f;
///     if (map[to] == Tile::Road)  return 0.5f;
///     return 1.0f;
/// };
/// @endcode
using CostFn = std::function<float(IVec2 from, IVec2 to)>;

// ---------------------------------------------------------------------------
// find_path — A* shortest path
// ---------------------------------------------------------------------------

/// @brief Find the shortest path from @p start to @p goal using A*.
///
/// Returns a vector of cells forming the path, **including both `start` and
/// `goal`**, in order from start to goal.  Returns an empty vector if no
/// path exists (blocked or out of bounds).
///
/// The search uses 8-directional (Chebyshev) neighbours by default. To
/// restrict to 4-directional movement return `-1` for diagonal moves in your
/// cost function.
///
/// @param start    Starting cell.
/// @param goal     Target cell.
/// @param width    Map width in cells.
/// @param height   Map height in cells.
/// @param cost_fn  Movement cost callable — see `CostFn`.
/// @return         Path from start to goal (inclusive), or empty if unreachable.
///
/// @code
/// auto path = find_path({1,1}, {20,10}, map_w, map_h,
///     [&](IVec2, IVec2 to) -> float {
///         return (!map.in_bounds(to) || map[to] == Tile::Wall) ? -1.0f : 1.0f;
///     });
///
/// if (path.empty()) { log("No path found!"); }
/// else { entity_follow_path(monster, path); }
/// @endcode
std::vector<IVec2> find_path(
        IVec2 start, IVec2 goal,
        int width, int height,
        const CostFn& cost_fn);

// ---------------------------------------------------------------------------
// dijkstra_map — multi-source Dijkstra distance grid
// ---------------------------------------------------------------------------

/// @brief Compute a Dijkstra distance map from a set of goal cells.
///
/// Returns a `Grid<float>` the same size as the map (`width` × `height`).
/// Each cell contains the minimum movement cost to reach it from any goal.
/// Unreachable cells hold `PathCostInfinity`.
///
/// @param width    Map width in cells.
/// @param height   Map height in cells.
/// @param goals    Seed cells (zero cost). May be empty (all cells unreachable).
/// @param cost_fn  Movement cost callable — negative means impassable.
/// @return         Distance grid.
///
/// @code
/// // Single-source: distance from player.
/// auto dmap = dijkstra_map(w, h, {player_pos},
///     [&](IVec2, IVec2 to) -> float {
///         return map[to] == Tile::Wall ? -1.0f : 1.0f;
///     });
///
/// // Multi-source: distance to nearest exit.
/// auto dmap = dijkstra_map(w, h, exit_positions, cost);
///
/// // Use the map: monster steps toward player.
/// // Iterate 8 neighbours, pick lowest-cost walkable one.
/// @endcode
Grid<float> dijkstra_map(
        int width, int height,
        const std::vector<IVec2>& goals,
        const CostFn& cost_fn);

// ---------------------------------------------------------------------------
// dijkstra_step — single greedy step on a Dijkstra map
// ---------------------------------------------------------------------------

/// @brief Return the best neighbour to move to from @p pos on a Dijkstra map.
///
/// Picks the walkable 8-directional neighbour of @p pos with the lowest value
/// in @p dmap.  Returns @p pos unchanged if no improvement is possible (the
/// agent is already at a local minimum, or surrounded by walls).
///
/// @param pos      Current position.
/// @param dmap     Dijkstra distance grid (from `dijkstra_map()`).
/// @param width    Map width (must match dmap).
/// @param height   Map height (must match dmap).
/// @param cost_fn  Movement cost callable — negative means impassable.
/// @return         Best next cell, or @p pos if stuck.
///
/// @code
/// // Move monster one step toward player each turn.
/// monster_pos = dijkstra_step(monster_pos, player_dmap, w, h, cost);
///
/// // Flee: negate the map first.
/// Grid<float> flee = player_dmap;
/// for (float& v : flee.data())
///     if (v < PathCostInfinity) v = -v;
/// monster_pos = dijkstra_step(monster_pos, flee, w, h, cost);
/// @endcode
IVec2 dijkstra_step(
        IVec2 pos,
        const Grid<float>& dmap,
        int width, int height,
        const CostFn& cost_fn);

} // namespace xebble
