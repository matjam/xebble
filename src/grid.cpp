/// @file grid.cpp
/// @brief Non-template grid utility function definitions.
#include <xebble/grid.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace xebble {

std::vector<IVec2> neighbors4(IVec2 pos, int32_t grid_w, int32_t grid_h) {
    std::vector<IVec2> out;
    out.reserve(4);
    constexpr IVec2 dirs[4] = {{ 0,-1},{ 1, 0},{ 0, 1},{-1, 0}};
    for (auto d : dirs) {
        IVec2 nb = pos + d;
        if (nb.x >= 0 && nb.x < grid_w && nb.y >= 0 && nb.y < grid_h)
            out.push_back(nb);
    }
    return out;
}

std::vector<IVec2> neighbors8(IVec2 pos, int32_t grid_w, int32_t grid_h) {
    std::vector<IVec2> out;
    out.reserve(8);
    for (int32_t dy = -1; dy <= 1; ++dy) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            IVec2 nb = pos + IVec2{dx, dy};
            if (nb.x >= 0 && nb.x < grid_w && nb.y >= 0 && nb.y < grid_h)
                out.push_back(nb);
        }
    }
    return out;
}

std::vector<IVec2> line(IVec2 a, IVec2 b) {
    std::vector<IVec2> out;
    int32_t dx  = std::abs(b.x - a.x);
    int32_t dy  = std::abs(b.y - a.y);
    int32_t sx  = (a.x < b.x) ? 1 : -1;
    int32_t sy  = (a.y < b.y) ? 1 : -1;
    int32_t err = dx - dy;
    IVec2   cur = a;
    while (true) {
        out.push_back(cur);
        if (cur == b) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cur.x += sx; }
        if (e2 <  dx) { err += dx; cur.y += sy; }
    }
    return out;
}

int32_t dist_chebyshev(IVec2 a, IVec2 b) noexcept {
    return std::max(std::abs(b.x - a.x), std::abs(b.y - a.y));
}

int32_t dist_manhattan(IVec2 a, IVec2 b) noexcept {
    return std::abs(b.x - a.x) + std::abs(b.y - a.y);
}

float dist_euclidean(IVec2 a, IVec2 b) noexcept {
    float dx = static_cast<float>(b.x - a.x);
    float dy = static_cast<float>(b.y - a.y);
    return std::sqrt(dx * dx + dy * dy);
}

IRect rect_clamp(IRect r, int32_t grid_w, int32_t grid_h) noexcept {
    int32_t nx = std::max(r.x, 0);
    int32_t ny = std::max(r.y, 0);
    int32_t nr = std::min(r.right(),  grid_w);
    int32_t nb = std::min(r.bottom(), grid_h);
    if (nr <= nx || nb <= ny) return {nx, ny, 0, 0};
    return {nx, ny, nr - nx, nb - ny};
}

} // namespace xebble
