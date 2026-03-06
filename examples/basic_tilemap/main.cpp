/// @file main.cpp
/// @brief Basic tilemap example demonstrating the Xebble framework.
///
/// Generates test tile assets on first run, then renders a simple tilemap
/// with a movable player sprite using WASD keys.

#include <xebble/xebble.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {

/// Generate a 128x128 test spritesheet with 8x8 colored 16x16 tiles.
void generate_test_tiles(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) return;

    constexpr int W = 128, H = 128, TILE = 16;
    std::vector<uint8_t> pixels(W * H * 4);

    // Define some tile colors (RGBA)
    struct TileColor { uint8_t r, g, b; };
    TileColor colors[] = {
        {34, 139, 34},    // 0: green (grass)
        {139, 69, 19},    // 1: brown (wall)
        {70, 130, 180},   // 2: steel blue (water)
        {189, 183, 107},  // 3: khaki (sand)
        {220, 20, 60},    // 4: crimson (player)
        {255, 215, 0},    // 5: gold (treasure)
        {128, 128, 128},  // 6: gray (stone)
        {0, 100, 0},      // 7: dark green (tree)
    };
    constexpr int num_colors = sizeof(colors) / sizeof(colors[0]);

    for (int ty = 0; ty < H / TILE; ty++) {
        for (int tx = 0; tx < W / TILE; tx++) {
            int tile_idx = ty * (W / TILE) + tx;
            auto& c = colors[tile_idx % num_colors];

            for (int py = 0; py < TILE; py++) {
                for (int px = 0; px < TILE; px++) {
                    int x = tx * TILE + px;
                    int y = ty * TILE + py;
                    int idx = (y * W + x) * 4;

                    // Add a subtle checkerboard pattern within each tile
                    bool checker = ((px / 4) + (py / 4)) % 2 == 0;
                    uint8_t shade = checker ? 0 : 20;

                    // Add a 1px border to each tile for visibility
                    bool border = (px == 0 || py == 0 || px == TILE - 1 || py == TILE - 1);
                    if (border) {
                        pixels[idx + 0] = std::max(0, c.r - 40);
                        pixels[idx + 1] = std::max(0, c.g - 40);
                        pixels[idx + 2] = std::max(0, c.b - 40);
                    } else {
                        pixels[idx + 0] = static_cast<uint8_t>(std::min(255, c.r + shade));
                        pixels[idx + 1] = static_cast<uint8_t>(std::min(255, c.g + shade));
                        pixels[idx + 2] = static_cast<uint8_t>(std::min(255, c.b + shade));
                    }
                    pixels[idx + 3] = 255;
                }
            }
        }
    }

    stbi_write_png(path.string().c_str(), W, H, 4, pixels.data(), W * 4);
    std::printf("Generated test tiles: %s\n", path.string().c_str());
}

} // namespace

class BasicTilemap : public xebble::Game {
    const xebble::SpriteSheet* tiles_ = nullptr;
    std::optional<xebble::TileMap> tilemap_;
    xebble::Sprite player_;

public:
    void init(xebble::Renderer& renderer, xebble::AssetManager& assets) override {
        tiles_ = &assets.get<xebble::SpriteSheet>("tiles");

        // Create a 40x22 tilemap (fills 640x352 of the 640x360 virtual resolution)
        tilemap_.emplace(*tiles_, 40, 22, 2);

        // Fill ground layer with grass tiles
        for (uint32_t y = 0; y < 22; y++)
            for (uint32_t x = 0; x < 40; x++)
                tilemap_->set_tile(0, x, y, 0);

        // Place some wall tiles on layer 1
        for (uint32_t x = 5; x < 15; x++) {
            tilemap_->set_tile(1, x, 5, 1);
            tilemap_->set_tile(1, x, 10, 1);
        }
        for (uint32_t y = 5; y <= 10; y++) {
            tilemap_->set_tile(1, 5, y, 1);
            tilemap_->set_tile(1, 14, y, 1);
        }

        // Scatter some decorations
        tilemap_->set_tile(1, 8, 7, 5);  // treasure
        tilemap_->set_tile(1, 10, 8, 5); // treasure
        tilemap_->set_tile(1, 20, 3, 7); // tree
        tilemap_->set_tile(1, 22, 4, 7); // tree
        tilemap_->set_tile(1, 30, 15, 2); // water

        player_ = {
            .position = {160.0f, 120.0f},
            .z_order = 1.0f,
            .sheet = tiles_,
            .source = 4u, // crimson tile as player
        };
    }

    void update(float /*dt*/) override {}

    void on_event(const xebble::Event& event) override {
        if (event.type == xebble::EventType::KeyPress) {
            switch (event.key().key) {
                case xebble::Key::W: player_.position.y -= 16.0f; break;
                case xebble::Key::S: player_.position.y += 16.0f; break;
                case xebble::Key::A: player_.position.x -= 16.0f; break;
                case xebble::Key::D: player_.position.x += 16.0f; break;
                case xebble::Key::Escape: std::exit(0); break;
                default: break;
            }
        }
    }

    void draw(xebble::Renderer& renderer) override {
        // Draw tilemap layers
        if (tilemap_ && tiles_) {
            for (uint32_t layer = 0; layer < tilemap_->layer_count(); layer++) {
                std::vector<xebble::SpriteInstance> instances;
                for (uint32_t y = 0; y < tilemap_->height(); y++) {
                    for (uint32_t x = 0; x < tilemap_->width(); x++) {
                        auto tile = tilemap_->tile_at(layer, x, y);
                        if (!tile) continue;

                        auto uv = tiles_->region(*tile);
                        instances.push_back({
                            .pos_x = static_cast<float>(x * tiles_->tile_width()),
                            .pos_y = static_cast<float>(y * tiles_->tile_height()),
                            .uv_x = uv.x,
                            .uv_y = uv.y,
                            .uv_w = uv.w,
                            .uv_h = uv.h,
                            .quad_w = static_cast<float>(tiles_->tile_width()),
                            .quad_h = static_cast<float>(tiles_->tile_height()),
                            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                        });
                    }
                }
                if (!instances.empty()) {
                    renderer.submit_instances(instances, tiles_->texture(),
                        static_cast<float>(layer));
                }
            }
        }

        // Draw player sprite
        if (tiles_) {
            auto uv = tiles_->region(player_.tile_index());
            xebble::SpriteInstance inst{
                .pos_x = player_.position.x,
                .pos_y = player_.position.y,
                .uv_x = uv.x,
                .uv_y = uv.y,
                .uv_w = uv.w,
                .uv_h = uv.h,
                .quad_w = static_cast<float>(tiles_->tile_width()),
                .quad_h = static_cast<float>(tiles_->tile_height()),
                .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
            };
            renderer.submit_instances({&inst, 1}, tiles_->texture(), 2.0f);
        }
    }

    void layout(uint32_t /*w*/, uint32_t /*h*/) override {}
};

int main() {
    // Generate test assets if they don't exist
    generate_test_tiles("examples/basic_tilemap/assets/tiles.png");

    return xebble::run(std::make_unique<BasicTilemap>(), {
        .window = {.title = "Xebble - Basic Tilemap", .width = 1280, .height = 720},
        .renderer = {.virtual_width = 640, .virtual_height = 360},
        .assets = {.directory = "examples/basic_tilemap/assets/",
                   .manifest = "examples/basic_tilemap/assets/manifest.toml"},
    });
}
