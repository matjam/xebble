/// @file test_fov.cpp
/// @brief Unit tests for xebble::compute_fov (recursive shadowcasting).

#include <xebble/fov.hpp>

#include <gtest/gtest.h>

#include <set>
#include <vector>

using namespace xebble;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Collect all visible cells from compute_fov into a set.
static std::set<std::pair<int, int>> visible_set(IVec2 origin, int radius,
                                                 const std::function<bool(IVec2)>& blocks,
                                                 int grid_w = 40, int grid_h = 25) {
    std::set<std::pair<int, int>> out;
    compute_fov(origin, radius, blocks, [&](IVec2 p) {
        if (p.x >= 0 && p.x < grid_w && p.y >= 0 && p.y < grid_h)
            out.insert({p.x, p.y});
    });
    return out;
}

/// All-open map (no walls).
static auto no_walls() {
    return [](IVec2) {
        return false;
    };
}

// ---------------------------------------------------------------------------
// VisState
// ---------------------------------------------------------------------------

TEST(VisState, DefaultValues) {
    EXPECT_EQ(static_cast<int>(VisState::Unseen), 0);
    EXPECT_EQ(static_cast<int>(VisState::Revealed), 1);
    EXPECT_EQ(static_cast<int>(VisState::Visible), 2);
}

// ---------------------------------------------------------------------------
// Origin always visible
// ---------------------------------------------------------------------------

TEST(ComputeFov, OriginAlwaysVisible) {
    IVec2 origin{5, 5};
    auto vis = visible_set(origin, 4, no_walls());
    EXPECT_TRUE(vis.count({5, 5}));
}

TEST(ComputeFov, OriginVisibleEvenRadiusZero) {
    IVec2 origin{3, 3};
    std::set<std::pair<int, int>> out;
    compute_fov(origin, 0, no_walls(), [&](IVec2 p) { out.insert({p.x, p.y}); });
    EXPECT_EQ(out.size(), 1u);
    EXPECT_TRUE(out.count({3, 3}));
}

// ---------------------------------------------------------------------------
// Open field — all cells within radius visible
// ---------------------------------------------------------------------------

TEST(ComputeFov, OpenFieldRadius1) {
    IVec2 origin{5, 5};
    auto vis = visible_set(origin, 1, no_walls());

    // Should see all 9 cells in a 3×3 square (and origin).
    EXPECT_TRUE(vis.count({5, 5}));
    EXPECT_TRUE(vis.count({4, 5}));
    EXPECT_TRUE(vis.count({6, 5}));
    EXPECT_TRUE(vis.count({5, 4}));
    EXPECT_TRUE(vis.count({5, 6}));
}

TEST(ComputeFov, OpenFieldCountGrowsWithRadius) {
    IVec2 origin{15, 12};
    auto r3 = visible_set(origin, 3, no_walls());
    auto r6 = visible_set(origin, 6, no_walls());
    // Larger radius → more visible cells.
    EXPECT_GT(r6.size(), r3.size());
}

TEST(ComputeFov, OpenFieldSymmetric) {
    // With no walls a cell at (cx+dx, cy+dy) should be visible iff
    // (cx-dx, cy+dy) is, and iff (cx+dx, cy-dy) is, etc.
    IVec2 origin{15, 12};
    int radius = 5;
    auto vis = visible_set(origin, radius, no_walls(), 80, 25);

    for (auto& [x, y] : vis) {
        int dx = x - origin.x;
        int dy = y - origin.y;
        // Mirror across both axes — all four quadrant reflections should exist.
        EXPECT_TRUE(vis.count({origin.x - dx, origin.y + dy}))
            << "Missing horizontal mirror of (" << x << "," << y << ")";
        EXPECT_TRUE(vis.count({origin.x + dx, origin.y - dy}))
            << "Missing vertical mirror of (" << x << "," << y << ")";
    }
}

// ---------------------------------------------------------------------------
// Walls block visibility
// ---------------------------------------------------------------------------

TEST(ComputeFov, WallBlocksCell) {
    // Wall at (6,5) — the cell directly to the right of origin.
    IVec2 origin{5, 5};
    auto blocks = [](IVec2 p) {
        return p.x == 6 && p.y == 5;
    };

    auto vis = visible_set(origin, 4, blocks);
    // The wall cell itself is visible (you can see it, just not through it).
    EXPECT_TRUE(vis.count({6, 5}));
    // But cells behind it in the same direction should not be.
    EXPECT_FALSE(vis.count({7, 5}));
    EXPECT_FALSE(vis.count({8, 5}));
}

TEST(ComputeFov, WallDoesNotBlockPerpendicularDirection) {
    // Wall at (6,5) should not block cells above/below origin.
    IVec2 origin{5, 5};
    auto blocks = [](IVec2 p) {
        return p.x == 6 && p.y == 5;
    };

    auto vis = visible_set(origin, 4, blocks);
    EXPECT_TRUE(vis.count({5, 4}));
    EXPECT_TRUE(vis.count({5, 6}));
    EXPECT_TRUE(vis.count({4, 5}));
}

TEST(ComputeFov, ColumnWallCastsShadow) {
    // Vertical wall at x=6 from y=3..7 — should shadow columns behind it.
    IVec2 origin{5, 5};
    auto blocks = [](IVec2 p) {
        return p.x == 6 && p.y >= 3 && p.y <= 7;
    };

    auto vis = visible_set(origin, 8, blocks);
    // Cells well behind the wall at the same y should be hidden.
    EXPECT_FALSE(vis.count({9, 5}));
}

TEST(ComputeFov, AdjacentWallsCreateCorridor) {
    // Horizontal corridor: walls at y=4 and y=6, open at y=5.
    // Looking right from (0,5) we should see down the corridor.
    IVec2 origin{0, 5};
    auto blocks = [](IVec2 p) {
        return p.y == 4 || p.y == 6;
    };

    auto vis = visible_set(origin, 10, blocks, 20, 10);
    // Cells along the corridor floor at y=5 should be visible.
    EXPECT_TRUE(vis.count({5, 5}));
    EXPECT_TRUE(vis.count({8, 5}));
}

// ---------------------------------------------------------------------------
// Radius limits
// ---------------------------------------------------------------------------

TEST(ComputeFov, CellsBeyondRadiusNotVisible) {
    IVec2 origin{10, 10};
    int radius = 4;
    auto vis = visible_set(origin, radius, no_walls(), 40, 25);

    // (10+5, 10) is dx=5 > radius=4, Euclidean distance=5 > 4.
    EXPECT_FALSE(vis.count({15, 10}));
}

TEST(ComputeFov, CellsAtRadiusEdgeVisible) {
    IVec2 origin{10, 10};
    int radius = 4;
    auto vis = visible_set(origin, radius, no_walls(), 40, 25);

    // (14, 10) is exactly radius away horizontally.
    EXPECT_TRUE(vis.count({14, 10}));
    EXPECT_TRUE(vis.count({10, 14}));
}

// ---------------------------------------------------------------------------
// Grid<VisState> convenience overload
// ---------------------------------------------------------------------------

TEST(ComputeFov, GridOverloadWritesVisible) {
    Grid<VisState> vis(20, 20, VisState::Unseen);
    IVec2 origin{10, 10};
    compute_fov(origin, 5, no_walls(), vis);

    EXPECT_EQ(vis[origin], VisState::Visible);
    // Some nearby cell.
    IVec2 nearby{10, 11};
    EXPECT_EQ(vis[nearby], VisState::Visible);
}

TEST(ComputeFov, GridOverloadIgnoresOutOfBounds) {
    // Origin near the edge — out-of-bounds cells must not crash.
    Grid<VisState> vis(10, 10, VisState::Unseen);
    IVec2 origin{1, 1};
    // Should not throw or crash even though the algorithm generates
    // cells with negative coordinates.
    EXPECT_NO_THROW(compute_fov(origin, 5, [](IVec2) { return false; }, vis));
}

TEST(ComputeFov, GridOverloadDoesNotClearExistingRevealed) {
    // Pre-mark a distant cell as Revealed; it should stay Revealed
    // after compute_fov (the overload only sets Visible, never clears).
    Grid<VisState> vis(20, 20, VisState::Unseen);
    IVec2 distant{18, 18};
    vis[distant] = VisState::Revealed;

    IVec2 origin{5, 5};
    compute_fov(origin, 3, no_walls(), vis);

    EXPECT_EQ(vis[distant], VisState::Revealed);
}
