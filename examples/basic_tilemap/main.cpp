/// @file main.cpp
/// @brief Roguelike dungeon demo using the Angband Adam Bolt 16x16 tileset.
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
#include <memory>
#include <optional>
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
constexpr uint32_t VIEW_TILES_X = VIRTUAL_W / TILE_SIZE;    // 40
constexpr uint32_t VIEW_TILES_Y = VIRTUAL_H / TILE_SIZE;    // 22
constexpr uint32_t HUD_HEIGHT = 2;
constexpr uint32_t MAP_VIEW_TILES_Y = VIEW_TILES_Y - HUD_HEIGHT; // 20

} // namespace

class RoguelikeDemo : public xebble::Game {
    const xebble::SpriteSheet* sheet_ = nullptr;
    const xebble::BitmapFont* font_ = nullptr;
    std::optional<xebble::TileMap> tilemap_;
    Dungeon dungeon_;

    int player_x_ = 0;
    int player_y_ = 0;
    int camera_x_ = 0;
    int camera_y_ = 0;
    int items_collected_ = 0;
    std::string message_ = "Welcome to the dungeon. Use WASD or arrow keys to move.";

public:
    void init(xebble::Renderer& renderer, xebble::AssetManager& assets) override {
        sheet_ = &assets.get<xebble::SpriteSheet>("tiles");
        font_ = &assets.get<xebble::BitmapFont>("font");
        generate_new_dungeon();
    }

    void generate_new_dungeon() {
        dungeon_ = generate_dungeon();
        player_x_ = dungeon_.player_start_x;
        player_y_ = dungeon_.player_start_y;
        items_collected_ = 0;
        message_ = "Welcome to the dungeon. Use WASD or arrow keys to move.";

        tilemap_.emplace(*sheet_, dungeon_.width, dungeon_.height, 2);
        for (int y = 0; y < dungeon_.height; y++) {
            for (int x = 0; x < dungeon_.width; x++) {
                tilemap_->set_tile(0, x, y, dungeon_.floor_tiles[y * dungeon_.width + x]);
                uint32_t feat = dungeon_.feature_tiles[y * dungeon_.width + x];
                if (feat != UINT32_MAX)
                    tilemap_->set_tile(1, x, y, feat);
            }
        }
        update_camera();
    }

    void try_move(int dx, int dy) {
        int nx = player_x_ + dx;
        int ny = player_y_ + dy;

        if (!dungeon_.is_walkable(nx, ny)) {
            message_ = "You bump into a wall.";
            return;
        }

        if (auto* monster = dungeon_.monster_at(nx, ny)) {
            message_ = std::format("You see a {}.", monster->name);
            return;
        }

        player_x_ = nx;
        player_y_ = ny;

        if (auto* item = dungeon_.item_at(nx, ny)) {
            item->alive = false;
            items_collected_++;
            message_ = std::format("You picked up {}.", item->name);
        } else {
            uint32_t feat = dungeon_.feature_tiles[ny * dungeon_.width + nx];
            if (feat == tiles::STAIRS_DOWN)
                message_ = "You see a staircase leading down.";
            else if (feat == tiles::STAIRS_UP)
                message_ = "You see a staircase leading up.";
            else if (feat == tiles::OPEN_DOOR)
                message_ = "You pass through a doorway.";
            else
                message_ = "";
        }
        update_camera();
    }

    void update_camera() {
        camera_x_ = player_x_ - static_cast<int>(VIEW_TILES_X) / 2;
        camera_y_ = player_y_ - static_cast<int>(MAP_VIEW_TILES_Y) / 2;
        camera_x_ = std::clamp(camera_x_, 0, std::max(0, dungeon_.width - static_cast<int>(VIEW_TILES_X)));
        camera_y_ = std::clamp(camera_y_, 0, std::max(0, dungeon_.height - static_cast<int>(MAP_VIEW_TILES_Y)));
    }

    void update(float /*dt*/) override {}

    void on_event(const xebble::Event& event) override {
        if (event.type == xebble::EventType::KeyPress) {
            switch (event.key().key) {
                case xebble::Key::W: case xebble::Key::Up:    try_move(0, -1); break;
                case xebble::Key::S: case xebble::Key::Down:  try_move(0,  1); break;
                case xebble::Key::A: case xebble::Key::Left:  try_move(-1, 0); break;
                case xebble::Key::D: case xebble::Key::Right: try_move( 1, 0); break;
                case xebble::Key::R: generate_new_dungeon(); break;
                case xebble::Key::Escape: std::exit(0); break;
                default: break;
            }
        }
    }

    void draw(xebble::Renderer& renderer) override {
        // --- Tilemap layers ---
        for (uint32_t layer = 0; layer < tilemap_->layer_count(); layer++) {
            std::vector<xebble::SpriteInstance> instances;
            for (int vy = 0; vy <= static_cast<int>(MAP_VIEW_TILES_Y); vy++) {
                for (int vx = 0; vx <= static_cast<int>(VIEW_TILES_X); vx++) {
                    int tx = camera_x_ + vx;
                    int ty = camera_y_ + vy;
                    if (tx < 0 || tx >= dungeon_.width || ty < 0 || ty >= dungeon_.height)
                        continue;
                    auto tile = tilemap_->tile_at(layer, tx, ty);
                    if (!tile) continue;
                    auto uv = sheet_->region(*tile);
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

        // --- Entities ---
        {
            std::vector<xebble::SpriteInstance> instances;
            for (auto& e : dungeon_.entities) {
                if (!e.alive) continue;
                int sx = e.x - camera_x_;
                int sy = e.y - camera_y_;
                if (sx < 0 || sx >= static_cast<int>(VIEW_TILES_X) ||
                    sy < 0 || sy >= static_cast<int>(MAP_VIEW_TILES_Y))
                    continue;
                auto uv = sheet_->region(e.tile);
                instances.push_back({
                    .pos_x = static_cast<float>(sx * TILE_SIZE),
                    .pos_y = static_cast<float>(sy * TILE_SIZE),
                    .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                    .quad_w = static_cast<float>(TILE_SIZE),
                    .quad_h = static_cast<float>(TILE_SIZE),
                    .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                });
            }
            if (!instances.empty())
                renderer.submit_instances(instances, sheet_->texture(), 2.0f);
        }

        // --- Player ---
        {
            int px = player_x_ - camera_x_;
            int py = player_y_ - camera_y_;
            auto uv = sheet_->region(tiles::PLAYER);
            xebble::SpriteInstance inst{
                .pos_x = static_cast<float>(px * TILE_SIZE),
                .pos_y = static_cast<float>(py * TILE_SIZE),
                .uv_x = uv.x, .uv_y = uv.y, .uv_w = uv.w, .uv_h = uv.h,
                .quad_w = static_cast<float>(TILE_SIZE),
                .quad_h = static_cast<float>(TILE_SIZE),
                .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            };
            renderer.submit_instances({&inst, 1}, sheet_->texture(), 3.0f);
        }

        // --- HUD background ---
        {
            std::vector<xebble::SpriteInstance> hud_bg;
            auto wall_uv = sheet_->region(tiles::PERMANENT_WALL);
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
            renderer.submit_instances(hud_bg, sheet_->texture(), 4.0f);
        }

        // --- HUD text ---
        draw_hud(renderer);
    }

    void draw_hud(xebble::Renderer& renderer) {
        float hud_y = static_cast<float>(MAP_VIEW_TILES_Y * TILE_SIZE);
        float gw = static_cast<float>(font_->glyph_width());
        float gh = static_cast<float>(font_->glyph_height());

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
                                   player_x_, player_y_, items_collected_);
        draw_text(status, 4.0f, hud_y + 4.0f, 1.0f, 1.0f, 0.6f);

        if (!message_.empty())
            draw_text(message_, 4.0f, hud_y + 4.0f + gh + 2.0f, 0.8f, 0.8f, 0.8f);
    }

    void layout(uint32_t /*w*/, uint32_t /*h*/) override {}
};

int main() {
    auto assets_dir = find_assets_dir();
    std::filesystem::create_directories(assets_dir);

    // Generate bitmap font atlas
    font_gen::generate_font(assets_dir / "font.png");

    // Write manifest (always, to keep font charset in sync)
    auto manifest_path = assets_dir / "manifest.toml";
    {
        auto charset = font_gen::font_charset();
        // Escape backslash and double-quote for TOML basic string
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

    return xebble::run(std::make_unique<RoguelikeDemo>(), {
        .window = {.title = "Xebble - Roguelike Demo", .width = 1280, .height = 720},
        .renderer = {.virtual_width = VIRTUAL_W, .virtual_height = VIRTUAL_H},
        .assets = {.directory = assets_dir, .manifest = manifest_path},
    });
}
