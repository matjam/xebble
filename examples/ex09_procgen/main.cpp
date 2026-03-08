/// @file main.cpp  (ex09_procgen)
/// @brief Procedural dungeon generation: BSP, cellular automata, drunkard walk.
///
/// Demonstrates:
///   - `BSPNode` + `bsp_split()` room partitioning
///   - `cellular_step()` cave smoothing
///   - `drunkard_walk()` cave carving
///   - `place_rooms()` + `connect_rooms()` room placement
///   - `rect_for_each()` to stamp tiles
///   - `flood_fill()` connectivity check

#include <xebble/xebble.hpp>
#include "../shared/pixel_atlas.hpp"

#include <format>
#include <optional>
#include <string>

namespace {

constexpr int MAP_W = 60;
constexpr int MAP_H = 33;
constexpr float CELL_PX = 10.0f;
constexpr float OX = (640.0f - MAP_W * CELL_PX) / 2.0f;
constexpr float OY = (360.0f - MAP_H * CELL_PX) / 2.0f;

enum class GenMode { BSP, Cellular, Drunkard, Rooms };

struct GenState {
    GenMode mode        = GenMode::BSP;
    uint64_t seed       = 42;
    int      floor_count = 0;
    int      reachable   = 0;
};

class ProcgenSystem : public xebble::System {
    std::optional<xebble::SpriteSheet>  sheet_;
    std::shared_ptr<xebble::TileMap>    tilemap_;
    xebble::Grid<bool>                  map_{MAP_W, MAP_H, true};

    void generate(xebble::World& world) {
        auto& gs  = world.resource<GenState>();
        xebble::Rng rng(gs.seed);

        // Reset to all walls.
        map_.fill(true);

        switch (gs.mode) {
            case GenMode::BSP: {
                xebble::BSPNode root{xebble::IRect{1, 1, MAP_W - 2, MAP_H - 2}};
                xebble::bsp_split(root, rng, 6);
                root.each_leaf([&](const xebble::BSPNode& leaf) {
                    xebble::IRect room = leaf.rect.expand(-1);
                    if (room.valid())
                        xebble::rect_for_each(room, [&](xebble::IVec2 p){ map_[p] = false; });
                });
                break;
            }
            case GenMode::Cellular: {
                // Seed ~50% floor.
                for (int y = 1; y < MAP_H - 1; ++y)
                    for (int x = 1; x < MAP_W - 1; ++x)
                        map_[{x, y}] = rng.chance(0.50f);
                // Smooth 5 generations.
                for (int i = 0; i < 5; ++i)
                    map_ = xebble::cellular_step(map_, 4);
                break;
            }
            case GenMode::Drunkard: {
                xebble::drunkard_walk(map_, {MAP_W / 2, MAP_H / 2}, 700, rng);
                break;
            }
            case GenMode::Rooms: {
                auto rooms = xebble::place_rooms(map_, 4, 10, 3, 7, rng, 40);
                for (auto& r : rooms)
                    xebble::rect_for_each(r, [&](xebble::IVec2 p){ map_[p] = false; });
                if (rooms.size() > 1)
                    xebble::connect_rooms(rooms, map_, rng);
                break;
            }
        }

        // Flood fill to count reachable floor cells.
        xebble::IVec2 seed_pos{MAP_W / 2, MAP_H / 2};
        // Find any floor cell as seed.
        for (int y = 1; y < MAP_H - 1 && map_[seed_pos]; ++y)
            for (int x = 1; x < MAP_W - 1; ++x)
                if (!map_[{x, y}]) { seed_pos = {x, y}; goto found; }
        found:
        auto reach = xebble::flood_fill(seed_pos, map_,
            [&](xebble::IVec2 p){ return !map_[p]; });
        gs.floor_count = 0;
        for (auto v : map_) gs.floor_count += (v ? 0 : 1);
        gs.reachable = static_cast<int>(reach.size());

        // Upload to TileMap.
        if (tilemap_) {
            for (int y = 0; y < MAP_H; ++y)
                for (int x = 0; x < MAP_W; ++x) {
                    xebble::IVec2 p{x, y};
                    tilemap_->set_tile(0, x, y,
                        map_[p] ? pixel_atlas::TILE_ROCK : pixel_atlas::TILE_FLOOR);
                }
        }
    }

public:
    void init(xebble::World& world) override {
        auto* renderer = world.resource<xebble::Renderer*>();
        sheet_ = pixel_atlas::create(renderer->context());

        world.add_resource(GenState{});

        tilemap_ = std::make_shared<xebble::TileMap>(*sheet_, MAP_W, MAP_H, 1);
        world.build_entity()
            .with(xebble::TileMapLayer{tilemap_, 0.0f})
            .with(xebble::Position{OX, OY})
            .build();

        generate(world);
    }

    void update(xebble::World& world, float) override {
        auto& gs = world.resource<GenState>();
        bool regen = false;

        for (const auto& ev : world.resource<xebble::EventQueue>().events) {
            if (ev.type != xebble::EventType::KeyPress) continue;
            auto k = ev.key().key;
            if (k == xebble::Key::Escape) std::exit(0);
            if (k == xebble::Key::Space) { ++gs.seed; regen = true; }
            if (k == xebble::Key::B)  { gs.mode = GenMode::BSP;      regen = true; }
            if (k == xebble::Key::C)  { gs.mode = GenMode::Cellular;  regen = true; }
            if (k == xebble::Key::D)  { gs.mode = GenMode::Drunkard;  regen = true; }
            if (k == xebble::Key::R)  { gs.mode = GenMode::Rooms;     regen = true; }
        }

        if (regen) generate(world);
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& gs = world.resource<GenState>();
        const char* mode_name = [&] {
            switch (gs.mode) {
                case GenMode::BSP:      return "BSP";
                case GenMode::Cellular: return "Cellular";
                case GenMode::Drunkard: return "Drunkard";
                case GenMode::Rooms:    return "Rooms";
            }
            return "?";
        }();

        auto& ui = world.resource<xebble::UIContext>();
        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 48}},
            [&](auto& p) {
                p.text(u8"ex09 \u2014 Procedural Generation  |  [Esc] Quit",
                       {.color = {220, 220, 220}});
                { auto s = std::format("[B]SP  [C]ellular  [D]runkard  [R]ooms  [Space] new seed  "
                                   "Mode: {:s}  Seed: {:d}",
                                   mode_name, gs.seed);
                  p.text(std::u8string(s.begin(), s.end()), {.color = {160, 220, 160}}); }
                { auto s = std::format("Floor cells: {:d}  Reachable: {:d}  "
                                   "({:d}% connected)",
                                   gs.floor_count, gs.reachable,
                                   gs.floor_count > 0
                                   ? gs.reachable * 100 / gs.floor_count : 0);
                  p.text(std::u8string(s.begin(), s.end()), {.color = {180, 180, 240}}); }
            });
        xebble::debug_overlay(world, renderer);
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<ProcgenSystem>();

    return xebble::run(std::move(world), {
        .window   = {.title = "ex09 — Procedural Generation", .width = 1280, .height = 720},
        .renderer = {.virtual_width = 640, .virtual_height = 360},
    });
}
