/// @file pathfinding.cpp
/// @brief A* and Dijkstra pathfinding implementations.
#include <xebble/pathfinding.hpp>

#include <algorithm>
#include <cmath>
#include <queue>

namespace xebble {

std::vector<IVec2> find_path(IVec2 start, IVec2 goal, int width, int height,
                             const CostFn& cost_fn) {
    if (start == goal)
        return {start};

    auto idx = [width](IVec2 p) {
        return p.y * width + p.x;
    };
    auto in_bounds = [width, height](IVec2 p) {
        return p.x >= 0 && p.x < width && p.y >= 0 && p.y < height;
    };

    int total = width * height;
    std::vector<float> g(total, PathCostInfinity);
    std::vector<int> parent(total, -1);

    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    g[idx(start)] = 0.0f;
    auto heuristic = [&](IVec2 p) -> float {
        return static_cast<float>(std::max(std::abs(p.x - goal.x), std::abs(p.y - goal.y)));
    };
    open.push({heuristic(start), idx(start)});

    static constexpr int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    while (!open.empty()) {
        auto [f, ci] = open.top();
        open.pop();
        IVec2 cur{ci % width, ci / width};

        if (cur == goal) {
            std::vector<IVec2> path;
            int at = ci;
            while (at != -1) {
                path.push_back({at % width, at / width});
                at = parent[at];
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        float cur_g = g[ci];
        if (f > cur_g + heuristic(cur) + 1e-6f)
            continue;

        for (int d = 0; d < 8; ++d) {
            IVec2 nb{cur.x + dx[d], cur.y + dy[d]};
            if (!in_bounds(nb))
                continue;
            float c = cost_fn(cur, nb);
            if (c < 0.0f)
                continue;
            float ng = cur_g + c;
            int ni = idx(nb);
            if (ng < g[ni]) {
                g[ni] = ng;
                parent[ni] = ci;
                open.push({ng + heuristic(nb), ni});
            }
        }
    }
    return {};
}

Grid<float> dijkstra_map(int width, int height, const std::vector<IVec2>& goals,
                         const CostFn& cost_fn) {
    Grid<float> dist(width, height, PathCostInfinity);

    auto in_bounds = [width, height](IVec2 p) {
        return p.x >= 0 && p.x < width && p.y >= 0 && p.y < height;
    };

    using Node = std::pair<float, IVec2>;
    auto cmp = [](const Node& a, const Node& b) {
        return a.first > b.first;
    };
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> pq(cmp);

    for (const auto& g : goals) {
        if (in_bounds(g)) {
            dist[g] = 0.0f;
            pq.push({0.0f, g});
        }
    }

    static constexpr int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    while (!pq.empty()) {
        auto [d, cur] = pq.top();
        pq.pop();
        if (d > dist[cur] + 1e-6f)
            continue;

        for (int i = 0; i < 8; ++i) {
            IVec2 nb{cur.x + dx[i], cur.y + dy[i]};
            if (!in_bounds(nb))
                continue;
            float c = cost_fn(cur, nb);
            if (c < 0.0f)
                continue;
            float nd = d + c;
            if (nd < dist[nb]) {
                dist[nb] = nd;
                pq.push({nd, nb});
            }
        }
    }
    return dist;
}

IVec2 dijkstra_step(IVec2 pos, const Grid<float>& dmap, int width, int height,
                    const CostFn& cost_fn) {
    auto in_bounds = [width, height](IVec2 p) {
        return p.x >= 0 && p.x < width && p.y >= 0 && p.y < height;
    };

    static constexpr int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int dy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    IVec2 best = pos;
    float best_v = dmap.in_bounds(pos) ? dmap[pos] : PathCostInfinity;

    for (int i = 0; i < 8; ++i) {
        IVec2 nb{pos.x + dx[i], pos.y + dy[i]};
        if (!in_bounds(nb))
            continue;
        if (cost_fn(pos, nb) < 0.0f)
            continue;
        float v = dmap[nb];
        if (v < best_v) {
            best_v = v;
            best = nb;
        }
    }
    return best;
}

} // namespace xebble
