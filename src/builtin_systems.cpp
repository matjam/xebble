/// @file builtin_systems.cpp
/// @brief Built-in render system implementations.
#include <xebble/builtin_systems.hpp>
#include <xebble/components.hpp>
#include <xebble/renderer.hpp>
#include <xebble/spritesheet.hpp>
#include <xebble/tilemap.hpp>
#include <xebble/world.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
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
        if (tl.tilemap)
            entries.push_back({&tl});
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
                    auto tile =
                        tm.tile_at(layer, static_cast<uint32_t>(tx), static_cast<uint32_t>(ty));
                    if (!tile)
                        continue;
                    auto uv = sheet.region(*tile);
                    float screen_x = static_cast<float>(tx) * static_cast<float>(tw) - cam.x;
                    float screen_y = static_cast<float>(ty) * static_cast<float>(th) - cam.y;
                    instances.push_back({
                        .pos_x = screen_x,
                        .pos_y = screen_y,
                        .uv_x = uv.x,
                        .uv_y = uv.y,
                        .uv_w = uv.w,
                        .uv_h = uv.h,
                        .quad_w = static_cast<float>(tw),
                        .quad_h = static_cast<float>(th),
                        .r = 1.0f,
                        .g = 1.0f,
                        .b = 1.0f,
                        .a = 1.0f,
                        .scale = 1.0f,
                        .rotation = 0.0f,
                        .pivot_x = 0.0f,
                        .pivot_y = 0.0f,
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
    float fvw = static_cast<float>(renderer.virtual_width());
    float fvh = static_cast<float>(renderer.virtual_height());
    uint64_t gen = world.generation();
    uint32_t fi = renderer.current_frame_index();

    if (gen != last_generation_) {
        // Structural change — full rebuild directly into GPU mapped memory.
        rebuild(world, renderer, cam.x, cam.y, fvw, fvh);
        last_generation_ = gen;
        // Mark the OTHER frame slot as dirty so it gets a full write
        // next time it's used.
        frame_dirty_[fi] = false;
        frame_dirty_[fi ^ 1] = true;
    } else if (frame_dirty_[fi]) {
        // This frame slot hasn't been written since the last rebuild.
        // The other slot got the rebuild data — we need a full write of
        // all instance data into this slot's buffer, then patch positions.
        SpriteInstance* dst = renderer.map_instance_buffer(instance_count_);
        if (!dst)
            return;

        // Copy the full instance data from the other frame's buffer.
        uint32_t other = fi ^ 1;
        const auto* src =
            static_cast<const SpriteInstance*>(renderer.map_instance_buffer(instance_count_));
        // Can't read from the other buffer easily — it might be in use by GPU.
        // Instead, just do a full rebuild into this buffer too.
        rebuild(world, renderer, cam.x, cam.y, fvw, fvh);
        frame_dirty_[fi] = false;
    } else {
        // Position-only frame: patch just pos_x/pos_y at stride directly
        // into the mapped GPU buffer.
        SpriteInstance* dst = renderer.map_instance_buffer(instance_count_);
        if (!dst)
            return;

        for (uint32_t i = 0; i < instance_count_; ++i) {
            auto& pos = world.get<Position>(gpu_entities_[i]);
            auto& spr = world.get<Sprite>(gpu_entities_[i]);
            float tw = static_cast<float>(spr.sheet->tile_width());
            float th = static_cast<float>(spr.sheet->tile_height());
            dst[i].pos_x = (pos.x - cam.x) + spr.pivot_x * tw * spr.scale;
            dst[i].pos_y = (pos.y - cam.y) + spr.pivot_y * th * spr.scale;
        }

        renderer.flush_instance_buffer(instance_count_);
    }

    if (instance_count_ == 0)
        return;

    // Record batches (rebuild already flushed; position-only path flushed above).
    for (auto& b : batch_runs_)
        renderer.record_batch(*b.texture, b.z_order, b.first, b.count);
}

void SpriteRenderSystem::rebuild(World& world, Renderer& renderer, float cam_x, float cam_y,
                                 float fvw, float fvh) {
    struct SortKey {
        float z_order;
        const Texture* texture;
        uint32_t index;
    };

    thread_local std::vector<SpriteInstance> instances;
    thread_local std::vector<SortKey> keys;
    thread_local std::vector<Entity> entities;
    instances.clear();
    keys.clear();
    entities.clear();

    world.each<Position, Sprite>([&](Entity e, Position& pos, Sprite& spr) {
        if (!spr.sheet)
            return;
        float sx = pos.x - cam_x;
        float sy = pos.y - cam_y;
        float tw = static_cast<float>(spr.sheet->tile_width());
        float th = static_cast<float>(spr.sheet->tile_height());
        float half_diag = 0.5f * std::sqrt(tw * tw + th * th) * std::abs(spr.scale);
        float cx = sx + tw * 0.5f;
        float cy = sy + th * 0.5f;
        if (cx + half_diag < 0 || cx - half_diag > fvw || cy + half_diag < 0 ||
            cy - half_diag > fvh)
            return;
        auto uv = spr.sheet->region(spr.tile_index);
        auto idx = static_cast<uint32_t>(instances.size());
        instances.push_back({
            .pos_x = sx + spr.pivot_x * tw * spr.scale,
            .pos_y = sy + spr.pivot_y * th * spr.scale,
            .uv_x = uv.x,
            .uv_y = uv.y,
            .uv_w = uv.w,
            .uv_h = uv.h,
            .quad_w = tw,
            .quad_h = th,
            .r = static_cast<float>(spr.tint.r) / 255.0f,
            .g = static_cast<float>(spr.tint.g) / 255.0f,
            .b = static_cast<float>(spr.tint.b) / 255.0f,
            .a = static_cast<float>(spr.tint.a) / 255.0f,
            .scale = spr.scale,
            .rotation = spr.rotation,
            .pivot_x = spr.pivot_x,
            .pivot_y = spr.pivot_y,
        });
        keys.push_back({spr.z_order, &spr.sheet->texture(), idx});
        entities.push_back(e);
    });

    if (keys.empty()) {
        gpu_entities_.clear();
        batch_runs_.clear();
        instance_count_ = 0;
        return;
    }

    // Sort compact keys.
    std::sort(keys.begin(), keys.end(), [](const SortKey& a, const SortKey& b) {
        if (a.z_order != b.z_order)
            return a.z_order < b.z_order;
        return a.texture < b.texture;
    });

    auto total = static_cast<uint32_t>(keys.size());

    // Get mapped GPU buffer.
    SpriteInstance* dst = renderer.map_instance_buffer(total);
    if (!dst)
        return;

    // Build sorted data directly into GPU buffer + gpu_entities_ + batch_runs_.
    gpu_entities_.clear();
    gpu_entities_.reserve(total);
    batch_runs_.clear();

    uint32_t write_pos = 0;
    size_t i = 0;
    while (i < keys.size()) {
        size_t run_start = i;
        const Texture* tex = keys[i].texture;
        float z = keys[i].z_order;
        while (i < keys.size() && keys[i].texture == tex && keys[i].z_order == z)
            ++i;

        uint32_t first = write_pos;
        uint32_t count = static_cast<uint32_t>(i - run_start);
        for (size_t j = run_start; j < i; ++j) {
            dst[write_pos++] = instances[keys[j].index];
            gpu_entities_.push_back(entities[keys[j].index]);
        }
        batch_runs_.push_back({tex, z, first, count});
    }

    instance_count_ = write_pos;
    renderer.flush_instance_buffer(instance_count_);
}

} // namespace xebble
