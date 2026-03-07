/// @file test_pathfinding.cpp
/// @brief Unit tests for xebble find_path, dijkstra_map, and dijkstra_step.

#include <xebble/pathfinding.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace xebble;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Open-map cost function (all cells walkable, uniform cost 1).
static float open_cost(IVec2 /*from*/, IVec2 /*to*/) { return 1.0f; }

/// Wall grid: walls[y*w+x] == true means blocked.
static auto wall_cost(const Grid<bool>& walls) {
    return [&](IVec2 /*from*/, IVec2 to) -> float {
        if (!walls.in_bounds(to)) return -1.0f;
        return walls[to] ? -1.0f : 1.0f;
    };
}

// ---------------------------------------------------------------------------
// find_path — basic
// ---------------------------------------------------------------------------

TEST(FindPath, SameStartAndGoal) {
    auto path = find_path({5,5}, {5,5}, 20, 20, open_cost);
    ASSERT_EQ(path.size(), 1u);
    EXPECT_EQ(path[0], (IVec2{5,5}));
}

TEST(FindPath, AdjacentCells) {
    auto path = find_path({0,0}, {1,0}, 20, 20, open_cost);
    ASSERT_EQ(path.size(), 2u);
    EXPECT_EQ(path[0], (IVec2{0,0}));
    EXPECT_EQ(path[1], (IVec2{1,0}));
}

TEST(FindPath, StartAndGoalInPath) {
    auto path = find_path({2,3}, {8,7}, 20, 20, open_cost);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.front(), (IVec2{2,3}));
    EXPECT_EQ(path.back(),  (IVec2{8,7}));
}

TEST(FindPath, PathIsContiguous) {
    auto path = find_path({0,0}, {10,10}, 20, 20, open_cost);
    ASSERT_FALSE(path.empty());
    for (size_t i = 1; i < path.size(); ++i) {
        int dx = std::abs(path[i].x - path[i-1].x);
        int dy = std::abs(path[i].y - path[i-1].y);
        EXPECT_LE(dx, 1) << "Step " << i << " x jump too large";
        EXPECT_LE(dy, 1) << "Step " << i << " y jump too large";
    }
}

TEST(FindPath, OpenGridShortestLength) {
    // On an open grid from (0,0) to (5,0) the path length should be 6 cells.
    auto path = find_path({0,0}, {5,0}, 20, 20, open_cost);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.size(), 6u);
}

TEST(FindPath, WallBlocking) {
    // Vertical wall at x=5 from y=0..9 — must go around.
    Grid<bool> walls(20, 20, false);
    for (int y = 0; y < 10; ++y) walls[IVec2{5,y}] = true;

    auto path = find_path({0,5}, {10,5}, 20, 20, wall_cost(walls));
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.front(), (IVec2{0,5}));
    EXPECT_EQ(path.back(),  (IVec2{10,5}));
    // No cell in the path should be a wall.
    for (auto& p : path)
        EXPECT_FALSE(walls.in_bounds(p) && walls[p]) << "Path passes through wall at " << p.x << "," << p.y;
}

TEST(FindPath, CompletelyBlocked) {
    // Surround goal with impassable walls.
    Grid<bool> walls(10, 10, false);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (dx != 0 || dy != 0)
                walls[IVec2{5+dx, 5+dy}] = true;

    auto path = find_path({0,0}, {5,5}, 10, 10, wall_cost(walls));
    EXPECT_TRUE(path.empty());
}

TEST(FindPath, GoalOutOfBoundsUnreachable) {
    auto path = find_path({0,0}, {99,99}, 10, 10, open_cost);
    EXPECT_TRUE(path.empty());
}

// ---------------------------------------------------------------------------
// dijkstra_map
// ---------------------------------------------------------------------------

TEST(DijkstraMap, SingleGoalOriginCostZero) {
    IVec2 goal{5,5};
    auto dmap = dijkstra_map(20, 20, {goal}, open_cost);
    EXPECT_FLOAT_EQ(dmap[goal], 0.0f);
}

TEST(DijkstraMap, AdjacentCellCostOne) {
    IVec2 goal{5,5};
    auto dmap = dijkstra_map(20, 20, {goal}, open_cost);
    IVec2 right{6,5}, below{5,6};
    EXPECT_FLOAT_EQ(dmap[right], 1.0f);
    EXPECT_FLOAT_EQ(dmap[below], 1.0f);
}

TEST(DijkstraMap, DiagonalCostOne) {
    // open_cost returns 1.0 for all moves including diagonals.
    IVec2 goal{5,5};
    auto dmap = dijkstra_map(20, 20, {goal}, open_cost);
    IVec2 diag{6,6};
    EXPECT_FLOAT_EQ(dmap[diag], 1.0f);
}

TEST(DijkstraMap, DistancesIncreaseWithDistance) {
    IVec2 goal{10,10};
    auto dmap = dijkstra_map(30, 30, {goal}, open_cost);
    IVec2 n1{11,10}, n2{12,10}, n3{13,10};
    EXPECT_LT(dmap[n1], dmap[n2]);
    EXPECT_LT(dmap[n2], dmap[n3]);
}

TEST(DijkstraMap, MultipleGoals) {
    // Two goals: (0,0) and (19,19). Both goal cells should be cost 0.
    auto dmap = dijkstra_map(20, 20, {{0,0},{19,19}}, open_cost);
    IVec2 goal_a{0,0}, goal_b{19,19};
    IVec2 mid{10,10};
    EXPECT_FLOAT_EQ(dmap[goal_a], 0.0f);
    EXPECT_FLOAT_EQ(dmap[goal_b], 0.0f);
    // Centre of the grid is reachable (cost < infinity).
    EXPECT_LT(dmap[mid], PathCostInfinity);
}

TEST(DijkstraMap, NoGoalsAllInfinity) {
    auto dmap = dijkstra_map(10, 10, {}, open_cost);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x) {
            IVec2 p{x,y};
            EXPECT_EQ(dmap[p], PathCostInfinity);
        }
}

TEST(DijkstraMap, WallsBlockPropagation) {
    Grid<bool> walls(10, 10, false);
    // Vertical wall at x=5 fully blocking the map.
    for (int y = 0; y < 10; ++y) walls[IVec2{5,y}] = true;

    auto dmap = dijkstra_map(10, 10, {{0,5}}, wall_cost(walls));
    // Left side reachable.
    IVec2 left{1,5}, right{9,5};
    EXPECT_LT(dmap[left], PathCostInfinity);
    // Right side unreachable.
    EXPECT_EQ(dmap[right], PathCostInfinity);
}

// ---------------------------------------------------------------------------
// dijkstra_step
// ---------------------------------------------------------------------------

TEST(DijkstraStep, StepsTowardGoal) {
    IVec2 goal{10,5};
    auto dmap = dijkstra_map(20, 20, {goal}, open_cost);
    IVec2 pos{5,5};
    IVec2 next = dijkstra_step(pos, dmap, 20, 20, open_cost);
    // Should move one step closer to goal (x increases).
    EXPECT_GT(next.x, pos.x);
}

TEST(DijkstraStep, AlreadyAtGoalStaysStill) {
    IVec2 goal{5,5};
    auto dmap = dijkstra_map(20, 20, {goal}, open_cost);
    IVec2 next = dijkstra_step(goal, dmap, 20, 20, open_cost);
    EXPECT_EQ(next, goal);
}

TEST(DijkstraStep, SurroundedByWallsStaysStill) {
    Grid<bool> walls(10, 10, false);
    // Box the agent in with walls.
    IVec2 pos{5,5};
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (dx != 0 || dy != 0)
                walls[IVec2{5+dx,5+dy}] = true;

    auto dmap = dijkstra_map(10, 10, {{0,0}}, wall_cost(walls));
    IVec2 next = dijkstra_step(pos, dmap, 10, 10, wall_cost(walls));
    EXPECT_EQ(next, pos);
}
