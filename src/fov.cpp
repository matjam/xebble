/// @file fov.cpp
/// @brief Recursive shadowcasting FOV implementation.
#include <xebble/fov.hpp>

namespace xebble {

namespace {

void cast_light(
        int cx, int cy, int radius,
        int row, float start_slope, float end_slope,
        int xx, int xy, int yx, int yy,
        const std::function<bool(IVec2)>& blocks,
        const std::function<void(IVec2)>& mark)
{
    if (start_slope < end_slope) return;

    float next_start = start_slope;
    bool  blocked    = false;

    for (int d = row; d <= radius && !blocked; ++d) {
        int dy = -d;
        for (int dx = -d; dx <= 0; ++dx) {
            int   mx = cx + dx * xx + dy * xy;
            int   my = cy + dx * yx + dy * yy;
            IVec2 p{mx, my};

            float l_slope = (static_cast<float>(dx) - 0.5f) /
                            (static_cast<float>(dy) + 0.5f);
            float r_slope = (static_cast<float>(dx) + 0.5f) /
                            (static_cast<float>(dy) - 0.5f);

            if (start_slope < r_slope) continue;
            if (end_slope   > l_slope) break;

            if (dx * dx + dy * dy <= radius * radius)
                mark(p);

            if (blocked) {
                if (blocks(p)) {
                    next_start = r_slope;
                } else {
                    blocked     = false;
                    start_slope = next_start;
                }
            } else {
                if (blocks(p) && d < radius) {
                    blocked = true;
                    cast_light(cx, cy, radius, d + 1,
                               start_slope, l_slope,
                               xx, xy, yx, yy, blocks, mark);
                    next_start = r_slope;
                }
            }
        }
    }
}

} // anonymous namespace

void compute_fov(
        IVec2 origin,
        int   radius,
        const std::function<bool(IVec2)>& blocks,
        const std::function<void(IVec2)>& mark)
{
    mark(origin);

    static constexpr int transforms[8][4] = {
        { 1,  0,  0,  1},
        { 0,  1,  1,  0},
        { 0, -1,  1,  0},
        {-1,  0,  0,  1},
        {-1,  0,  0, -1},
        { 0, -1, -1,  0},
        { 0,  1, -1,  0},
        { 1,  0,  0, -1},
    };
    for (const auto& t : transforms) {
        cast_light(origin.x, origin.y, radius,
                   1, 1.0f, 0.0f,
                   t[0], t[1], t[2], t[3],
                   blocks, mark);
    }
}

void compute_fov(
        IVec2 origin,
        int   radius,
        const std::function<bool(IVec2)>& blocks,
        Grid<VisState>& vis)
{
    compute_fov(origin, radius, blocks, [&](IVec2 p) {
        if (vis.in_bounds(p)) vis[p] = VisState::Visible;
    });
}

} // namespace xebble
