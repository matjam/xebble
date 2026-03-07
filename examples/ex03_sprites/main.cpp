/// @file main.cpp  (ex03_sprites)
/// @brief Sprite stress-test combining five techniques in one scene:
///
///   1. Parallax background — three tile layers scrolling at different speeds
///   2. Boid flocking      — 200 sprites with separation/alignment/cohesion
///   3. Particle bursts    — short-lived sparks spawned from boid collisions
///   4. Atlas showcase     — all 16 palette tiles cycling as a ticker strip
///   5. Entity churn       — continuous spawn/despawn visible in the HUD counter

#include <xebble/xebble.hpp>
#include "../shared/pixel_atlas.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <vector>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
namespace {

constexpr float VW = 640.0f;
constexpr float VH = 360.0f;

// Boids
constexpr int   BOID_COUNT     = 200;
constexpr float BOID_SPEED     = 60.0f;
constexpr float BOID_MAX_FORCE = 120.0f;
constexpr float SEP_RADIUS     = 24.0f;
constexpr float ALI_RADIUS     = 48.0f;
constexpr float COH_RADIUS     = 64.0f;
constexpr float SEP_WEIGHT     = 2.2f;
constexpr float ALI_WEIGHT     = 1.0f;
constexpr float COH_WEIGHT     = 0.9f;

// Particles
constexpr int   MAX_PARTICLES  = 512;
constexpr float PARTICLE_LIFE  = 0.7f;
constexpr float PARTICLE_SPEED = 80.0f;

// Parallax
constexpr int   PARALLAX_ROWS = 3;

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

struct Vel { float x = 0, y = 0; };

struct Boid {
    float angle = 0.0f;       // heading in radians
    float flash_timer = 0.0f; // > 0 → show collision flash colour
};

struct Particle {
    float life = 0.0f;        // remaining lifetime (seconds)
    float max_life = 0.0f;
};

struct ParallaxTile {
    int   layer  = 0;         // 0 = far, 1 = mid, 2 = near
    float base_x = 0.0f;     // logical x before scroll offset
};

struct AtlasTicker {
    float timer = 0.0f;
};

// ---------------------------------------------------------------------------
// Math helpers
// ---------------------------------------------------------------------------

inline float dot(float ax, float ay, float bx, float by) {
    return ax*bx + ay*by;
}
inline float length(float x, float y) {
    return std::sqrt(x*x + y*y);
}
inline void normalise(float& x, float& y) {
    float l = length(x, y);
    if (l > 0.0001f) { x /= l; y /= l; }
}
inline float clamp_len(float& x, float& y, float max_len) {
    float l = length(x, y);
    if (l > max_len) { x = x/l*max_len; y = y/l*max_len; }
    return l;
}
inline float wrap(float v, float lo, float hi) {
    float r = hi - lo;
    while (v < lo) v += r;
    while (v >= hi) v -= r;
    return v;
}

// ---------------------------------------------------------------------------
// Main system
// ---------------------------------------------------------------------------

class DemoSystem : public xebble::System {
    std::optional<xebble::SpriteSheet> sheet_;
    float  time_        = 0.0f;
    float  scroll_[3]   = {};   // per-layer scroll offsets
    int    entity_count_= 0;
    int    spawn_total_ = 0;
    int    despawn_total_= 0;
    xebble::Rng rng_{42};

    // Boid position cache (avoids querying ECS for neighbours per-boid)
    struct BoidData { float x, y, vx, vy; xebble::Entity e; };
    std::vector<BoidData> boid_cache_;

public:
    // -----------------------------------------------------------------------
    void init(xebble::World& world) override {
        auto& ctx = world.resource<xebble::Renderer*>()->context();
        sheet_ = pixel_atlas::create(ctx);

        world.register_component<Vel>();
        world.register_component<Boid>();
        world.register_component<Particle>();
        world.register_component<ParallaxTile>();
        world.register_component<AtlasTicker>();

        build_parallax(world);
        build_boids(world);
        build_atlas_ticker(world);
    }

    // -----------------------------------------------------------------------
    void update(xebble::World& world, float dt) override {
        time_ += dt;

        for (const auto& ev : world.resource<xebble::EventQueue>().events) {
            if (ev.type == xebble::EventType::KeyPress &&
                ev.key().key == xebble::Key::Escape)
                std::exit(0);
        }

        tick_parallax(world, dt);
        tick_boids(world, dt);
        tick_particles(world, dt);
        tick_atlas_ticker(world, dt);
        entity_count_ = count_live(world);
    }

    // -----------------------------------------------------------------------
    void draw(xebble::World& world, xebble::Renderer&) override {
        auto& ui = world.resource<xebble::UIContext>();
        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 34}},
            [&](auto& p) {
                { auto s = std::format(
                    "ex03  t={:.1f}s  entities={:d}  spawned={:d}  despawned={:d}",
                    time_, entity_count_, spawn_total_, despawn_total_);
                  p.text(std::u8string(s.begin(), s.end()), {.color = {220, 220, 220}}); }
                p.text(
                    u8"Boids=200  Parallax=3 layers  Particles  Atlas ticker  |  [Esc] Quit",
                    {.color = {150, 150, 150}});
            });
    }

private:
    // =======================================================================
    // Parallax background
    // =======================================================================
    void build_parallax(xebble::World& world) {
        // Three layers of tiles at different densities and tile types.
        const uint32_t tiles[PARALLAX_ROWS] = {
            pixel_atlas::TILE_WATER,  // far  (slowest)
            pixel_atlas::TILE_GRASS,  // mid
            pixel_atlas::TILE_DIRT,   // near (fastest)
        };
        const float ys[PARALLAX_ROWS]    = { 60.0f,  140.0f, 230.0f };
        const float scales[PARALLAX_ROWS]= { 1.0f,    1.5f,   2.0f  };
        // tile size on screen = 16 * scale
        for (int layer = 0; layer < PARALLAX_ROWS; ++layer) {
            float tw  = pixel_atlas::TILE_W * scales[layer];
            int   cnt = static_cast<int>(VW / tw) + 2;
            for (int i = 0; i < cnt; ++i) {
                float x = i * tw;
                auto e = world.build_entity()
                    .with(xebble::Position{x, ys[layer]})
                    .with(xebble::Sprite{&*sheet_, tiles[layer],
                                         float(layer) * 0.1f,          // z
                                         xebble::Color{200,200,200,180}}) // slightly transparent
                    .with(ParallaxTile{layer, x})
                    .build();
                (void)e;
                ++spawn_total_;
            }
        }
    }

    void tick_parallax(xebble::World& world, float dt) {
        const float speeds[PARALLAX_ROWS] = { 12.0f, 28.0f, 60.0f };
        const float scales[PARALLAX_ROWS] = {  1.0f,  1.5f,  2.0f };
        for (int i = 0; i < PARALLAX_ROWS; ++i)
            scroll_[i] = std::fmod(scroll_[i] + speeds[i] * dt, VW);

        world.each<ParallaxTile, xebble::Position>(
            [&](xebble::Entity, ParallaxTile& pt, xebble::Position& pos) {
                float tw = pixel_atlas::TILE_W * scales[pt.layer];
                pos.x = wrap(pt.base_x - scroll_[pt.layer], -tw, VW);
            });
    }

    // =======================================================================
    // Boids
    // =======================================================================
    void build_boids(xebble::World& world) {
        const uint32_t boid_tiles[] = {
            pixel_atlas::TILE_RED,   pixel_atlas::TILE_GREEN,
            pixel_atlas::TILE_CYAN,  pixel_atlas::TILE_MAGENTA,
            pixel_atlas::TILE_ORANGE,pixel_atlas::TILE_YELLOW,
        };
        for (int i = 0; i < BOID_COUNT; ++i) {
            float angle = rng_.range(0, 628) * 0.01f;  // 0..2π
            float vx = std::cos(angle) * BOID_SPEED;
            float vy = std::sin(angle) * BOID_SPEED;
            uint32_t tile = boid_tiles[i % 6];
            world.build_entity()
                .with(xebble::Position{
                    rng_.range(0, (int)VW) * 1.0f,
                    rng_.range(0, (int)VH) * 1.0f})
                .with(xebble::Sprite{&*sheet_, tile, 1.0f})
                .with(Vel{vx, vy})
                .with(Boid{angle, 0.0f})
                .build();
            ++spawn_total_;
        }
    }

    void tick_boids(xebble::World& world, float dt) {
        // Rebuild cache
        boid_cache_.clear();
        world.each<Boid, xebble::Position, Vel>(
            [&](xebble::Entity e, Boid&, xebble::Position& p, Vel& v) {
                boid_cache_.push_back({p.x, p.y, v.x, v.y, e});
            });

        // Compute steering for each boid
        struct Steer { float fx, fy; bool collision; };
        std::vector<Steer> steers(boid_cache_.size(), {0,0,false});

        for (size_t i = 0; i < boid_cache_.size(); ++i) {
            auto& b = boid_cache_[i];
            float sep_x=0,sep_y=0, ali_x=0,ali_y=0, coh_x=0,coh_y=0;
            int   sep_n=0, ali_n=0, coh_n=0;
            bool  collision = false;

            for (size_t j = 0; j < boid_cache_.size(); ++j) {
                if (i == j) continue;
                auto& o = boid_cache_[j];
                float dx = b.x - o.x, dy = b.y - o.y;
                float d  = length(dx, dy);
                if (d < 0.001f) continue;

                if (d < SEP_RADIUS) {
                    sep_x += dx/d; sep_y += dy/d; ++sep_n;
                    if (d < 8.0f) collision = true;
                }
                if (d < ALI_RADIUS) {
                    ali_x += o.vx; ali_y += o.vy; ++ali_n;
                }
                if (d < COH_RADIUS) {
                    coh_x += o.x; coh_y += o.y; ++coh_n;
                }
            }

            float fx = 0, fy = 0;
            if (sep_n) {
                sep_x/=sep_n; sep_y/=sep_n;
                normalise(sep_x, sep_y);
                fx += sep_x * SEP_WEIGHT;
                fy += sep_y * SEP_WEIGHT;
            }
            if (ali_n) {
                ali_x/=ali_n; ali_y/=ali_n;
                normalise(ali_x, ali_y);
                ali_x -= b.vx/BOID_SPEED; ali_y -= b.vy/BOID_SPEED;
                fx += ali_x * ALI_WEIGHT;
                fy += ali_y * ALI_WEIGHT;
            }
            if (coh_n) {
                coh_x/=coh_n; coh_y/=coh_n;
                float tox = coh_x - b.x, toy = coh_y - b.y;
                normalise(tox, toy);
                fx += tox * COH_WEIGHT;
                fy += toy * COH_WEIGHT;
            }

            // Soft boundary repulsion
            float margin = 40.0f;
            if (b.x < margin)    fx += (margin - b.x) / margin * 3.0f;
            if (b.x > VW-margin) fx -= (b.x - (VW-margin)) / margin * 3.0f;
            if (b.y < margin)    fy += (margin - b.y) / margin * 3.0f;
            if (b.y > VH-margin) fy -= (b.y - (VH-margin)) / margin * 3.0f;

            clamp_len(fx, fy, BOID_MAX_FORCE);
            steers[i] = {fx, fy, collision};
        }

        // Apply steering and spawn particles on collision
        size_t idx = 0;
        world.each<Boid, xebble::Position, Vel, xebble::Sprite>(
            [&](xebble::Entity, Boid& boid, xebble::Position& pos, Vel& vel, xebble::Sprite& spr) {
                if (idx >= steers.size()) return;
                auto& s = steers[idx++];

                vel.x += s.fx * dt;
                vel.y += s.fy * dt;
                // Clamp to speed
                float spd = length(vel.x, vel.y);
                if (spd > 0.001f) {
                    vel.x = vel.x/spd * std::clamp(spd, BOID_SPEED*0.5f, BOID_SPEED*1.5f);
                    vel.y = vel.y/spd * std::clamp(spd, BOID_SPEED*0.5f, BOID_SPEED*1.5f);
                }

                pos.x = wrap(pos.x + vel.x * dt, 0.0f, VW);
                pos.y = wrap(pos.y + vel.y * dt, 0.0f, VH);
                boid.angle = std::atan2(vel.y, vel.x);

                // Rotate sprite to face heading direction.
                // Boids are squares so add π/4 to make a corner point forward.
                spr.rotation = boid.angle + std::numbers::pi_v<float> * 0.25f;
                spr.pivot_x  = 0.5f;
                spr.pivot_y  = 0.5f;
                spr.scale    = 1.0f;

                // Flash colour on collision
                if (s.collision) {
                    boid.flash_timer = 0.12f;
                    if (rng_.range(0, 10) < 2)  // 20% chance → spark burst
                        spawn_burst(world, pos.x, pos.y);
                }
                if (boid.flash_timer > 0.0f) {
                    boid.flash_timer -= dt;
                    spr.tint = {255, 255, 255, 255};
                } else {
                    // Hue-shift tint by heading angle
                    float hue = std::fmod(boid.angle / (2.0f * std::numbers::pi_v<float>) + 1.0f, 1.0f);
                    spr.tint  = hue_to_rgb(hue);
                }
            });
    }

    // =======================================================================
    // Particles
    // =======================================================================
    void spawn_burst(xebble::World& world, float x, float y) {
        int count = rng_.range(3, 8);
        for (int i = 0; i < count; ++i) {
            float angle = rng_.range(0, 628) * 0.01f;
            float spd   = rng_.range(30, (int)PARTICLE_SPEED);
            float life  = rng_.range(20, 70) * 0.01f;
            world.build_entity()
                .with(xebble::Position{x, y})
                .with(xebble::Sprite{&*sheet_, pixel_atlas::TILE_YELLOW, 5.0f,
                                     xebble::Color{255, 220, 80, 255}})
                .with(Vel{std::cos(angle)*spd, std::sin(angle)*spd})
                .with(Particle{life, life})
                .build();
            ++spawn_total_;
        }
    }

    void tick_particles(xebble::World& world, float dt) {
        std::vector<xebble::Entity> dead;
        world.each<Particle, xebble::Position, Vel, xebble::Sprite>(
            [&](xebble::Entity e, Particle& p, xebble::Position& pos, Vel& vel, xebble::Sprite& spr) {
                p.life -= dt;
                if (p.life <= 0.0f) { dead.push_back(e); return; }

                vel.y += 60.0f * dt;  // gravity
                pos.x += vel.x * dt;
                pos.y += vel.y * dt;

                float t = p.life / p.max_life;  // 1→0 as it dies

                // Shrink and spin as particle dies
                spr.scale    = t * 1.5f;         // starts big, shrinks to 0
                spr.rotation = (1.0f - t) * std::numbers::pi_v<float> * 6.0f; // 3 full spins
                spr.pivot_x  = 0.5f;
                spr.pivot_y  = 0.5f;

                // Fade from yellow → orange → red
                uint8_t alpha = static_cast<uint8_t>(t * 255.0f);
                uint8_t g     = static_cast<uint8_t>(t * t * 200.0f);
                spr.tint = {255, g, 0, alpha};
            });
        for (auto e : dead) { world.destroy(e); ++despawn_total_; }
    }

    // =======================================================================
    // Atlas ticker strip
    // =======================================================================
    void build_atlas_ticker(xebble::World& world) {
        // One entity per palette tile, displayed along the top of the screen.
        for (uint32_t t = 0; t < pixel_atlas::NUM_TILES; ++t) {
            float x = t * (VW / pixel_atlas::NUM_TILES) + 4.0f;
            world.build_entity()
                .with(xebble::Position{x, 8.0f})
                .with(xebble::Sprite{&*sheet_, t, 10.0f})
                .with(AtlasTicker{float(t) * 0.1f})  // staggered phase
                .build();
            ++spawn_total_;
        }
    }

    void tick_atlas_ticker(xebble::World& world, float dt) {
        world.each<AtlasTicker, xebble::Sprite>(
            [&](xebble::Entity, AtlasTicker& tk, xebble::Sprite& spr) {
                tk.timer += dt;
                // Pulse brightness
                float pulse  = 0.5f + 0.5f * std::sin(tk.timer * 3.0f);
                uint8_t bright = static_cast<uint8_t>(180 + pulse * 75.0f);
                spr.tint = {bright, bright, bright, 255};
                // Spin — each tile rotates at a slightly different speed
                spr.rotation = tk.timer * (1.0f + spr.tile_index * 0.15f);
                spr.scale    = 0.8f + 0.2f * pulse;  // gentle size pulse
                spr.pivot_x  = 0.5f;
                spr.pivot_y  = 0.5f;
                // Cycle through all tiles on a staggered timer
                if (tk.timer > 1.5f) {
                    spr.tile_index = (spr.tile_index + 1) % pixel_atlas::NUM_TILES;
                    tk.timer = 0.0f;
                }
            });
    }

    // =======================================================================
    // Utilities
    // =======================================================================
    int count_live(xebble::World& world) {
        int n = 0;
        world.each<xebble::Sprite>([&](xebble::Entity, xebble::Sprite&) { ++n; });
        return n;
    }

    static xebble::Color hue_to_rgb(float h) {
        float h6 = h * 6.0f;
        int   hi = static_cast<int>(h6) % 6;
        float f  = h6 - std::floor(h6);
        auto  b  = [](float v) { return static_cast<uint8_t>(v * 255.0f); };
        switch (hi) {
            case 0:  return {255,      b(f),    0,   255};
            case 1:  return {b(1-f),   255,     0,   255};
            case 2:  return {0,        255,     b(f), 255};
            case 3:  return {0,        b(1-f),  255, 255};
            case 4:  return {b(f),     0,       255, 255};
            default: return {255,      0,       b(1-f), 255};
        }
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<DemoSystem>();

    return xebble::run(std::move(world), {
        .window   = {.title = "ex03 — Sprites", .width = 1280, .height = 720},
        .renderer = {.virtual_width = 640, .virtual_height = 360},
    });
}
