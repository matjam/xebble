/// @file main.cpp  (ex02_tilemap)
/// @brief Multi-layer tilemap with camera scrolling.
///
/// Demonstrates:
///   - TileMap creation (2 layers: floor + decoration)
///   - TileMapLayer component + TileMapRenderSystem
///   - Camera resource — scrolling the viewport over a large map
///   - Parallax effect via TileMap::set_offset()
///   - WASD / arrow key camera pan

#include <xebble/xebble.hpp>
#include "../shared/pixel_atlas.hpp"

#include <format>
#include <optional>

namespace {

constexpr int MAP_W   = 60;
constexpr int MAP_H   = 40;
constexpr int TILE_PX = 16;   // pixels per tile

// Simple checkerboard dungeon map: walls on borders, open inside.
void fill_floor_layer(xebble::TileMap& map) {
    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            bool wall = (x == 0 || y == 0 || x == MAP_W-1 || y == MAP_H-1);
            map.set_tile(0, x, y, wall ? pixel_atlas::TILE_ROCK : pixel_atlas::TILE_FLOOR);
        }
    }
}

// Layer 1: scattered decorations using a simple hash.
void fill_deco_layer(xebble::TileMap& map) {
    for (int y = 1; y < MAP_H-1; ++y) {
        for (int x = 1; x < MAP_W-1; ++x) {
            uint32_t h = static_cast<uint32_t>(x * 1013 + y * 733 + x * y * 17) % 100;
            if (h < 3)        map.set_tile(1, x, y, pixel_atlas::TILE_GOAL);
            else if (h < 6)   map.set_tile(1, x, y, pixel_atlas::TILE_YELLOW);
        }
    }
}

class TilemapSystem : public xebble::System {
    std::optional<xebble::SpriteSheet> sheet_;
    float cam_speed_ = 120.0f; // pixels/sec

public:
    void init(xebble::World& world) override {
        auto& ctx = world.resource<xebble::Renderer*>()->context();
        sheet_ = pixel_atlas::create(ctx);

        // Build a 2-layer TileMap.
        auto map = std::make_shared<xebble::TileMap>(*sheet_, MAP_W, MAP_H, 2);
        fill_floor_layer(*map);
        fill_deco_layer(*map);

        // Attach it to an entity.
        world.build_entity()
            .with(xebble::TileMapLayer{map, /*z_order=*/0.0f})
            .build();
    }

    void update(xebble::World& world, float dt) override {
        auto& cam = world.resource<xebble::Camera>();
        auto& eq  = world.resource<xebble::EventQueue>();

        // Quit.
        for (const auto& e : eq.events) {
            if (e.type == xebble::EventType::KeyPress &&
                e.key().key == xebble::Key::Escape) std::exit(0);
        }

        // Held-key scrolling using GLFW key repeat.
        for (const auto& e : eq.events) {
            if (e.type != xebble::EventType::KeyPress &&
                e.type != xebble::EventType::KeyRepeat) continue;
            float step = cam_speed_ * dt;
            switch (e.key().key) {
                case xebble::Key::W: case xebble::Key::Up:    cam.y -= step; break;
                case xebble::Key::S: case xebble::Key::Down:  cam.y += step; break;
                case xebble::Key::A: case xebble::Key::Left:  cam.x -= step; break;
                case xebble::Key::D: case xebble::Key::Right: cam.x += step; break;
                default: break;
            }
        }

        // Clamp camera to map bounds.
        float max_cam_x = float(MAP_W  * TILE_PX - 640);
        float max_cam_y = float(MAP_H  * TILE_PX - 320);
        if (cam.x < 0)        cam.x = 0;
        if (cam.y < 0)        cam.y = 0;
        if (cam.x > max_cam_x) cam.x = max_cam_x;
        if (cam.y > max_cam_y) cam.y = max_cam_y;
    }

    void draw(xebble::World& world, xebble::Renderer&) override {
        auto& cam = world.resource<xebble::Camera>();
        auto& ui  = world.resource<xebble::UIContext>();

        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 34}},
            [&](auto& p) {
                { auto s = std::format("ex02 \u2014 TileMap   Camera ({:.0f}, {:.0f})  Map {}x{}",
                                   cam.x, cam.y, MAP_W, MAP_H);
                  p.text(std::u8string(s.begin(), s.end()), {.color = {220, 220, 220}}); }
                p.text(u8"WASD / Arrow keys to scroll  |  [Esc] Quit",
                       {.color = {160, 160, 160}});
            });
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<TilemapSystem>();

    return xebble::run(std::move(world), {
        .window   = {.title = "ex02 — TileMap", .width = 1280, .height = 720},
        .renderer = {.virtual_width = 640, .virtual_height = 360},
    });
}
