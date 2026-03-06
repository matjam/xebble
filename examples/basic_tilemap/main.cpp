/// @file main.cpp
/// @brief Roguelike dungeon demo using ECS and the Angband Adam Bolt 16x16 tileset.
///
/// Generates a procedural dungeon with rooms and corridors. Move with WASD or
/// arrow keys. Walk over items to collect them. Bump into monsters to see their
/// name. Press R to regenerate the dungeon. Press Escape to quit.

#include <xebble/xebble.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "dungeon.hpp"
#include "font_gen.hpp"
#include "tiles.hpp"

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
        if (std::filesystem::exists(resources)) return resources;
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

// --- ECS Components ---
struct PlayerTag {};
struct Position { int x, y; };
struct TileSprite { uint32_t tile_id; };
struct MonsterInfo { std::string name; };
struct ItemInfo { std::string name; };
struct Camera { int x, y; };
struct GameState {
    Dungeon dungeon;
    int items_collected = 0;
    std::string message = "Welcome to the dungeon. Use WASD or arrow keys to move.";
    bool needs_rebuild = true;
};

// --- Systems ---

class DungeonSystem : public xebble::System {
public:
    void init(xebble::World& world) override {
        rebuild(world);
    }

    void update(xebble::World& world, float) override {
        auto& state = world.resource<GameState>();
        if (!state.needs_rebuild) return;
        rebuild(world);
    }

private:
    void rebuild(xebble::World& world) {
        auto& state = world.resource<GameState>();
        state.dungeon = generate_dungeon();
        state.items_collected = 0;
        state.message = "Welcome to the dungeon. Use WASD or arrow keys to move.";
        state.needs_rebuild = false;

        // Clear old entities
        std::vector<xebble::Entity> to_destroy;
        world.each<Position>([&](xebble::Entity e, Position&) {
            to_destroy.push_back(e);
        });
        for (auto e : to_destroy) world.destroy(e);
        world.flush_destroyed();

        auto& dg = state.dungeon;

        // Create player
        world.build_entity()
            .with<PlayerTag>({})
            .with<Position>({dg.player_start_x, dg.player_start_y})
            .with<TileSprite>({tiles::PLAYER})
            .build();

        // Create monsters
        for (auto& ent : dg.entities) {
            if (!ent.is_monster) continue;
            world.build_entity()
                .with<Position>({ent.x, ent.y})
                .with<TileSprite>({ent.tile})
                .with<MonsterInfo>({ent.name})
                .build();
        }

        // Create items
        for (auto& ent : dg.entities) {
            if (ent.is_monster) continue;
            world.build_entity()
                .with<Position>({ent.x, ent.y})
                .with<TileSprite>({ent.tile})
                .with<ItemInfo>({ent.name})
                .build();
        }

        // Update camera
        update_camera(world);
    }

    void update_camera(xebble::World& world) {
        auto& cam = world.resource<Camera>();
        world.each<PlayerTag, Position>([&](xebble::Entity, PlayerTag&, Position& pos) {
            cam.x = pos.x - static_cast<int>(VIEW_TILES_X) / 2;
            cam.y = pos.y - static_cast<int>(MAP_VIEW_TILES_Y) / 2;
            auto& dg = world.resource<GameState>().dungeon;
            cam.x = std::clamp(cam.x, 0, std::max(0, dg.width - static_cast<int>(VIEW_TILES_X)));
            cam.y = std::clamp(cam.y, 0, std::max(0, dg.height - static_cast<int>(MAP_VIEW_TILES_Y)));
        });
    }
};

class InputSystem : public xebble::System {
public:
    void update(xebble::World& world, float) override {
        auto& eq = world.resource<xebble::EventQueue>();
        for (auto& event : eq.events) {
            if (event.type != xebble::EventType::KeyPress) continue;
            switch (event.key().key) {
                case xebble::Key::W: case xebble::Key::Up:    try_move(world, 0, -1); break;
                case xebble::Key::S: case xebble::Key::Down:  try_move(world, 0,  1); break;
                case xebble::Key::A: case xebble::Key::Left:  try_move(world, -1, 0); break;
                case xebble::Key::D: case xebble::Key::Right: try_move(world,  1, 0); break;
                case xebble::Key::R: world.resource<GameState>().needs_rebuild = true; break;
                case xebble::Key::Escape: std::exit(0); break;
                default: break;
            }
        }
    }

private:
    void try_move(xebble::World& world, int dx, int dy) {
        auto& state = world.resource<GameState>();
        auto& cam = world.resource<Camera>();

        world.each<PlayerTag, Position>([&](xebble::Entity, PlayerTag&, Position& pos) {
            int nx = pos.x + dx;
            int ny = pos.y + dy;

            if (!state.dungeon.is_walkable(nx, ny)) {
                state.message = "You bump into a wall.";
                return;
            }

            // Check for monster
            bool blocked = false;
            world.each<MonsterInfo, Position>([&](xebble::Entity, MonsterInfo& info, Position& mpos) {
                if (mpos.x == nx && mpos.y == ny) {
                    state.message = std::format("You see a {}.", info.name);
                    blocked = true;
                }
            });
            if (blocked) return;

            pos.x = nx;
            pos.y = ny;

            // Check for item pickup
            bool picked_up = false;
            world.each<ItemInfo, Position>([&](xebble::Entity ie, ItemInfo& info, Position& ipos) {
                if (ipos.x == nx && ipos.y == ny) {
                    state.items_collected++;
                    state.message = std::format("You picked up {}.", info.name);
                    world.destroy(ie);
                    picked_up = true;
                }
            });

            if (!picked_up) {
                uint32_t feat = state.dungeon.feature_tiles[ny * state.dungeon.width + nx];
                if (feat == tiles::STAIRS_DOWN)
                    state.message = "You see a staircase leading down.";
                else if (feat == tiles::STAIRS_UP)
                    state.message = "You see a staircase leading up.";
                else if (feat == tiles::OPEN_DOOR)
                    state.message = "You pass through a doorway.";
                else
                    state.message = "";
            }

            // Update camera
            cam.x = pos.x - static_cast<int>(VIEW_TILES_X) / 2;
            cam.y = pos.y - static_cast<int>(MAP_VIEW_TILES_Y) / 2;
            cam.x = std::clamp(cam.x, 0, std::max(0, state.dungeon.width - static_cast<int>(VIEW_TILES_X)));
            cam.y = std::clamp(cam.y, 0, std::max(0, state.dungeon.height - static_cast<int>(MAP_VIEW_TILES_Y)));
        });
    }
};

class RenderSystem : public xebble::System {
    const xebble::SpriteSheet* sheet_ = nullptr;

public:
    void init(xebble::World& world) override {
        auto* assets = world.resource<xebble::AssetManager*>();
        sheet_ = &assets->get<xebble::SpriteSheet>("tiles");
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& state = world.resource<GameState>();
        auto& cam = world.resource<Camera>();
        auto& dg = state.dungeon;

        // Draw tilemap layers (floor + features)
        for (int layer = 0; layer < 2; layer++) {
            std::vector<xebble::SpriteInstance> instances;
            for (int vy = 0; vy <= static_cast<int>(MAP_VIEW_TILES_Y); vy++) {
                for (int vx = 0; vx <= static_cast<int>(VIEW_TILES_X); vx++) {
                    int tx = cam.x + vx;
                    int ty = cam.y + vy;
                    if (tx < 0 || tx >= dg.width || ty < 0 || ty >= dg.height) continue;
                    uint32_t tile;
                    if (layer == 0) {
                        tile = dg.floor_tiles[ty * dg.width + tx];
                    } else {
                        tile = dg.feature_tiles[ty * dg.width + tx];
                        if (tile == UINT32_MAX) continue;
                    }
                    auto uv = sheet_->region(tile);
                    instances.push_back({
                        .pos_x = static_cast<float>(vx * TILE_SIZE),
                        .pos_y = static_cast<float>(vy * TILE_SIZE),
                        .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                        .quad_w = static_cast<float>(TILE_SIZE),
                        .quad_h = static_cast<float>(TILE_SIZE),
                        .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                    });
                }
            }
            if (!instances.empty())
                renderer.submit_instances(instances, sheet_->texture(), static_cast<float>(layer));
        }

        // Draw entities (monsters + items)
        {
            std::vector<xebble::SpriteInstance> instances;
            world.each<Position, TileSprite>([&](xebble::Entity e, Position& pos, TileSprite& spr) {
                if (world.has<PlayerTag>(e)) return; // player drawn separately
                int sx = pos.x - cam.x;
                int sy = pos.y - cam.y;
                if (sx < 0 || sx >= static_cast<int>(VIEW_TILES_X) ||
                    sy < 0 || sy >= static_cast<int>(MAP_VIEW_TILES_Y))
                    return;
                auto uv = sheet_->region(spr.tile_id);
                instances.push_back({
                    .pos_x = static_cast<float>(sx * TILE_SIZE),
                    .pos_y = static_cast<float>(sy * TILE_SIZE),
                    .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                    .quad_w = static_cast<float>(TILE_SIZE),
                    .quad_h = static_cast<float>(TILE_SIZE),
                    .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                });
            });
            if (!instances.empty())
                renderer.submit_instances(instances, sheet_->texture(), 2.0f);
        }

        // Draw player
        world.each<PlayerTag, Position, TileSprite>([&](xebble::Entity, PlayerTag&, Position& pos, TileSprite& spr) {
            int px = pos.x - cam.x;
            int py = pos.y - cam.y;
            auto uv = sheet_->region(spr.tile_id);
            xebble::SpriteInstance inst{
                .pos_x = static_cast<float>(px * TILE_SIZE),
                .pos_y = static_cast<float>(py * TILE_SIZE),
                .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                .quad_w = static_cast<float>(TILE_SIZE),
                .quad_h = static_cast<float>(TILE_SIZE),
                .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            };
            renderer.submit_instances({&inst, 1}, sheet_->texture(), 3.0f);
        });
    }
};

class HudSystem : public xebble::System {
    const xebble::SpriteSheet* tile_sheet_ = nullptr;
    const xebble::BitmapFont* font_ = nullptr;

public:
    void init(xebble::World& world) override {
        auto* assets = world.resource<xebble::AssetManager*>();
        tile_sheet_ = &assets->get<xebble::SpriteSheet>("tiles");
        font_ = &assets->get<xebble::BitmapFont>("font");
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& state = world.resource<GameState>();
        float hud_y = static_cast<float>(MAP_VIEW_TILES_Y * TILE_SIZE);
        float gw = static_cast<float>(font_->glyph_width());
        float gh = static_cast<float>(font_->glyph_height());

        // HUD background
        {
            std::vector<xebble::SpriteInstance> hud_bg;
            auto wall_uv = tile_sheet_->region(tiles::PERMANENT_WALL);
            for (uint32_t x = 0; x < VIEW_TILES_X; x++) {
                for (uint32_t row = 0; row < HUD_HEIGHT; row++) {
                    hud_bg.push_back({
                        .pos_x = static_cast<float>(x * TILE_SIZE),
                        .pos_y = static_cast<float>(MAP_VIEW_TILES_Y * TILE_SIZE + row * TILE_SIZE),
                        .uv_x = wall_uv.x, .uv_y = wall_uv.y,
                        .uv_w = wall_uv.w, .uv_h = wall_uv.h,
                        .quad_w = static_cast<float>(TILE_SIZE),
                        .quad_h = static_cast<float>(TILE_SIZE),
                        .r = 0.2f, .g = 0.2f, .b = 0.25f, .a = 1.0f,
                    });
                }
            }
            renderer.submit_instances(hud_bg, tile_sheet_->texture(), 4.0f);
        }

        // HUD text
        int player_x = 0, player_y = 0;
        world.each<PlayerTag, Position>([&](xebble::Entity, PlayerTag&, Position& pos) {
            player_x = pos.x;
            player_y = pos.y;
        });

        auto draw_text = [&](const std::string& text, float x, float y,
                              float r, float g, float b) {
            std::vector<xebble::SpriteInstance> glyphs;
            for (size_t i = 0; i < text.size(); i++) {
                auto gi = font_->glyph_index(text[i]);
                if (!gi) continue;
                auto uv = font_->sheet().region(*gi);
                glyphs.push_back({
                    .pos_x = x + static_cast<float>(i) * gw,
                    .pos_y = y,
                    .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                    .quad_w = gw, .quad_h = gh,
                    .r = r, .g = g, .b = b, .a = 1.0f,
                });
            }
            if (!glyphs.empty())
                renderer.submit_instances(glyphs, font_->sheet().texture(), 5.0f);
        };

        auto status = std::format("Pos:({},{}) Items:{} [R]egen [Esc]ape",
                                   player_x, player_y, state.items_collected);
        draw_text(status, 4.0f, hud_y + 4.0f, 1.0f, 1.0f, 0.6f);

        if (!state.message.empty())
            draw_text(state.message, 4.0f, hud_y + 4.0f + gh + 2.0f, 0.8f, 0.8f, 0.8f);
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
            if (c == '\\') escaped += "\\\\";
            else if (c == '"') escaped += "\\\"";
            else escaped += c;
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

    // Register components
    world.register_component<PlayerTag>();
    world.register_component<Position>();
    world.register_component<TileSprite>();
    world.register_component<MonsterInfo>();
    world.register_component<ItemInfo>();

    // Add resources
    world.add_resource<Camera>({0, 0});
    world.add_resource<GameState>({});

    // Add systems (order matters)
    world.add_system<DungeonSystem>();
    world.add_system<InputSystem>();
    world.add_system<RenderSystem>();
    world.add_system<HudSystem>();

    return xebble::run(std::move(world), {
        .window = {.title = "Xebble - Roguelike Demo", .width = 1280, .height = 720},
        .renderer = {.virtual_width = VIRTUAL_W, .virtual_height = VIRTUAL_H},
        .assets = {.directory = assets_dir, .manifest = manifest_path},
    });
}
