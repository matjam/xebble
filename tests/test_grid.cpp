#include <gtest/gtest.h>
#include <xebble/grid.hpp>
#include <algorithm>
#include <cmath>

using namespace xebble;

// ---------------------------------------------------------------------------
// IVec2
// ---------------------------------------------------------------------------

TEST(IVec2, DefaultConstruction) {
    IVec2 v;
    EXPECT_EQ(v.x, 0);
    EXPECT_EQ(v.y, 0);
}

TEST(IVec2, AggregateInit) {
    IVec2 v{3, -5};
    EXPECT_EQ(v.x,  3);
    EXPECT_EQ(v.y, -5);
}

TEST(IVec2, Equality) {
    EXPECT_EQ((IVec2{1, 2}), (IVec2{1, 2}));
    EXPECT_NE((IVec2{1, 2}), (IVec2{1, 3}));
}

TEST(IVec2, Arithmetic) {
    IVec2 a{1, 2};
    IVec2 b{3, 4};
    EXPECT_EQ(a + b, (IVec2{4, 6}));
    EXPECT_EQ(b - a, (IVec2{2, 2}));
    EXPECT_EQ(a * 3, (IVec2{3, 6}));
}

TEST(IVec2, CompoundAssignment) {
    IVec2 v{1, 2};
    v += IVec2{2, 3};
    EXPECT_EQ(v, (IVec2{3, 5}));
    v -= IVec2{1, 1};
    EXPECT_EQ(v, (IVec2{2, 4}));
}

// ---------------------------------------------------------------------------
// IRect
// ---------------------------------------------------------------------------

TEST(IRect, DefaultConstruction) {
    IRect r;
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
    EXPECT_EQ(r.w, 0);
    EXPECT_EQ(r.h, 0);
}

TEST(IRect, EdgeAccessors) {
    IRect r{2, 3, 4, 5};
    EXPECT_EQ(r.right(),  6);
    EXPECT_EQ(r.bottom(), 8);
}

TEST(IRect, ValidAndContains) {
    IRect r{1, 1, 3, 3};
    EXPECT_TRUE(r.valid());
    EXPECT_TRUE(r.contains({1, 1}));  // top-left included
    EXPECT_TRUE(r.contains({3, 3}));  // bottom-right included (3 < 1+3=4)
    EXPECT_FALSE(r.contains({4, 1})); // right edge excluded
    EXPECT_FALSE(r.contains({1, 4})); // bottom edge excluded
    EXPECT_FALSE(r.contains({0, 0}));

    IRect zero{0, 0, 0, 0};
    EXPECT_FALSE(zero.valid());
}

TEST(IRect, Intersect) {
    IRect a{0, 0, 4, 4};
    IRect b{2, 2, 4, 4};
    IRect c = a.intersect(b);
    EXPECT_EQ(c.x, 2);
    EXPECT_EQ(c.y, 2);
    EXPECT_EQ(c.w, 2);
    EXPECT_EQ(c.h, 2);
}

TEST(IRect, IntersectNoOverlap) {
    IRect a{0, 0, 2, 2};
    IRect b{5, 5, 2, 2};
    IRect c = a.intersect(b);
    EXPECT_FALSE(c.valid());
}

TEST(IRect, Expand) {
    IRect r{2, 2, 4, 4};
    IRect e = r.expand(1);
    EXPECT_EQ(e.x, 1);
    EXPECT_EQ(e.y, 1);
    EXPECT_EQ(e.w, 6);
    EXPECT_EQ(e.h, 6);
}

TEST(IRect, Equality) {
    EXPECT_EQ((IRect{1,2,3,4}), (IRect{1,2,3,4}));
    EXPECT_NE((IRect{1,2,3,4}), (IRect{1,2,3,5}));
}

// ---------------------------------------------------------------------------
// Grid<T>
// ---------------------------------------------------------------------------

TEST(Grid, Construction) {
    Grid<int> g(5, 3);
    EXPECT_EQ(g.width(),  5);
    EXPECT_EQ(g.height(), 3);
    EXPECT_EQ(g.size(),  15u);
}

TEST(Grid, FillConstruction) {
    Grid<int> g(4, 4, 7);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            IVec2 p{x, y};
            EXPECT_EQ(g[p], 7);
        }
}

TEST(Grid, InBounds) {
    Grid<int> g(5, 5);
    EXPECT_TRUE(g.in_bounds({0, 0}));
    EXPECT_TRUE(g.in_bounds({4, 4}));
    EXPECT_FALSE(g.in_bounds({5, 0}));
    EXPECT_FALSE(g.in_bounds({0, 5}));
    EXPECT_FALSE(g.in_bounds({-1, 0}));
}

TEST(Grid, ReadWrite) {
    Grid<int> g(3, 3, 0);
    IVec2 p12{1, 2};
    IVec2 p00{0, 0};
    g[p12] = 42;
    EXPECT_EQ(g[p12], 42);
    EXPECT_EQ(g[p00], 0);
}

TEST(Grid, AtBoundsChecked) {
    Grid<int> g(3, 3, 0);
    g[{1, 1}] = 99;

    auto opt = g.at({1, 1});
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->get(), 99);

    auto out = g.at({10, 10});
    EXPECT_FALSE(out.has_value());
}

TEST(Grid, Fill) {
    Grid<int> g(3, 3, 0);
    g.fill(5);
    for (auto v : g) EXPECT_EQ(v, 5);
}

TEST(Grid, ConstAccess) {
    Grid<int> g(2, 2, 3);
    const Grid<int>& cg = g;
    IVec2 p00{0, 0};
    EXPECT_EQ(cg[p00], 3);
    EXPECT_TRUE(cg.at({0, 0}).has_value());
    EXPECT_FALSE(cg.at({5, 5}).has_value());
}

// ---------------------------------------------------------------------------
// neighbors4
// ---------------------------------------------------------------------------

TEST(Neighbors4, Centre) {
    auto nbs = neighbors4({2, 2}, 5, 5);
    EXPECT_EQ(nbs.size(), 4u);
    EXPECT_TRUE(std::find(nbs.begin(), nbs.end(), IVec2{2,1}) != nbs.end());
    EXPECT_TRUE(std::find(nbs.begin(), nbs.end(), IVec2{3,2}) != nbs.end());
    EXPECT_TRUE(std::find(nbs.begin(), nbs.end(), IVec2{2,3}) != nbs.end());
    EXPECT_TRUE(std::find(nbs.begin(), nbs.end(), IVec2{1,2}) != nbs.end());
}

TEST(Neighbors4, Corner) {
    auto nbs = neighbors4({0, 0}, 5, 5);
    EXPECT_EQ(nbs.size(), 2u);
}

TEST(Neighbors4, Edge) {
    auto nbs = neighbors4({0, 2}, 5, 5);
    EXPECT_EQ(nbs.size(), 3u);
}

TEST(Neighbors4, GridOverload) {
    Grid<int> g(5, 5);
    EXPECT_EQ(neighbors4({2,2}, g).size(), 4u);
}

// ---------------------------------------------------------------------------
// neighbors8
// ---------------------------------------------------------------------------

TEST(Neighbors8, Centre) {
    auto nbs = neighbors8({2, 2}, 5, 5);
    EXPECT_EQ(nbs.size(), 8u);
}

TEST(Neighbors8, Corner) {
    auto nbs = neighbors8({0, 0}, 5, 5);
    EXPECT_EQ(nbs.size(), 3u);
}

TEST(Neighbors8, Edge) {
    auto nbs = neighbors8({0, 2}, 5, 5);
    EXPECT_EQ(nbs.size(), 5u);
}

TEST(Neighbors8, GridOverload) {
    Grid<int> g(5, 5);
    EXPECT_EQ(neighbors8({0,0}, g).size(), 3u);
}

// ---------------------------------------------------------------------------
// Bresenham line
// ---------------------------------------------------------------------------

TEST(Line, SamePoint) {
    auto pts = line({2, 2}, {2, 2});
    ASSERT_EQ(pts.size(), 1u);
    EXPECT_EQ(pts[0], (IVec2{2, 2}));
}

TEST(Line, Horizontal) {
    auto pts = line({0, 0}, {4, 0});
    ASSERT_EQ(pts.size(), 5u);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(pts[static_cast<size_t>(i)], (IVec2{i, 0}));
}

TEST(Line, Vertical) {
    auto pts = line({0, 0}, {0, 3});
    ASSERT_EQ(pts.size(), 4u);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(pts[static_cast<size_t>(i)], (IVec2{0, i}));
}

TEST(Line, Diagonal45) {
    auto pts = line({0, 0}, {3, 3});
    ASSERT_EQ(pts.size(), 4u);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(pts[static_cast<size_t>(i)], (IVec2{i, i}));
}

TEST(Line, StartsAtA) {
    auto pts = line({1, 2}, {5, 8});
    EXPECT_EQ(pts.front(), (IVec2{1, 2}));
    EXPECT_EQ(pts.back(),  (IVec2{5, 8}));
}

TEST(Line, ReverseDirection) {
    auto fwd = line({0, 0}, {3, 0});
    auto rev = line({3, 0}, {0, 0});
    ASSERT_EQ(fwd.size(), rev.size());
    // Reversed line should be the forward line in reverse order
    for (size_t i = 0; i < fwd.size(); ++i)
        EXPECT_EQ(fwd[i], rev[fwd.size() - 1 - i]);
}

// ---------------------------------------------------------------------------
// Distance metrics
// ---------------------------------------------------------------------------

TEST(Distance, Chebyshev) {
    EXPECT_EQ(dist_chebyshev({0,0}, {3,1}), 3);
    EXPECT_EQ(dist_chebyshev({0,0}, {2,2}), 2);
    EXPECT_EQ(dist_chebyshev({0,0}, {0,0}), 0);
    EXPECT_EQ(dist_chebyshev({1,2}, {4,3}), 3);
}

TEST(Distance, Manhattan) {
    EXPECT_EQ(dist_manhattan({0,0}, {3,1}), 4);
    EXPECT_EQ(dist_manhattan({0,0}, {2,2}), 4);
    EXPECT_EQ(dist_manhattan({0,0}, {0,0}), 0);
    EXPECT_EQ(dist_manhattan({1,2}, {4,3}), 4);
}

TEST(Distance, Euclidean) {
    EXPECT_FLOAT_EQ(dist_euclidean({0,0}, {0,0}), 0.0f);
    EXPECT_FLOAT_EQ(dist_euclidean({0,0}, {3,4}), 5.0f);
    EXPECT_NEAR(dist_euclidean({0,0}, {1,1}), std::sqrt(2.0f), 1e-5f);
}

// ---------------------------------------------------------------------------
// Flood fill
// ---------------------------------------------------------------------------

// Open 5x5 grid — all cells reachable from centre.
TEST(FloodFill, OpenGrid4) {
    // Everything passable
    auto result = flood_fill({2,2}, 5, 5, [](IVec2){ return true; });
    EXPECT_EQ(result.size(), 25u);
}

TEST(FloodFill, OpenGrid8) {
    auto result = flood_fill({2,2}, 5, 5, [](IVec2){ return true; }, true);
    EXPECT_EQ(result.size(), 25u);
}

// Wall splits the grid in half vertically: column x=2 is blocked.
TEST(FloodFill, WallBlocks4) {
    // 5x5, column 2 is a wall. Start on left side (x=0).
    auto result = flood_fill({0,0}, 5, 5, [](IVec2 p){ return p.x != 2; });
    // Only the left two columns (x=0,1) should be reachable
    EXPECT_EQ(result.size(), 10u);
    for (auto p : result) EXPECT_LT(p.x, 2);
}

// 8-way fill can go around a diagonal gap that 4-way can't.
TEST(FloodFill, DiagonalGap) {
    // Block cells (1,0) and (0,1) — creates a diagonal gap from (0,0).
    // 4-way: (0,0) is isolated
    // 8-way: (0,0) can reach (1,1) diagonally
    auto pass = [](IVec2 p) {
        return !(p.x == 1 && p.y == 0) && !(p.x == 0 && p.y == 1);
    };
    auto r4 = flood_fill({0,0}, 5, 5, pass, false);
    auto r8 = flood_fill({0,0}, 5, 5, pass, true);
    // 4-way: only origin (passable check not applied to origin itself)
    EXPECT_EQ(r4.size(), 1u);
    // 8-way: can reach more cells via (1,1)
    EXPECT_GT(r8.size(), 1u);
}

TEST(FloodFill, OutOfBoundsOrigin) {
    auto result = flood_fill({10,10}, 5, 5, [](IVec2){ return true; });
    EXPECT_TRUE(result.empty());
}

TEST(FloodFill, GridOverload) {
    Grid<bool> g(5, 5, true);
    auto result = flood_fill({0,0}, g, [](IVec2){ return true; });
    EXPECT_EQ(result.size(), 25u);
}

// ---------------------------------------------------------------------------
// rect_clamp
// ---------------------------------------------------------------------------

TEST(RectClamp, InsideBounds) {
    IRect r = rect_clamp({1,1,3,3}, 10, 10);
    EXPECT_EQ(r, (IRect{1,1,3,3}));
}

TEST(RectClamp, ClipsRight) {
    IRect r = rect_clamp({8,0,5,3}, 10, 10);
    EXPECT_EQ(r.x, 8);
    EXPECT_EQ(r.w, 2); // clipped to grid width
}

TEST(RectClamp, ClipsBottom) {
    IRect r = rect_clamp({0,8,3,5}, 10, 10);
    EXPECT_EQ(r.y, 8);
    EXPECT_EQ(r.h, 2);
}

TEST(RectClamp, NegativeOrigin) {
    IRect r = rect_clamp({-2,-2,6,6}, 10, 10);
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
    EXPECT_EQ(r.w, 4);
    EXPECT_EQ(r.h, 4);
}

TEST(RectClamp, EntirelyOutside) {
    IRect r = rect_clamp({20,20,5,5}, 10, 10);
    EXPECT_FALSE(r.valid());
}

TEST(RectClamp, GridOverload) {
    Grid<int> g(10, 10);
    IRect r = rect_clamp({0,0,5,5}, g);
    EXPECT_EQ(r, (IRect{0,0,5,5}));
}

// ---------------------------------------------------------------------------
// rect_for_each
// ---------------------------------------------------------------------------

TEST(RectForEach, CountCells) {
    int count = 0;
    rect_for_each({1,1,3,2}, [&](IVec2){ ++count; });
    EXPECT_EQ(count, 6); // 3 wide * 2 tall
}

TEST(RectForEach, AllCellsVisited) {
    Grid<int> g(5, 5, 0);
    rect_for_each({1,1,3,3}, [&](IVec2 p){ g[p] = 1; });
    // Cells inside rect should be 1
    for (int y = 1; y < 4; ++y)
        for (int x = 1; x < 4; ++x) {
            IVec2 p{x, y};
            EXPECT_EQ(g[p], 1);
        }
    // Cells outside should remain 0
    IVec2 corner00{0,0}, corner44{4,4};
    EXPECT_EQ(g[corner00], 0);
    EXPECT_EQ(g[corner44], 0);
}
