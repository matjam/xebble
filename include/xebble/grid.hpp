/// @file grid.hpp
/// @brief 2D grid utilities: integer coordinates, Grid<T> container, adjacency
///        iteration, Bresenham line drawing, distance metrics, flood fill, and
///        rectangle operations.
///
/// All functionality is header-only and has no dependency on the ECS, renderer,
/// or any other Xebble subsystem. It can be used standalone.
///
/// ---
///
/// ## Quick-start example
///
/// A minimal roguelike dungeon map using these types:
///
/// @code
/// #include <xebble/grid.hpp>
/// using namespace xebble;
///
/// enum class Tile { Floor, Wall };
///
/// // Build a 40x25 map filled with walls, carve a 10x10 room in the centre.
/// Grid<Tile> map(40, 25, Tile::Wall);
/// IRect room{15, 7, 10, 10};
/// rect_for_each(room, [&](IVec2 p){ map[p] = Tile::Floor; });
///
/// // Check 4-way passability from the room centre.
/// IVec2 centre{20, 12};
/// for (IVec2 nb : neighbors4(centre, map)) {
///     if (map[nb] == Tile::Floor) { /* can step here */ }
/// }
///
/// // Find all floor cells reachable from the centre via 4-connectivity.
/// auto reachable = flood_fill(centre, map,
///     [&](IVec2 p){ return map[p] == Tile::Floor; });
/// @endcode
#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <stdexcept>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// IVec2 — integer 2D coordinate
// ---------------------------------------------------------------------------

/// @brief Integer 2D coordinate used for addressing grid cells.
///
/// All grid functions use IVec2 for positions so there is no ambiguity
/// between (column, row) and (x, y) — both mean the same thing here:
/// x increases rightward, y increases downward (screen convention).
///
/// @code
/// IVec2 player{5, 3};  // column 5, row 3
/// IVec2 delta{1, 0};   // one step right
/// IVec2 next = player + delta;  // {6, 3}
///
/// // Walk a path stored as a list of offsets.
/// std::vector<IVec2> offsets = {{1,0},{0,1},{-1,0}};
/// for (IVec2 d : offsets)
///     player += d;
/// @endcode
struct IVec2 {
    int32_t x = 0;  ///< Column (increases rightward).
    int32_t y = 0;  ///< Row    (increases downward).

    bool operator==(const IVec2&) const = default;
    bool operator!=(const IVec2&) const = default;

    IVec2 operator+(IVec2 rhs) const noexcept { return {x + rhs.x, y + rhs.y}; }
    IVec2 operator-(IVec2 rhs) const noexcept { return {x - rhs.x, y - rhs.y}; }
    IVec2 operator*(int32_t s) const noexcept { return {x * s, y * s}; }
    IVec2& operator+=(IVec2 rhs) noexcept { x += rhs.x; y += rhs.y; return *this; }
    IVec2& operator-=(IVec2 rhs) noexcept { x -= rhs.x; y -= rhs.y; return *this; }
};

// ---------------------------------------------------------------------------
// IRect — integer axis-aligned rectangle
// ---------------------------------------------------------------------------

/// @brief Integer axis-aligned rectangle defined by a top-left corner and size.
///
/// IRect describes a region of grid cells. It follows the standard
/// half-open convention: the left and top edges are *inclusive*, while the
/// right and bottom edges are *exclusive* (i.e. right = x + w, bottom = y + h).
/// This makes width/height arithmetic exact and avoids off-by-one errors.
///
/// @code
/// // A 10-wide, 8-tall room starting at column 2, row 3.
/// IRect room{2, 3, 10, 8};
///
/// // Check whether the player is inside the room.
/// IVec2 player{5, 7};
/// if (room.contains(player)) { /* player is in the room */ }
///
/// // Compute the overlap between two rooms (for door placement, etc.).
/// IRect other{8, 5, 6, 6};
/// IRect shared = room.intersect(other);
/// if (shared.valid()) { /* rooms overlap — shared cells are in 'shared' */ }
///
/// // Add a 1-cell border around a room (e.g. to place wall tiles).
/// IRect with_border = room.expand(1);
/// @endcode
struct IRect {
    int32_t x = 0;   ///< Left edge (inclusive column).
    int32_t y = 0;   ///< Top  edge (inclusive row).
    int32_t w = 0;   ///< Width  in cells.
    int32_t h = 0;   ///< Height in cells.

    bool operator==(const IRect&) const = default;

    /// @brief Exclusive right edge: the first column to the right of this rect.
    ///
    /// Iterating `for (int x = r.x; x < r.right(); ++x)` visits every column.
    int32_t right()  const noexcept { return x + w; }

    /// @brief Exclusive bottom edge: the first row below this rect.
    ///
    /// Iterating `for (int y = r.y; y < r.bottom(); ++y)` visits every row.
    int32_t bottom() const noexcept { return y + h; }

    /// @brief True if the rectangle has positive area (w > 0 && h > 0).
    ///
    /// Use this to check whether an intersect() or rect_clamp() result is
    /// non-empty before iterating over it.
    bool valid() const noexcept { return w > 0 && h > 0; }

    /// @brief True if @p pos is inside this rectangle.
    ///
    /// Uses the half-open convention: the left/top edges are included and the
    /// right/bottom edges are excluded, matching how iteration works.
    ///
    /// @code
    /// IRect r{0, 0, 4, 4};  // cells (0,0)..(3,3)
    /// r.contains({3, 3});   // true  — last valid cell
    /// r.contains({4, 0});   // false — right edge is excluded
    /// @endcode
    bool contains(IVec2 pos) const noexcept {
        return pos.x >= x && pos.x < right() && pos.y >= y && pos.y < bottom();
    }

    /// @brief Return the intersection of this rectangle with @p other.
    ///
    /// If the rectangles don't overlap the result has zero size (valid() == false).
    ///
    /// @code
    /// IRect a{0, 0, 5, 5};
    /// IRect b{3, 3, 5, 5};
    /// IRect overlap = a.intersect(b);  // {3, 3, 2, 2}
    ///
    /// IRect c{10, 10, 2, 2};
    /// IRect none = a.intersect(c);    // valid() == false — no overlap
    /// @endcode
    IRect intersect(IRect other) const noexcept {
        int32_t nx = std::max(x, other.x);
        int32_t ny = std::max(y, other.y);
        int32_t nr = std::min(right(),  other.right());
        int32_t nb = std::min(bottom(), other.bottom());
        if (nr <= nx || nb <= ny) return {nx, ny, 0, 0};
        return {nx, ny, nr - nx, nb - ny};
    }

    /// @brief Return a copy of this rectangle expanded by @p delta on all sides.
    ///
    /// Positive delta grows the rect; negative delta shrinks it.
    /// The result is not clamped — use rect_clamp() afterwards if needed.
    ///
    /// @code
    /// IRect room{5, 5, 8, 6};
    ///
    /// // Expand by 1 to get the ring of wall cells around the room.
    /// IRect with_walls = room.expand(1);  // {4, 4, 10, 8}
    ///
    /// // Shrink by 1 to get an inner margin (e.g. keep monsters away from walls).
    /// IRect inner = room.expand(-1);      // {6, 6, 6, 4}
    /// @endcode
    IRect expand(int32_t delta) const noexcept {
        return {x - delta, y - delta, w + 2 * delta, h + 2 * delta};
    }
};

// ---------------------------------------------------------------------------
// Grid<T> — flat 2D container
// ---------------------------------------------------------------------------

/// @brief A flat 2D array with integer indexing. Owns its storage.
///
/// `Grid<T>` is the core container for any per-cell map data: tile types,
/// passability flags, light levels, Dijkstra distances, FOV state, etc.
/// Cells are stored in row-major order so that iterating by row is
/// cache-friendly.
///
/// Access patterns:
/// - `grid[pos]`    — unchecked read/write (use when pos is known in-bounds).
/// - `grid.at(pos)` — bounds-checked; returns `nullopt` if out of bounds.
/// - Range-for      — iterates all cells in row-major order.
///
/// @code
/// enum class Tile { Floor, Wall };
///
/// // Create a 40-wide, 25-tall map, all walls.
/// Grid<Tile> map(40, 25, Tile::Wall);
///
/// // Carve a floor area.
/// for (int y = 2; y < 10; ++y)
///     for (int x = 2; x < 12; ++x)
///         map[{x, y}] = Tile::Floor;
///
/// // Safe read from user input (e.g. mouse cursor position).
/// IVec2 cursor = get_mouse_tile();
/// if (auto cell = map.at(cursor)) {
///     display_tooltip(cell->get());
/// }
///
/// // Iterate all cells (e.g. to serialise the map).
/// for (const Tile& t : map) { write(t); }
///
/// // Store a numeric value per cell (e.g. Dijkstra distance map).
/// Grid<int> dist(map.width(), map.height(), INT_MAX);
/// dist[{0, 0}] = 0;
/// @endcode
template<typename T>
class Grid {
public:
    /// @brief Construct a value-initialised grid (T{} per cell).
    ///
    /// For numeric types this zero-initialises all cells. For class types,
    /// the default constructor is called for each cell.
    ///
    /// @param width  Number of columns. Must be > 0.
    /// @param height Number of rows.    Must be > 0.
    Grid(int32_t width, int32_t height)
        : width_(width), height_(height), cells_(static_cast<size_t>(width * height))
    {
        assert(width > 0 && height > 0);
    }

    /// @brief Construct a grid with all cells initialised to @p fill.
    ///
    /// @param width  Number of columns. Must be > 0.
    /// @param height Number of rows.    Must be > 0.
    /// @param fill   Value to copy into every cell.
    ///
    /// @code
    /// // A passability grid — everything blocked by default.
    /// Grid<bool> passable(80, 50, false);
    ///
    /// // A heat-map initialised to "infinity".
    /// Grid<int> heat(80, 50, INT_MAX);
    /// @endcode
    Grid(int32_t width, int32_t height, const T& fill)
        : width_(width), height_(height),
          cells_(static_cast<size_t>(width * height), fill)
    {
        assert(width > 0 && height > 0);
    }

    int32_t width()  const noexcept { return width_; }
    int32_t height() const noexcept { return height_; }
    /// @brief Total number of cells (width * height).
    size_t  size()   const noexcept { return cells_.size(); }

    /// @brief Return true if @p pos lies within [0, width) × [0, height).
    ///
    /// Use this to guard `operator[]` calls on externally-supplied positions.
    ///
    /// @code
    /// IVec2 click = screen_to_tile(mouse_x, mouse_y);
    /// if (map.in_bounds(click)) {
    ///     process_tile(map[click]);
    /// }
    /// @endcode
    bool in_bounds(IVec2 pos) const noexcept {
        return pos.x >= 0 && pos.x < width_ && pos.y >= 0 && pos.y < height_;
    }

    /// @brief Return the flat storage index for @p pos (row-major).
    ///
    /// index = pos.y * width + pos.x
    ///
    /// Useful when interfacing with renderer APIs that expect a flat array
    /// (e.g. uploading tile indices to a TileMap layer as a span).
    size_t index_of(IVec2 pos) const noexcept {
        return static_cast<size_t>(pos.y * width_ + pos.x);
    }

    /// @brief Unchecked cell access by position.
    ///
    /// No bounds checking is performed. Use `at()` when the position may be
    /// out of bounds (e.g. from player input or pathfinding edge cases).
    ///
    /// @code
    /// map[{x, y}] = Tile::Floor;
    /// Tile t = map[player_pos];
    /// @endcode
    T& operator[](IVec2 pos) noexcept { return cells_[index_of(pos)]; }
    const T& operator[](IVec2 pos) const noexcept { return cells_[index_of(pos)]; }

    /// @brief Bounds-checked cell access. Returns nullopt if out of bounds.
    ///
    /// The returned reference wrapper becomes dangling if the Grid is moved or
    /// destroyed, so dereference it immediately.
    ///
    /// @code
    /// // Read a cell that might be out of bounds (e.g. AOE radius near the edge).
    /// for (IVec2 p : explosion_tiles) {
    ///     if (auto cell = map.at(p)) {
    ///         apply_damage(cell->get());
    ///     }
    /// }
    ///
    /// // Write via the checked accessor.
    /// if (auto cell = map.at(target)) {
    ///     cell->get() = Tile::Crater;
    /// }
    /// @endcode
    std::optional<std::reference_wrapper<T>> at(IVec2 pos) noexcept {
        if (!in_bounds(pos)) return std::nullopt;
        return cells_[index_of(pos)];
    }
    std::optional<std::reference_wrapper<const T>> at(IVec2 pos) const noexcept {
        if (!in_bounds(pos)) return std::nullopt;
        return cells_[index_of(pos)];
    }

    /// @brief Overwrite every cell with @p value.
    ///
    /// Useful for resetting computed grids between frames or turns.
    ///
    /// @code
    /// // Reset the FOV grid at the start of each turn before recomputing it.
    /// Grid<bool> visible(map.width(), map.height());
    /// // ... each turn:
    /// visible.fill(false);
    /// compute_fov(player_pos, radius, visible);
    /// @endcode
    void fill(const T& value) { std::fill(cells_.begin(), cells_.end(), value); }

    /// @brief Pointer to the raw flat storage (row-major, width*height elements).
    ///
    /// Useful for bulk uploads to renderer APIs (e.g. setting an entire TileMap
    /// layer from a Grid<uint32_t> of tile indices).
    ///
    /// @code
    /// Grid<uint32_t> indices(map.width(), map.height());
    /// // ... fill indices ...
    /// tilemap.set_layer(0, std::span(indices.data(), indices.size()));
    /// @endcode
    const T* data() const noexcept { return cells_.data(); }
    T*       data()       noexcept { return cells_.data(); }

    /// @brief Iterator range over all cells in row-major order.
    ///
    /// @code
    /// // Count wall tiles.
    /// int walls = std::count(map.begin(), map.end(), Tile::Wall);
    ///
    /// // Range-for syntax.
    /// for (Tile& t : map) { if (t == Tile::Door) t = Tile::Floor; }
    /// @endcode
    auto begin() noexcept { return cells_.begin(); }
    auto end()   noexcept { return cells_.end(); }
    auto begin() const noexcept { return cells_.begin(); }
    auto end()   const noexcept { return cells_.end(); }

private:
    int32_t width_;
    int32_t height_;
    std::vector<T> cells_;
};

// ---------------------------------------------------------------------------
// Grid<bool> explicit specialization
//
// std::vector<bool> is a space-optimised specialisation that does NOT return
// a real bool& from operator[]; it returns a proxy object.  This breaks any
// code that binds the result to a bool& (e.g. procgen, fov).  We specialise
// Grid<bool> to store uint8_t internally while exposing the identical public
// interface with real bool& references.
// ---------------------------------------------------------------------------

template<>
class Grid<bool> {
public:
    Grid(int32_t width, int32_t height)
        : width_(width), height_(height),
          cells_(static_cast<size_t>(width * height), 0)
    { assert(width > 0 && height > 0); }

    Grid(int32_t width, int32_t height, bool fill)
        : width_(width), height_(height),
          cells_(static_cast<size_t>(width * height), fill ? 1u : 0u)
    { assert(width > 0 && height > 0); }

    int32_t width()  const noexcept { return width_; }
    int32_t height() const noexcept { return height_; }
    size_t  size()   const noexcept { return cells_.size(); }

    bool in_bounds(IVec2 pos) const noexcept {
        return pos.x >= 0 && pos.x < width_ && pos.y >= 0 && pos.y < height_;
    }

    size_t index_of(IVec2 pos) const noexcept {
        return static_cast<size_t>(pos.y * width_ + pos.x);
    }

    // operator[] returns a real uint8_t&, reinterpreted as bool& for assignment.
    // Since sizeof(uint8_t) == sizeof(bool) on all supported platforms and
    // uint8_t stores 0/1 values here, the reinterpret_cast is well-defined for
    // the operations we perform (assign 0/1, read as bool).
    bool& operator[](IVec2 pos) noexcept {
        return reinterpret_cast<bool&>(cells_[index_of(pos)]);
    }
    const bool& operator[](IVec2 pos) const noexcept {
        return reinterpret_cast<const bool&>(cells_[index_of(pos)]);
    }

    std::optional<std::reference_wrapper<bool>> at(IVec2 pos) noexcept {
        if (!in_bounds(pos)) return std::nullopt;
        return reinterpret_cast<bool&>(cells_[index_of(pos)]);
    }
    std::optional<std::reference_wrapper<const bool>> at(IVec2 pos) const noexcept {
        if (!in_bounds(pos)) return std::nullopt;
        return reinterpret_cast<const bool&>(cells_[index_of(pos)]);
    }

    void fill(bool value) {
        std::fill(cells_.begin(), cells_.end(), value ? 1u : 0u);
    }

    // data() returns uint8_t* rather than bool* to avoid any aliasing issues.
    // Users relying on data() for bool grids should treat each byte as 0 or 1.
    const uint8_t* data() const noexcept { return cells_.data(); }
    uint8_t*       data()       noexcept { return cells_.data(); }

    // Iterators expose uint8_t references for performance; cast as needed.
    auto begin() noexcept       { return cells_.begin(); }
    auto end()   noexcept       { return cells_.end(); }
    auto begin() const noexcept { return cells_.begin(); }
    auto end()   const noexcept { return cells_.end(); }

private:
    int32_t width_;
    int32_t height_;
    std::vector<uint8_t> cells_;
};

// ---------------------------------------------------------------------------
// Adjacency iteration
// ---------------------------------------------------------------------------

/// @brief Return the up-to-4 cardinal (N/E/S/W) neighbours of @p pos that lie
///        within a grid of the given dimensions.
///
/// Use this for 4-directional movement systems (no diagonal stepping), FOV
/// expansion, or any algorithm where diagonal adjacency is not meaningful.
///
/// Cells are returned in N/E/S/W order. Only cells within
/// [0, grid_w) × [0, grid_h) are included, so the result has 2 elements at a
/// corner, 3 on an edge, and 4 in the interior.
///
/// @param pos     Cell to query.
/// @param grid_w  Grid width.
/// @param grid_h  Grid height.
/// @return Up to 4 neighbour positions.
///
/// @code
/// // Simple movement validation for a 4-directional roguelike.
/// Grid<Tile> map(40, 25, Tile::Wall);
///
/// bool can_move(IVec2 from, IVec2 to) {
///     // Only allow movement to cardinal neighbours that are floor.
///     for (IVec2 nb : neighbors4(from, map)) {
///         if (nb == to && map[nb] == Tile::Floor) return true;
///     }
///     return false;
/// }
///
/// // Spread fire: ignite every floor neighbour of a burning cell.
/// void spread_fire(IVec2 cell, Grid<Tile>& map) {
///     for (IVec2 nb : neighbors4(cell, map.width(), map.height())) {
///         if (map[nb] == Tile::Floor) map[nb] = Tile::Fire;
///     }
/// }
/// @endcode
std::vector<IVec2> neighbors4(IVec2 pos, int32_t grid_w, int32_t grid_h);

/// @brief Return the up-to-8 neighbours (cardinal + diagonal) of @p pos that
///        lie within a grid of the given dimensions.
///
/// Use this for 8-directional movement, Chebyshev-distance calculations, or
/// cellular-automata rules where diagonal cells count as adjacent.
///
/// Cells are returned in row-major order (top-left to bottom-right, skipping
/// the centre). The result has 3 elements at a corner, 5 on an edge, and 8
/// in the interior.
///
/// @param pos     Cell to query.
/// @param grid_w  Grid width.
/// @param grid_h  Grid height.
/// @return Up to 8 neighbour positions.
///
/// @code
/// // Cellular automaton smoothing step (cave generation).
/// // A cell becomes a wall if 5 or more of its 8 neighbours are walls.
/// Grid<Tile> smooth(const Grid<Tile>& src) {
///     Grid<Tile> dst = src;
///     for (int y = 0; y < src.height(); ++y) {
///         for (int x = 0; x < src.width(); ++x) {
///             IVec2 p{x, y};
///             int wall_count = 0;
///             for (IVec2 nb : neighbors8(p, src)) {
///                 if (src[nb] == Tile::Wall) ++wall_count;
///             }
///             dst[p] = (wall_count >= 5) ? Tile::Wall : Tile::Floor;
///         }
///     }
///     return dst;
/// }
/// @endcode
std::vector<IVec2> neighbors8(IVec2 pos, int32_t grid_w, int32_t grid_h);

/// @brief Grid<T> overload — deduces width and height from the grid.
///
/// @code
/// Grid<Tile> map(40, 25, Tile::Wall);
/// for (IVec2 nb : neighbors4(player, map)) { ... }
/// for (IVec2 nb : neighbors8(player, map)) { ... }
/// @endcode
template<typename T>
std::vector<IVec2> neighbors4(IVec2 pos, const Grid<T>& g) {
    return neighbors4(pos, g.width(), g.height());
}
template<typename T>
std::vector<IVec2> neighbors8(IVec2 pos, const Grid<T>& g) {
    return neighbors8(pos, g.width(), g.height());
}

// ---------------------------------------------------------------------------
// Bresenham line drawing
// ---------------------------------------------------------------------------

/// @brief Return all grid cells on the Bresenham line from @p a to @p b,
///        inclusive of both endpoints.
///
/// This is the standard integer line-drawing algorithm. It visits each cell
/// whose centre is closest to the ideal geometric line, producing a thin,
/// connected sequence of cells with no gaps or doubles.
///
/// Common uses:
/// - **Projectile paths** — check every cell between attacker and target for
///   obstructions before deciding if an arrow hits.
/// - **Line-of-sight pre-check** — quickly test whether two points share a
///   clear axis before running full FOV.
/// - **Laser/beam weapons** — apply damage to every cell along the line.
/// - **Debug rendering** — draw a line of marker tiles on the map.
///
/// The result always begins at @p a and ends at @p b, regardless of direction.
///
/// @code
/// IVec2 archer{3, 3};
/// IVec2 target{9, 7};
///
/// // Check for wall obstructions along the arrow's path.
/// bool clear = true;
/// for (IVec2 p : line(archer, target)) {
///     if (p == archer) continue;          // skip the shooter's own cell
///     if (map[p] == Tile::Wall) { clear = false; break; }
/// }
///
/// // Apply a lightning bolt: damage every creature on the line.
/// for (IVec2 p : line(caster_pos, target_pos)) {
///     if (auto* creature = find_creature_at(p)) {
///         creature->take_damage(rng.roll("2d6"));
///     }
/// }
/// @endcode
std::vector<IVec2> line(IVec2 a, IVec2 b);

// ---------------------------------------------------------------------------
// Distance metrics
// ---------------------------------------------------------------------------

/// @brief Chebyshev (chessboard) distance: max(|dx|, |dy|).
///
/// Chebyshev distance is the correct metric for 8-directional movement where
/// diagonal steps have the same cost as cardinal steps. A unit moving under
/// Chebyshev distance can reach all cells within radius r in exactly r steps.
///
/// @code
/// IVec2 player{5, 5};
///
/// // Draw a "diamond" area of effect — all cells within Chebyshev distance 3.
/// for (int y = 0; y < map.height(); ++y)
///     for (int x = 0; x < map.width(); ++x)
///         if (dist_chebyshev(player, {x, y}) <= 3)
///             highlight({x, y});
///
/// // Check whether a monster is adjacent (including diagonals).
/// if (dist_chebyshev(player, monster_pos) == 1) { /* adjacent */ }
/// @endcode
int32_t dist_chebyshev(IVec2 a, IVec2 b) noexcept;

/// @brief Manhattan (taxicab) distance: |dx| + |dy|.
///
/// Manhattan distance is the correct metric for 4-directional movement.
/// It counts the minimum number of cardinal steps between two cells when no
/// diagonal movement is allowed.
///
/// @code
/// IVec2 player{5, 5};
///
/// // Find the closest chest in a 4-directional dungeon.
/// IVec2 nearest_chest;
/// int best = INT_MAX;
/// for (IVec2 chest : chests) {
///     int d = dist_manhattan(player, chest);
///     if (d < best) { best = d; nearest_chest = chest; }
/// }
///
/// // Warn the player when a monster is within striking range.
/// if (dist_manhattan(player, monster_pos) <= 2) { warn("Monster nearby!"); }
/// @endcode
int32_t dist_manhattan(IVec2 a, IVec2 b) noexcept;

/// @brief Euclidean (straight-line) distance, returned as a float.
///
/// Use Euclidean distance when you need geometric accuracy — for example,
/// explosion radii, sound propagation, or light falloff. For movement cost
/// calculations prefer Chebyshev or Manhattan which avoid the square root.
///
/// @code
/// IVec2 explosion{10, 8};
/// float blast_radius = 4.5f;
///
/// // Apply damage to all cells within the blast radius.
/// for (int y = 0; y < map.height(); ++y) {
///     for (int x = 0; x < map.width(); ++x) {
///         IVec2 p{x, y};
///         float d = dist_euclidean(explosion, p);
///         if (d <= blast_radius) {
///             int dmg = static_cast<int>((1.0f - d / blast_radius) * 20.0f);
///             apply_blast_damage(p, dmg);
///         }
///     }
/// }
/// @endcode
float dist_euclidean(IVec2 a, IVec2 b) noexcept;

// ---------------------------------------------------------------------------
// Flood fill
// ---------------------------------------------------------------------------

/// @brief BFS flood fill — returns all cells reachable from @p origin.
///
/// Starting from @p origin and expanding outward via BFS, every cell for which
/// @p passable returns true is collected and returned. The origin itself is
/// always included in the result regardless of whether passable returns true
/// for it (its passability is assumed by the caller placing the origin there).
///
/// Set @p eight_way = true to allow diagonal movement during the fill.
///
/// **Typical uses:**
/// - Connectivity checks (is the dungeon fully connected?).
/// - Detecting isolated regions (sealed-off rooms, pockets of water).
/// - Autoexplore — find all reachable unexplored cells from the player's
///   current position.
/// - "Blob" selection — select all floor tiles in the same room.
///
/// @param origin     Starting cell. Returns empty if out of bounds.
/// @param grid_w     Grid width.
/// @param grid_h     Grid height.
/// @param passable   Callable(IVec2) -> bool. Must return true for a cell to
///                   be included in the fill (not applied to @p origin).
/// @param eight_way  If true, expand via 8-connectivity; default is 4.
/// @return Unordered list of all reachable cells (including origin).
///
/// @code
/// Grid<Tile> map = generate_dungeon();
///
/// // Check whether the dungeon is fully connected from the stairs.
/// IVec2 stairs = find_stairs(map);
/// auto reachable = flood_fill(stairs, map,
///     [&](IVec2 p){ return map[p] == Tile::Floor; });
///
/// int total_floor = std::count(map.begin(), map.end(), Tile::Floor);
/// if ((int)reachable.size() < total_floor) {
///     // Some floor tiles are unreachable — regenerate or add a corridor.
/// }
///
/// // Highlight a room: flood-fill from the player to find all connected floor.
/// auto room_cells = flood_fill(player_pos, map,
///     [&](IVec2 p){ return map[p] == Tile::Floor; });
/// for (IVec2 p : room_cells)
///     overlay[p] = OverlayColor::Highlight;
///
/// // Flood-fill with 8-connectivity (e.g. water spreading diagonally).
/// auto water = flood_fill(source, map,
///     [&](IVec2 p){ return map[p] != Tile::Wall; },
///     /*eight_way=*/true);
/// @endcode
template<typename Passable>
std::vector<IVec2> flood_fill(
    IVec2 origin,
    int32_t grid_w, int32_t grid_h,
    Passable&& passable,
    bool eight_way = false)
{
    if (origin.x < 0 || origin.x >= grid_w ||
        origin.y < 0 || origin.y >= grid_h)
        return {};

    // Use uint8_t instead of bool to avoid std::vector<bool> proxy issues.
    Grid<uint8_t> visited(grid_w, grid_h, 0u);
    std::vector<IVec2> result;
    std::queue<IVec2> queue;

    visited[origin] = 1u;
    queue.push(origin);
    result.push_back(origin);

    while (!queue.empty()) {
        IVec2 cur = queue.front(); queue.pop();
        auto nbs = eight_way ? neighbors8(cur, grid_w, grid_h)
                             : neighbors4(cur, grid_w, grid_h);
        for (auto nb : nbs) {
            if (!visited[nb] && passable(nb)) {
                visited[nb] = 1u;
                result.push_back(nb);
                queue.push(nb);
            }
        }
    }

    return result;
}

/// @brief Grid<T> overload — deduces width and height from the grid.
///
/// @code
/// auto region = flood_fill(origin, map,
///     [&](IVec2 p){ return map[p] == Tile::Floor; });
/// @endcode
template<typename T, typename Passable>
std::vector<IVec2> flood_fill(
    IVec2 origin,
    const Grid<T>& grid,
    Passable&& passable,
    bool eight_way = false)
{
    return flood_fill(origin, grid.width(), grid.height(),
                      std::forward<Passable>(passable), eight_way);
}

// ---------------------------------------------------------------------------
// Rectangle utilities
// ---------------------------------------------------------------------------

/// @brief Clamp @p r so that it fits entirely within [0, grid_w) × [0, grid_h).
///
/// Any portion of @p r that extends beyond the grid boundary is trimmed.
/// Returns a zero-size rect (valid() == false) if @p r lies entirely outside
/// the grid.
///
/// Use this whenever a rect comes from external data (user input, a level
/// file, a camera window) that might overlap the grid boundary.
///
/// @code
/// // Safe camera window: clip the view rect to the map before rendering.
/// IRect camera = get_camera_rect();
/// IRect safe   = rect_clamp(camera, map);
/// rect_for_each(safe, [&](IVec2 p){ render_tile(p, map[p]); });
///
/// // Place a prefab room that may hang over the map edge; trim the overflow.
/// IRect prefab_area = place_prefab(rng, map);
/// IRect actual_area = rect_clamp(prefab_area, map);
/// if (actual_area.valid()) {
///     stamp_prefab(actual_area, map);
/// }
/// @endcode
IRect rect_clamp(IRect r, int32_t grid_w, int32_t grid_h) noexcept;

/// @brief Grid<T> overload — deduces grid bounds from the grid object.
///
/// @code
/// IRect safe = rect_clamp(camera_rect, map);
/// @endcode
template<typename T>
IRect rect_clamp(IRect r, const Grid<T>& grid) noexcept {
    return rect_clamp(r, grid.width(), grid.height());
}

/// @brief Call @p fn(IVec2) for every cell inside @p r, in row-major order.
///
/// This is the standard way to iterate over a rectangular region of the grid.
/// It is equivalent to a nested x/y loop, but reads more clearly and composes
/// naturally with lambdas.
///
/// @code
/// // Fill a rectangular room with floor tiles.
/// IRect room{5, 3, 12, 8};
/// rect_for_each(room, [&](IVec2 p){ map[p] = Tile::Floor; });
///
/// // Draw a border of wall tiles around a room.
/// IRect border = room.expand(1);
/// rect_for_each(border, [&](IVec2 p){
///     if (!room.contains(p)) map[p] = Tile::Wall;
/// });
///
/// // Collect all items inside a region.
/// std::vector<Item*> items_in_area;
/// rect_for_each(selection, [&](IVec2 p){
///     if (auto* item = item_grid[p]) items_in_area.push_back(item);
/// });
///
/// // Often combined with rect_clamp to avoid out-of-bounds access.
/// rect_for_each(rect_clamp(area_of_effect, map), [&](IVec2 p){
///     apply_effect(p);
/// });
/// @endcode
template<typename Fn>
void rect_for_each(IRect r, Fn&& fn) {
    for (int32_t y = r.y; y < r.bottom(); ++y)
        for (int32_t x = r.x; x < r.right(); ++x)
            fn(IVec2{x, y});
}

} // namespace xebble
