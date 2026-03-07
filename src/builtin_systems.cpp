/// @file builtin_systems.cpp
/// @brief Built-in render system implementations.
#include <xebble/builtin_systems.hpp>
#include <xebble/components.hpp>
#include <xebble/world.hpp>
#include <xebble/renderer.hpp>
#include <xebble/spritesheet.hpp>
#include <xebble/tilemap.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace xebble {

void TileMapRenderSystem::draw(World& world, Renderer& renderer) {
    auto& cam = world.resource<Camera>();
    uint32_t vw = renderer.virtual_width();
    uint32_t vh = renderer.virtual_height();

    // Collect and sort tilemap layers by z_order
    struct TileMapEntry {
        TileMapLayer* layer;
    };
    std::vector<TileMapEntry> entries;
    world.each<TileMapLayer>([&](Entity, TileMapLayer& tl) {
        if (tl.tilemap) entries.push_back({&tl});
    });
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.layer->z_order < b.layer->z_order; });

    for (auto& entry : entries) {
        auto& tm = *entry.layer->tilemap;
        auto& sheet = tm.sheet();
        uint32_t tw = sheet.tile_width();
        uint32_t th = sheet.tile_height();

        // Calculate visible tile range
        int start_tx = static_cast<int>(cam.x) / static_cast<int>(tw);
        int start_ty = static_cast<int>(cam.y) / static_cast<int>(th);
        int end_tx = start_tx + static_cast<int>(vw / tw) + 2;
        int end_ty = start_ty + static_cast<int>(vh / th) + 2;

        start_tx = std::max(start_tx, 0);
        start_ty = std::max(start_ty, 0);
        end_tx = std::min(end_tx, static_cast<int>(tm.width()));
        end_ty = std::min(end_ty, static_cast<int>(tm.height()));

        for (uint32_t layer = 0; layer < tm.layer_count(); layer++) {
            std::vector<SpriteInstance> instances;
            for (int ty = start_ty; ty < end_ty; ty++) {
                for (int tx = start_tx; tx < end_tx; tx++) {
                    auto tile = tm.tile_at(layer, static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                    if (!tile) continue;
                    auto uv = sheet.region(*tile);
                    float screen_x = static_cast<float>(tx) * static_cast<float>(tw) - cam.x;
                    float screen_y = static_cast<float>(ty) * static_cast<float>(th) - cam.y;
                    instances.push_back({
                        .pos_x  = screen_x,
                        .pos_y  = screen_y,
                        .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                        .quad_w = static_cast<float>(tw),
                        .quad_h = static_cast<float>(th),
                        .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                        .scale    = 1.0f,
                        .rotation = 0.0f,
                        .pivot_x  = 0.0f,
                        .pivot_y  = 0.0f,
                    });
                }
            }
            if (!instances.empty()) {
                renderer.submit_instances(instances, sheet.texture(),
                                          entry.layer->z_order + static_cast<float>(layer) * 0.01f);
            }
        }
    }
}

void SpriteRenderSystem::draw(World& world, Renderer& renderer) {
    auto& cam = world.resource<Camera>();
    uint32_t vw = renderer.virtual_width();
    uint32_t vh = renderer.virtual_height();

    // Collect visible sprites
    struct SpriteEntry {
        float screen_x, screen_y;
        const SpriteSheet* sheet;
        uint32_t tile_index;
        float z_order;
        Color tint;
        float scale;
        float rotation;
        float pivot_x, pivot_y;
    };
    std::vector<SpriteEntry> entries;

    world.each<Position, Sprite>([&](Entity, Position& pos, Sprite& spr) {
        if (!spr.sheet) return;
        float sx = pos.x - cam.x;
        float sy = pos.y - cam.y;
        float tw = static_cast<float>(spr.sheet->tile_width());
        float th = static_cast<float>(spr.sheet->tile_height());
        // Conservative cull: use scaled half-diagonal as radius so rotated
        // sprites near the edge are never incorrectly discarded.
        float half_diag = 0.5f * std::sqrt(tw * tw + th * th) * std::abs(spr.scale);
        float cx = sx + tw * 0.5f;
        float cy = sy + th * 0.5f;
        float fvw = static_cast<float>(vw);
        float fvh = static_cast<float>(vh);
        if (cx + half_diag < 0 || cx - half_diag > fvw ||
            cy + half_diag < 0 || cy - half_diag > fvh)
            return;
        entries.push_back({sx, sy, spr.sheet, spr.tile_index, spr.z_order,
                           spr.tint, spr.scale, spr.rotation,
                           spr.pivot_x, spr.pivot_y});
    });

    // Sort by z_order
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.z_order < b.z_order; });

    for (auto& e : entries) {
        auto uv = e.sheet->region(e.tile_index);
        float tw = static_cast<float>(e.sheet->tile_width());
        float th = static_cast<float>(e.sheet->tile_height());
        // pos_x/pos_y is the top-left of the unscaled tile in screen space.
        // The shader interprets inPosition as the pivot point in world space,
        // so we shift by pivot * tile_size to convert from top-left to pivot coords.
        SpriteInstance inst{
            .pos_x    = e.screen_x + e.pivot_x * tw * e.scale,
            .pos_y    = e.screen_y + e.pivot_y * th * e.scale,
            .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
            .quad_w   = tw, .quad_h = th,
            .r = static_cast<float>(e.tint.r) / 255.0f,
            .g = static_cast<float>(e.tint.g) / 255.0f,
            .b = static_cast<float>(e.tint.b) / 255.0f,
            .a = static_cast<float>(e.tint.a) / 255.0f,
            .scale    = e.scale,
            .rotation = e.rotation,
            .pivot_x  = e.pivot_x,
            .pivot_y  = e.pivot_y,
        };
        renderer.submit_instances({&inst, 1}, e.sheet->texture(), e.z_order);
    }
}

} // namespace xebble
