/// @file main.cpp
/// @brief Roguelike dungeon demo using xebble ECS with built-in rendering.
///
/// Generates a procedural dungeon with rooms and corridors. Move with WASD or
/// arrow keys. Walk over items to collect them. Bump into monsters to see their
/// name. Press R to regenerate the dungeon. Press Escape to quit.

#include <xebble/xebble.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "dungeon.hpp"
#include "font_gen.hpp"
#include "tiles.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <string>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

std::filesystem::path find_assets_dir() {
#ifdef __APPLE__
    char exe_buf[4096];
    uint32_t exe_size = sizeof(exe_buf);
    if (_NSGetExecutablePath(exe_buf, &exe_size) == 0) {
        auto resources = std::filesystem::path(exe_buf).parent_path() / "../Resources/assets";
        if (std::filesystem::exists(resources))
            return resources;
    }
#endif
    return "examples/basic_tilemap/assets";
}

constexpr uint32_t TILE_SIZE = 16;
constexpr uint32_t VIRTUAL_W = 640;
constexpr uint32_t VIRTUAL_H = 360;
constexpr uint32_t VIEW_TILES_X = VIRTUAL_W / TILE_SIZE;
constexpr uint32_t VIEW_TILES_Y = VIRTUAL_H / TILE_SIZE;
constexpr uint32_t HUD_HEIGHT = 2;
constexpr uint32_t MAP_VIEW_TILES_Y = VIEW_TILES_Y - HUD_HEIGHT;

// --- Game-specific components ---
struct PlayerTag {};
struct MonsterInfo {
    std::string name;
};
struct ItemInfo {
    std::string name;
};

// --- Game-specific resources ---
struct GameState {
    Dungeon dungeon;
    int items_collected = 0;
    std::u8string message = u8"Welcome to the dungeon. Use WASD or arrow keys to move.";
    bool needs_rebuild = true;
};

void center_camera(xebble::World& world, xebble::Position& player_pos) {
    auto& cam = world.resource<xebble::Camera>();
    auto& dg = world.resource<GameState>().dungeon;
    cam.x =
        player_pos.x - static_cast<float>(VIRTUAL_W) / 2.0f + static_cast<float>(TILE_SIZE) / 2.0f;
    cam.y = player_pos.y - static_cast<float>(VIRTUAL_H - HUD_HEIGHT * TILE_SIZE) / 2.0f +
            static_cast<float>(TILE_SIZE) / 2.0f;
    cam.x = std::clamp(cam.x, 0.0f, static_cast<float>(dg.width * TILE_SIZE - VIRTUAL_W));
    cam.y = std::clamp(
        cam.y, 0.0f,
        static_cast<float>(dg.height * TILE_SIZE - (VIRTUAL_H - HUD_HEIGHT * TILE_SIZE)));
}

// --- Systems ---

class DungeonSystem : public xebble::System {
    const xebble::SpriteSheet* sheet_ = nullptr;

public:
    void init(xebble::World& world) override {
        auto* assets = world.resource<xebble::AssetManager*>();
        sheet_ = &assets->get<xebble::SpriteSheet>("tiles");
        rebuild(world);
    }

    void update(xebble::World& world, float) override {
        auto& state = world.resource<GameState>();
        if (!state.needs_rebuild)
            return;
        rebuild(world);
    }

private:
    void rebuild(xebble::World& world) {
        auto& state = world.resource<GameState>();
        state.dungeon = generate_dungeon();
        state.items_collected = 0;
        state.message = u8"Welcome to the dungeon. Use WASD or arrow keys to move.";
        state.needs_rebuild = false;

        // Clear old entities
        std::vector<xebble::Entity> to_destroy;
        world.each<xebble::Position>(
            [&](xebble::Entity e, xebble::Position&) { to_destroy.push_back(e); });
        world.each<xebble::TileMapLayer>(
            [&](xebble::Entity e, xebble::TileMapLayer&) { to_destroy.push_back(e); });
        for (auto e : to_destroy)
            world.destroy(e);
        world.flush_destroyed();

        auto& dg = state.dungeon;

        // Create tilemap from dungeon data
        auto tilemap = std::make_shared<xebble::TileMap>(*sheet_, dg.width, dg.height, 2);
        for (int y = 0; y < dg.height; y++) {
            for (int x = 0; x < dg.width; x++) {
                tilemap->set_tile(0, x, y, dg.floor_tiles[y * dg.width + x]);
                uint32_t feat = dg.feature_tiles[y * dg.width + x];
                if (feat != UINT32_MAX)
                    tilemap->set_tile(1, x, y, feat);
            }
        }

        world.build_entity().with<xebble::TileMapLayer>({tilemap, 0.0f}).build();

        // Create player
        world.build_entity()
            .with<PlayerTag>({})
            .with<xebble::Position>({static_cast<float>(dg.player_start_x * TILE_SIZE),
                                     static_cast<float>(dg.player_start_y * TILE_SIZE)})
            .with<xebble::Sprite>({sheet_, tiles::PLAYER, 3.0f})
            .build();

        // Create monsters
        for (auto& ent : dg.entities) {
            if (!ent.is_monster)
                continue;
            world.build_entity()
                .with<xebble::Position>(
                    {static_cast<float>(ent.x * TILE_SIZE), static_cast<float>(ent.y * TILE_SIZE)})
                .with<xebble::Sprite>({sheet_, ent.tile, 2.0f})
                .with<MonsterInfo>({ent.name})
                .build();
        }

        // Create items
        for (auto& ent : dg.entities) {
            if (ent.is_monster)
                continue;
            world.build_entity()
                .with<xebble::Position>(
                    {static_cast<float>(ent.x * TILE_SIZE), static_cast<float>(ent.y * TILE_SIZE)})
                .with<xebble::Sprite>({sheet_, ent.tile, 1.0f})
                .with<ItemInfo>({ent.name})
                .build();
        }

        // Update camera
        update_camera(world);
    }

    void update_camera(xebble::World& world) {
        world.each<PlayerTag, xebble::Position>(
            [&](xebble::Entity, PlayerTag&, xebble::Position& pos) { center_camera(world, pos); });
    }
};

class InputSystem : public xebble::System {
public:
    void update(xebble::World& world, float) override {
        auto& eq = world.resource<xebble::EventQueue>();
        for (auto& event : eq.events) {
            if (event.type != xebble::EventType::KeyPress)
                continue;
            switch (event.key().key) {
            case xebble::Key::W:
            case xebble::Key::Up:
                try_move(world, 0, -1);
                break;
            case xebble::Key::S:
            case xebble::Key::Down:
                try_move(world, 0, 1);
                break;
            case xebble::Key::A:
            case xebble::Key::Left:
                try_move(world, -1, 0);
                break;
            case xebble::Key::D:
            case xebble::Key::Right:
                try_move(world, 1, 0);
                break;
            case xebble::Key::R:
                world.resource<GameState>().needs_rebuild = true;
                break;
            case xebble::Key::Escape:
                std::exit(0);
                break;
            default:
                break;
            }
        }
    }

private:
    void try_move(xebble::World& world, int dx, int dy) {
        auto& state = world.resource<GameState>();

        world.each<PlayerTag, xebble::Position>(
            [&](xebble::Entity, PlayerTag&, xebble::Position& pos) {
                int tile_x = static_cast<int>(pos.x) / TILE_SIZE;
                int tile_y = static_cast<int>(pos.y) / TILE_SIZE;
                int nx = tile_x + dx;
                int ny = tile_y + dy;

                if (!state.dungeon.is_walkable(nx, ny)) {
                    state.message = u8"You bump into a wall.";
                    return;
                }

                // Check for monster
                bool blocked = false;
                world.each<MonsterInfo, xebble::Position>(
                    [&](xebble::Entity, MonsterInfo& info, xebble::Position& mpos) {
                        int mx = static_cast<int>(mpos.x) / TILE_SIZE;
                        int my = static_cast<int>(mpos.y) / TILE_SIZE;
                        if (mx == nx && my == ny) {
                            {
                                auto s = std::format("You see a {}.", info.name);
                                state.message = std::u8string(s.begin(), s.end());
                            }
                            blocked = true;
                        }
                    });
                if (blocked)
                    return;

                pos.x = static_cast<float>(nx * TILE_SIZE);
                pos.y = static_cast<float>(ny * TILE_SIZE);

                // Check for item pickup
                bool picked_up = false;
                world.each<ItemInfo, xebble::Position>(
                    [&](xebble::Entity ie, ItemInfo& info, xebble::Position& ipos) {
                        int ix = static_cast<int>(ipos.x) / TILE_SIZE;
                        int iy = static_cast<int>(ipos.y) / TILE_SIZE;
                        if (ix == nx && iy == ny) {
                            state.items_collected++;
                            {
                                auto s = std::format("You picked up {}.", info.name);
                                state.message = std::u8string(s.begin(), s.end());
                            }
                            world.destroy(ie);
                            picked_up = true;
                        }
                    });

                if (!picked_up) {
                    uint32_t feat = state.dungeon.feature_tiles[ny * state.dungeon.width + nx];
                    if (feat == tiles::STAIRS_DOWN)
                        state.message = u8"You see a staircase leading down.";
                    else if (feat == tiles::STAIRS_UP)
                        state.message = u8"You see a staircase leading up.";
                    else if (feat == tiles::OPEN_DOOR)
                        state.message = u8"You pass through a doorway.";
                    else
                        state.message = u8"";
                }

                // Update camera
                center_camera(world, pos);
            });
    }
};

class HudSystem : public xebble::System {
public:
    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ui = world.resource<xebble::UIContext>();
        auto& state = world.resource<GameState>();

        int player_x = 0, player_y = 0;
        world.each<PlayerTag, xebble::Position>(
            [&](xebble::Entity, PlayerTag&, xebble::Position& pos) {
                player_x = static_cast<int>(pos.x) / TILE_SIZE;
                player_y = static_cast<int>(pos.y) / TILE_SIZE;
            });

        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 32}}, [&](auto& p) {
            {
                auto s = std::format("Pos:({},{}) Items:{} [R]egen [Esc]ape", player_x, player_y,
                                     state.items_collected);
                p.text(std::u8string(s.begin(), s.end()), {.color = {255, 255, 150}});
            }
            if (!state.message.empty())
                p.text(state.message, {.color = {200, 200, 200}});
        });
        xebble::debug_overlay(world, renderer);
    }
};

} // namespace

int main() {
    auto assets_dir = find_assets_dir();
    std::filesystem::create_directories(assets_dir);

    // Generate bitmap font atlas
    font_gen::generate_font(assets_dir / "font.png");

    // Write manifest
    auto manifest_path = assets_dir / "manifest.toml";
    {
        auto charset = font_gen::font_charset();
        std::string escaped;
        for (char c : charset) {
            if (c == '\\')
                escaped += "\\\\";
            else if (c == '"')
                escaped += "\\\"";
            else
                escaped += c;
        }

        std::FILE* f = std::fopen(manifest_path.string().c_str(), "w");
        if (f) {
            std::fprintf(f,
                         "[spritesheets.tiles]\n"
                         "path = \"angband-16x16.png\"\n"
                         "tile_width = 16\n"
                         "tile_height = 16\n"
                         "\n"
                         "[bitmap_fonts.font]\n"
                         "path = \"font.png\"\n"
                         "glyph_width = %d\n"
                         "glyph_height = %d\n"
                         "charset = \"%s\"\n",
                         font_gen::GLYPH_W, font_gen::GLYPH_H, escaped.c_str());
            std::fclose(f);
        }
    }

    xebble::World world;

    // Only game-specific components needed
    world.register_component<PlayerTag>();
    world.register_component<MonsterInfo>();
    world.register_component<ItemInfo>();

    world.add_resource<GameState>({});

    // Only game logic systems — no render system needed!
    world.add_system<DungeonSystem>();
    world.add_system<InputSystem>();
    world.add_system<HudSystem>();

    return xebble::run(
        std::move(world),
        {
            .window = {.title = "Xebble - Roguelike Demo", .width = 1280, .height = 720},
            .renderer = {.virtual_width = VIRTUAL_W, .virtual_height = VIRTUAL_H},
            .assets = {.directory = assets_dir, .manifest = manifest_path},
        });
}
