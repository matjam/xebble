/// @file procgen.hpp
/// @brief Procedural generation primitives for roguelike dungeon construction.
///
/// Pure functions and small value types — no ECS, no renderer dependency.
/// These are the building blocks; you assemble them into generators.
///
/// | Function | Description |
/// |---|---|
/// | `bsp_split`     | Recursively partition a rectangle into a BSP tree |
/// | `cellular_step` | One generation of Conway-style cellular automata |
/// | `drunkard_walk` | Random walk that carves floor tiles |
/// | `place_rooms`   | Randomly place non-overlapping rooms in a grid |
/// | `connect_rooms` | Connect room centres with L-shaped corridors |
///
/// ## Typical dungeon pipeline
///
/// @code
/// #include <xebble/procgen.hpp>
/// #include <xebble/rng.hpp>
/// using namespace xebble;
///
/// Rng rng(seed);
/// Grid<bool> map(80, 25, true);  // true = wall
///
/// // Option A — room-based dungeon:
/// auto rooms = place_rooms(map, 6, 10, 4, 8, rng, 30);
/// for (auto& r : rooms)
///     rect_for_each(r, [&](IVec2 p){ map[p] = false; });  // carve floor
/// connect_rooms(rooms, map, rng);
///
/// // Option B — cave via cellular automata:
/// // Seed ~45% of cells as floor.
/// for (int y = 1; y < 24; ++y)
///     for (int x = 1; x < 79; ++x)
///         map[IVec2{x,y}] = rng.chance(0.45f);
/// // Smooth for 5 generations.
/// for (int i = 0; i < 5; ++i) map = cellular_step(map, 4);
///
/// // Option C — drunkard walk cave:
/// drunkard_walk(map, {40,12}, 600, rng);
/// @endcode
#pragma once

#include <xebble/grid.hpp>
#include <xebble/rng.hpp>

#include <algorithm>
#include <memory>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// BSP tree
// ---------------------------------------------------------------------------

/// @brief A node in a Binary Space Partition tree.
///
/// Leaves are rooms (no children). Internal nodes carry two children and a
/// split axis/position.  Use `bsp_split()` to build the tree, then walk the
/// leaf nodes to place rooms.
///
/// @code
/// BSPNode root{IRect{0,0,80,25}};
/// bsp_split(root, rng, /*min_size=*/6);
///
/// // Collect leaf rectangles.
/// std::vector<IRect> leaves;
/// root.each_leaf([&](const BSPNode& n){ leaves.push_back(n.rect); });
/// @endcode
struct BSPNode {
    IRect rect;                               ///< Rectangle covered by this node.
    std::unique_ptr<BSPNode> left = nullptr;  ///< Left/top child (or null for leaf).
    std::unique_ptr<BSPNode> right = nullptr; ///< Right/bottom child (or null for leaf).

    /// @brief True if this node is a leaf (no further splits).
    [[nodiscard]] bool is_leaf() const { return !left && !right; }

    /// @brief Visit every leaf node depth-first.
    template<typename Fn>
    void each_leaf(Fn&& fn) const {
        if (is_leaf()) {
            fn(*this);
            return;
        }
        if (left)
            left->each_leaf(fn);
        if (right)
            right->each_leaf(fn);
    }
};

/// @brief Recursively split @p node using Binary Space Partitioning.
///
/// Splits are made along a randomly chosen axis (horizontal or vertical).
/// Splitting stops when either dimension of the resulting sub-rectangle
/// would fall below @p min_size.
///
/// @param node      Root node to split (modified in place).
/// @param rng       Random number generator.
/// @param min_size  Minimum width and height of any child node.
///
/// @code
/// BSPNode root{IRect{1,1,78,23}};
/// bsp_split(root, rng, 8);
///
/// root.each_leaf([&](const BSPNode& leaf) {
///     // Shrink leaf rect by 1–2 cells to create a room with walls.
///     IRect room = leaf.rect.expand(-1);
///     rect_for_each(room, [&](IVec2 p){ map[p] = Tile::Floor; });
/// });
/// @endcode
inline void bsp_split(BSPNode& node, Rng& rng, int min_size = 6) {
    const IRect& r = node.rect;
    bool can_h = r.h >= min_size * 2;
    bool can_v = r.w >= min_size * 2;
    if (!can_h && !can_v)
        return;

    bool split_h = can_h && (!can_v || rng.coin_flip());

    if (split_h) {
        int split = rng.range(r.y + min_size, r.y + r.h - min_size);
        node.left = std::make_unique<BSPNode>(BSPNode{IRect{r.x, r.y, r.w, split - r.y}});
        node.right = std::make_unique<BSPNode>(BSPNode{IRect{r.x, split, r.w, r.y + r.h - split}});
    } else {
        int split = rng.range(r.x + min_size, r.x + r.w - min_size);
        node.left = std::make_unique<BSPNode>(BSPNode{IRect{r.x, r.y, split - r.x, r.h}});
        node.right = std::make_unique<BSPNode>(BSPNode{IRect{split, r.y, r.x + r.w - split, r.h}});
    }

    bsp_split(*node.left, rng, min_size);
    bsp_split(*node.right, rng, min_size);
}

// ---------------------------------------------------------------------------
// Cellular automata
// ---------------------------------------------------------------------------

/// @brief Apply one step of cellular automata smoothing to a boolean grid.
///
/// A cell becomes `false` (floor/open) if it has strictly fewer than
/// @p birth_threshold `true` (wall) neighbours among its 8 surrounding cells.
/// Otherwise it stays or becomes `true` (wall).
///
/// This rule produces natural-looking cave shapes when run 4–6 times on a
/// grid seeded with ~45–55% walls.
///
/// @param grid             Input grid (true = wall, false = floor).
/// @param birth_threshold  Wall-neighbour count below which a cell opens up
///                         (classic value: 4 or 5).
/// @return                 New grid after one generation.
///
/// @code
/// Grid<bool> cave(80, 25, false);
/// // Seed ~50% walls (skip border).
/// for (int y = 1; y < 24; ++y)
///     for (int x = 1; x < 79; ++x)
///         cave[IVec2{x,y}] = rng.coin_flip();
///
/// // Smooth 5× with the classic cave threshold.
/// for (int i = 0; i < 5; ++i) cave = cellular_step(cave, 4);
/// @endcode
[[nodiscard]] inline Grid<bool> cellular_step(const Grid<bool>& grid, int birth_threshold = 4) {
    Grid<bool> result(grid.width(), grid.height(), true);

    for (int y = 0; y < grid.height(); ++y) {
        for (int x = 0; x < grid.width(); ++x) {
            IVec2 p{x, y};
            // Border cells are always walls.
            if (x == 0 || y == 0 || x == grid.width() - 1 || y == grid.height() - 1) {
                result[p] = true;
                continue;
            }
            int wall_count = 0;
            for (IVec2 nb : neighbors8(p, grid))
                if (grid[nb])
                    ++wall_count;
            // Out-of-bounds neighbours are treated as walls.
            // neighbors8 already clips to grid bounds, so count also OOB.
            int oob = 8 - static_cast<int>(neighbors8(p, grid).size());
            wall_count += oob;
            result[p] = (wall_count >= birth_threshold);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Drunkard walk
// ---------------------------------------------------------------------------

/// @brief Carve a cave by random-walking through the grid.
///
/// Starts at @p origin, takes @p steps random steps (4-directional), and sets
/// each visited cell to `false` (floor/open).  The walker bounces off the
/// border (clamped to [1, width-2] × [1, height-2]).
///
/// @param grid    Grid to carve into (true = wall, false = floor).
/// @param origin  Starting position of the walker.
/// @param steps   Number of steps to take.
/// @param rng     Random number generator.
///
/// @code
/// Grid<bool> map(80, 25, true);
/// drunkard_walk(map, {40, 12}, 500, rng);
/// @endcode
inline void drunkard_walk(Grid<bool>& grid, IVec2 origin, int steps, Rng& rng) {
    IVec2 pos = origin;
    static constexpr int dx[4] = {-1, 1, 0, 0};
    static constexpr int dy[4] = {0, 0, -1, 1};

    for (int i = 0; i < steps; ++i) {
        grid[pos] = false;
        int d = rng.range(3);
        pos.x = std::clamp(pos.x + dx[d], 1, grid.width() - 2);
        pos.y = std::clamp(pos.y + dy[d], 1, grid.height() - 2);
    }
}

// ---------------------------------------------------------------------------
// Room placement
// ---------------------------------------------------------------------------

/// @brief Attempt to place @p attempts non-overlapping rooms in @p grid.
///
/// Each room is a randomly sized rectangle (within [min_w,max_w] ×
/// [min_h,max_h]) placed at a random position within the grid.  Rooms are
/// accepted only if they don't overlap any previously placed room (with 1-cell
/// gap between them).
///
/// Returns the list of successfully placed room rectangles. The grid itself is
/// **not** modified — carve the rooms yourself using `rect_for_each`.
///
/// @param grid     Map grid (used only for size bounds).
/// @param min_w    Minimum room width.
/// @param max_w    Maximum room width.
/// @param min_h    Minimum room height.
/// @param max_h    Maximum room height.
/// @param rng      Random number generator.
/// @param attempts Number of random placement attempts.
/// @return         List of placed room rectangles.
///
/// @code
/// auto rooms = place_rooms(map, 5, 12, 4, 9, rng, 40);
/// for (auto& r : rooms)
///     rect_for_each(r, [&](IVec2 p){ map[p] = Tile::Floor; });
/// connect_rooms(rooms, map, rng);
/// @endcode
[[nodiscard]] inline std::vector<IRect> place_rooms(const Grid<bool>& grid, int min_w, int max_w,
                                                    int min_h, int max_h, Rng& rng,
                                                    int attempts = 30) {
    std::vector<IRect> rooms;

    for (int attempt = 0; attempt < attempts; ++attempt) {
        int w = rng.range(min_w, max_w);
        int h = rng.range(min_h, max_h);
        int x = rng.range(1, grid.width() - w - 1);
        int y = rng.range(1, grid.height() - h - 1);
        IRect candidate{x, y, w, h};

        // Expand by 1 for the gap check.
        IRect padded{x - 1, y - 1, w + 2, h + 2};

        bool overlaps = false;
        for (auto& r : rooms) {
            // Check if padded and existing room intersect.
            if (padded.x < r.x + r.w && padded.x + padded.w > r.x && padded.y < r.y + r.h &&
                padded.y + padded.h > r.y) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps)
            rooms.push_back(candidate);
    }
    return rooms;
}

// ---------------------------------------------------------------------------
// Room connection
// ---------------------------------------------------------------------------

/// @brief Connect room centres with L-shaped corridors.
///
/// For each consecutive pair of rooms in @p rooms, draws an L-shaped path
/// (horizontal then vertical, or vertical then horizontal — chosen randomly)
/// by setting every cell along the corridor to `false` (floor/open).
///
/// @param rooms  Rooms in the order they should be connected.
/// @param grid   Grid to carve corridors into.
/// @param rng    Random number generator.
///
/// @code
/// connect_rooms(rooms, map, rng);
/// @endcode
inline void connect_rooms(const std::vector<IRect>& rooms, Grid<bool>& grid, Rng& rng) {
    auto centre = [](const IRect& r) -> IVec2 {
        return {r.x + r.w / 2, r.y + r.h / 2};
    };

    for (size_t i = 1; i < rooms.size(); ++i) {
        IVec2 a = centre(rooms[i - 1]);
        IVec2 b = centre(rooms[i]);

        // Clamp corridor to safe bounds.
        auto carve_h = [&](int y, int x1, int x2) {
            int lo = std::min(x1, x2), hi = std::max(x1, x2);
            for (int x = lo; x <= hi; ++x) {
                IVec2 p{x, y};
                if (grid.in_bounds(p))
                    grid[p] = false;
            }
        };
        auto carve_v = [&](int x, int y1, int y2) {
            int lo = std::min(y1, y2), hi = std::max(y1, y2);
            for (int y = lo; y <= hi; ++y) {
                IVec2 p{x, y};
                if (grid.in_bounds(p))
                    grid[p] = false;
            }
        };

        if (rng.coin_flip()) {
            carve_h(a.y, a.x, b.x);
            carve_v(b.x, a.y, b.y);
        } else {
            carve_v(a.x, a.y, b.y);
            carve_h(b.y, a.x, b.x);
        }
    }
}

} // namespace xebble
