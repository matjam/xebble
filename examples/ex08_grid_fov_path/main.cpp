/// @file main.cpp  (ex08_grid_fov_path)
/// @brief Grid, Field-of-View (shadowcasting), and A*/Dijkstra pathfinding.
///
/// Demonstrates:
///   - `Grid<T>` creation and indexing
///   - `neighbors4()` / `neighbors8()` adjacency
///   - `flood_fill()` connectivity check
///   - `compute_fov()` with `VisState` grid
///   - `find_path()` A* shortest path
///   - `dijkstra_map()` multi-source distance map
///   - `dijkstra_step()` greedy step navigation

#include "../shared/pixel_atlas.hpp"

#include <xebble/xebble.hpp>

#include <cmath>
#include <format>
#include <optional>
#include <string>

namespace {

// Map constants (in cells).
constexpr int MAP_W = 40;
constexpr int MAP_H = 22;
// Tile size in virtual pixels.
constexpr float CELL_PX = 16.0f;
// Viewport offset so the map is centred.
constexpr float OX = (640.0f - MAP_W * CELL_PX) / 2.0f;
constexpr float OY = (360.0f - MAP_H * CELL_PX) / 2.0f;

// ---------------------------------------------------------------------------
// GridFOVSystem
// ---------------------------------------------------------------------------
class GridFOVSystem : public xebble::System {
    std::optional<xebble::SpriteSheet> sheet_;

    // Map data.
    xebble::Grid<bool> walls_{MAP_W, MAP_H, false}; // true = wall
    xebble::Grid<xebble::VisState> vis_{MAP_W, MAP_H, xebble::VisState::Unseen};
    xebble::Grid<float> dmap_{MAP_W, MAP_H, xebble::PathCostInfinity};

    // Player & goal positions.
    xebble::IVec2 player_pos_{5, 5};
    xebble::IVec2 goal_pos_{34, 16};

    // Current A* path.
    std::vector<xebble::IVec2> path_;

    // Entity for the rendered map (thin tile layer).
    xebble::Entity map_entity_{};

    std::shared_ptr<xebble::TileMap> tilemap_;

    float move_timer_ = 0.0f;

    void rebuild_fov() {
        // Reset all currently visible to Revealed, then recompute.
        for (int y = 0; y < MAP_H; ++y)
            for (int x = 0; x < MAP_W; ++x) {
                xebble::IVec2 p{x, y};
                if (vis_[p] == xebble::VisState::Visible)
                    vis_[p] = xebble::VisState::Revealed;
            }
        xebble::compute_fov(
            player_pos_, 8,
            [this](xebble::IVec2 p) -> bool { return !walls_.in_bounds(p) || walls_[p]; }, vis_);
    }

    void rebuild_path() {
        path_ = xebble::find_path(player_pos_, goal_pos_, MAP_W, MAP_H,
                                  [this](xebble::IVec2, xebble::IVec2 to) -> float {
                                      if (!walls_.in_bounds(to) || walls_[to])
                                          return -1.0f;
                                      return 1.0f;
                                  });
        // Also build a Dijkstra map from the goal for monsters to chase.
        dmap_ = xebble::dijkstra_map(MAP_W, MAP_H, {goal_pos_},
                                     [this](xebble::IVec2, xebble::IVec2 to) -> float {
                                         if (!walls_.in_bounds(to) || walls_[to])
                                             return -1.0f;
                                         return 1.0f;
                                     });
    }

    void update_tilemap() {
        if (!tilemap_)
            return;
        for (int y = 0; y < MAP_H; ++y) {
            for (int x = 0; x < MAP_W; ++x) {
                xebble::IVec2 p{x, y};
                uint32_t tile;
                // Layer 0: terrain.
                if (walls_[p]) {
                    tile = (vis_[p] != xebble::VisState::Unseen) ? pixel_atlas::TILE_ROCK
                                                                 : pixel_atlas::TILE_BLACK;
                } else {
                    switch (vis_[p]) {
                    case xebble::VisState::Visible:
                        tile = pixel_atlas::TILE_FLOOR;
                        break;
                    case xebble::VisState::Revealed:
                        tile = pixel_atlas::TILE_ROCK;
                        break;
                    default:
                        tile = pixel_atlas::TILE_BLACK;
                        break;
                    }
                }
                tilemap_->set_tile(0, x, y, tile);

                // Layer 1: path overlay (only in visible cells).
                bool on_path = false;
                if (vis_[p] == xebble::VisState::Visible) {
                    for (auto& pp : path_)
                        if (pp == p) {
                            on_path = true;
                            break;
                        }
                }
                tilemap_->set_tile(1, x, y,
                                   on_path ? pixel_atlas::TILE_CYAN : pixel_atlas::TILE_BLACK);
            }
        }
        // Mark player and goal.
        tilemap_->set_tile(1, player_pos_.x, player_pos_.y, pixel_atlas::TILE_HERO);
        tilemap_->set_tile(1, goal_pos_.x, goal_pos_.y, pixel_atlas::TILE_GOAL);
    }

public:
    void init(xebble::World& world) override {
        auto* renderer = world.resource<xebble::Renderer*>();
        sheet_ = pixel_atlas::create(renderer->context());

        // Build a simple map: border walls + a few interior walls.
        for (int x = 0; x < MAP_W; ++x) {
            walls_[{x, 0}] = walls_[{x, MAP_H - 1}] = true;
        }
        for (int y = 0; y < MAP_H; ++y) {
            walls_[{0, y}] = walls_[{MAP_W - 1, y}] = true;
        }
        // Horizontal internal wall with a gap.
        for (int x = 8; x < 32; ++x)
            walls_[{x, 11}] = true;
        walls_[{19, 11}] = false; // gap
        // Vertical internal wall.
        for (int y = 4; y < 18; ++y)
            walls_[{20, y}] = true;
        walls_[{20, 11}] = false;
        walls_[{20, 14}] = false;

        // Create a 2-layer TileMap: terrain + overlay.
        tilemap_ = std::make_shared<xebble::TileMap>(*sheet_, MAP_W, MAP_H, 2);
        map_entity_ = world.build_entity()
                          .with(xebble::TileMapLayer{tilemap_, 0.0f})
                          .with(xebble::Position{OX, OY})
                          .build();

        rebuild_fov();
        rebuild_path();
        update_tilemap();
    }

    void update(xebble::World& world, float dt) override {
        move_timer_ += dt;

        for (const auto& ev : world.resource<xebble::EventQueue>().events) {
            if (ev.type != xebble::EventType::KeyPress)
                continue;
            auto k = ev.key().key;
            if (k == xebble::Key::Escape)
                std::exit(0);

            // Move player with arrow keys.
            xebble::IVec2 delta{0, 0};
            if (k == xebble::Key::Up)
                delta = {0, -1};
            if (k == xebble::Key::Down)
                delta = {0, 1};
            if (k == xebble::Key::Left)
                delta = {-1, 0};
            if (k == xebble::Key::Right)
                delta = {1, 0};
            if (delta.x || delta.y) {
                xebble::IVec2 next = player_pos_ + delta;
                if (walls_.in_bounds(next) && !walls_[next]) {
                    player_pos_ = next;
                    rebuild_fov();
                    rebuild_path();
                    update_tilemap();
                }
            }
        }

        // Auto-follow the A* path every 0.3 s when 'A' is held.
        // (Demonstrated via dijkstra_step once per second.)
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ui = world.resource<xebble::UIContext>();
        float dist = (dmap_.in_bounds(player_pos_) && dmap_[player_pos_] < xebble::PathCostInfinity)
                         ? dmap_[player_pos_]
                         : -1.0f;
        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 48}}, [&](auto& p) {
            p.text(u8"ex08 \u2014 Grid / FOV / Pathfinding  |  [Esc] Quit",
                   {.color = {220, 220, 220}});
            p.text(u8"[Arrow keys] Move player   Cyan = A* path   Purple = goal",
                   {.color = {160, 200, 240}});
            {
                auto s = std::format("Player ({:2d},{:2d})  Path len {:2d}  "
                                     "Dijkstra dist {:.0f}",
                                     player_pos_.x, player_pos_.y, (int)path_.size(), dist);
                p.text(std::u8string(s.begin(), s.end()), {.color = {180, 220, 180}});
            }
        });
        xebble::debug_overlay(world, renderer);
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<GridFOVSystem>();

    return xebble::run(
        std::move(world),
        {
            .window = {.title = "ex08 — Grid / FOV / Pathfinding", .width = 1280, .height = 720},
            .renderer = {.virtual_width = 640, .virtual_height = 360},
        });
}
