/// @file main.cpp  (ex06_ecs)
/// @brief ECS deep-dive: component registration, multi-component queries,
///        entity lifecycle, and resource access.
///
/// Demonstrates:
///   - Defining and registering custom component types
///   - `world.build_entity().with<T>().build()` fluent builder
///   - `world.each<T>()` single-component iteration
///   - `world.each<T1, T2>()` multi-component filtered iteration
///   - Deferred entity destruction inside an each() loop
///   - Custom resources (`world.add_resource<T>()`)
///   - Querying resources from systems

#include "../shared/pixel_atlas.hpp"

#include <xebble/xebble.hpp>

#include <cmath>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>

#ifdef XEBBLE_PROFILE
#include <gperftools/profiler.h>
#endif

#define SPAWN_EXTRA_COUNT 100000

namespace {

// ---------------------------------------------------------------------------
// Custom component types
// ---------------------------------------------------------------------------

struct Velocity {
    float vx = 0.0f;
    float vy = 0.0f;
};
struct Health {
    int hp = 0;
    int max_hp = 0;
};
struct Lifetime {
    float remaining = 0.0f;
}; // particle expires

// ---------------------------------------------------------------------------
// Custom resource
// ---------------------------------------------------------------------------

struct Stats {
    int entities_alive = 0;
    int particles_born = 0;
    int particles_died = 0;
    float spawn_timer = 0.0f;
    int bulk_spawned = 0; // total from Space key
    int bulk_removed = 0; // total from Delete key
#ifdef XEBBLE_PROFILE
    int profile_frames = 0; // frames elapsed since profiling started
#endif
};

// ---------------------------------------------------------------------------
// ECS demo system
// ---------------------------------------------------------------------------

class ECSSystem : public xebble::System {
    std::optional<xebble::SpriteSheet> sheet_;
    xebble::Entity player_{};
    xebble::Rng rng_{12345};
    static constexpr uint32_t bulk_tiles_[6] = {
        pixel_atlas::TILE_CYAN,    pixel_atlas::TILE_GREEN,  pixel_atlas::TILE_BLUE,
        pixel_atlas::TILE_MAGENTA, pixel_atlas::TILE_ORANGE, pixel_atlas::TILE_FLOOR,
    };

public:
    void init(xebble::World& world) override {
        auto* renderer = world.resource<xebble::Renderer*>();
        sheet_ = pixel_atlas::create(renderer->context());

        // Register custom component types (Position and Sprite are
        // auto-registered).
        world.register_component<Velocity>();
        world.register_component<Health>();
        world.register_component<Lifetime>();

        // Add a custom resource.
        world.add_resource(Stats{});

        // Spawn the player entity using the fluent builder.
        player_ = world.build_entity()
                      .with(xebble::Position{304.0f, 164.0f})
                      .with(xebble::Sprite{&*sheet_, pixel_atlas::TILE_HERO, 5.0f})
                      .with(Health{10, 10})
                      .build();
        // (Player has no Velocity — stays in place.)

        // Pre-spawn a few "enemy" entities to show multi-component queries.
        for (int i = 0; i < 5; ++i) {
            float angle = static_cast<float>(i) * (6.2831853f / 5.0f);
            float r = 80.0f;
            (void)world.build_entity()
                .with(xebble::Position{304.0f + r * std::cos(angle), 164.0f + r * std::sin(angle)})
                .with(xebble::Sprite{&*sheet_, pixel_atlas::TILE_RED, 2.0f})
                .with(Velocity{std::cos(angle + 1.5f) * 40.0f, std::sin(angle + 1.5f) * 40.0f})
                .with(Health{3, 3})
                .build();
        }

#ifdef XEBBLE_PROFILE
        // Bulk-spawn 500k entities for profiling (with velocity — tests
        // position-only update path).
        constexpr int PROFILE_ENTITY_COUNT = 500'000;
        for (int i = 0; i < PROFILE_ENTITY_COUNT; ++i) {
            float angle = rng_.range(0, 628) * 0.01f;
            float speed = rng_.range(20, 120) * 1.0f;
            float x = rng_.range(0, 640) * 1.0f;
            float y = rng_.range(0, 360) * 1.0f;
            uint32_t tile = bulk_tiles_[rng_.range(0, 6)];
            world.build_entity()
                .with(xebble::Position{x, y})
                .with(xebble::Sprite{&*sheet_, tile, 3.0f})
                .with(Velocity{std::cos(angle) * speed, std::sin(angle) * speed})
                .with(Health{1, 1})
                .build();
        }
        world.resource<Stats>().entities_alive = 6 + PROFILE_ENTITY_COUNT;
        world.resource<Stats>().bulk_spawned = PROFILE_ENTITY_COUNT;
        setenv("CPUPROFILE_FREQUENCY", "1000", 1);
        ProfilerStart("ex06_ecs.prof");
#else
        world.resource<Stats>().entities_alive = 6; // 1 player + 5 enemies
#endif
    }

    void update(xebble::World& world, float dt) override {
        auto& stats = world.resource<Stats>();

        for (const auto& ev : world.resource<xebble::EventQueue>().events) {
            if (ev.type != xebble::EventType::KeyPress)
                continue;
            auto key = ev.key().key;

            if (key == xebble::Key::Escape)
                std::exit(0);

            if (key == xebble::Key::Space) {
                // Spawn SPAWN_EXTRA_COUNT entities with random positions and
                // velocities.
                for (int i = 0; i < SPAWN_EXTRA_COUNT; ++i) {
                    float angle = rng_.range(0, 628) * 0.01f;
                    float speed = rng_.range(20, 120) * 1.0f;
                    float x = rng_.range(0, 640) * 1.0f;
                    float y = rng_.range(0, 360) * 1.0f;
                    uint32_t tile = bulk_tiles_[rng_.range(0, 6)];
                    (void)world.build_entity()
                        .with(xebble::Position{x, y})
                        .with(xebble::Sprite{&*sheet_, tile, 3.0f})
                        .with(Velocity{std::cos(angle) * speed, std::sin(angle) * speed})
                        .with(Health{1, 1})
                        .build();
                }
                stats.bulk_spawned += SPAWN_EXTRA_COUNT;
                stats.entities_alive += SPAWN_EXTRA_COUNT;
            }

            if (key == xebble::Key::Delete || key == xebble::Key::Backspace) {
                // Remove up to SPAWN_EXTRA_COUNT entities that have Health (bulk
                // entities). Skip the player by checking for Velocity (player has
                // none).
                int removed = 0;
                std::vector<xebble::Entity> to_kill;
                world.each<Health, Velocity>([&](xebble::Entity e, Health&, Velocity&) {
                    if (removed < SPAWN_EXTRA_COUNT) {
                        to_kill.push_back(e);
                        ++removed;
                    }
                });
                for (auto e : to_kill)
                    world.destroy(e);
                stats.bulk_removed += removed;
                stats.entities_alive -= removed;
            }
        }

        // --- Multi-component iteration: move all entities with Position+Velocity.
        // This skips the player (no Velocity component).
        world.each<xebble::Position, Velocity>(
            [&](xebble::Entity, xebble::Position& pos, Velocity& vel) {
                pos.x += vel.vx * dt;
                pos.y += vel.vy * dt;
                // Wrap around the virtual screen (640x360).
                if (pos.x < 0)
                    pos.x += 640.0f;
                if (pos.x > 640)
                    pos.x -= 640.0f;
                if (pos.y < 0)
                    pos.y += 360.0f;
                if (pos.y > 360)
                    pos.y -= 360.0f;
            });

        // --- Spawn particles periodically.
        stats.spawn_timer += dt;
        if (stats.spawn_timer >= 0.3f) {
            stats.spawn_timer = 0.0f;
            float angle = static_cast<float>(stats.particles_born) * 0.7f;
            (void)world.build_entity()
                .with(xebble::Position{304.0f, 164.0f})
                .with(xebble::Sprite{&*sheet_, pixel_atlas::TILE_YELLOW, 1.0f, {220, 220, 50, 200}})
                .with(Velocity{std::cos(angle) * 60.0f, std::sin(angle) * 60.0f})
                .with(Lifetime{2.0f})
                .build();
            ++stats.particles_born;
            ++stats.entities_alive;
        }

        // --- Single-component iteration: tick Lifetime, destroy expired particles.
        // Use deferred destroy (safe inside each()).
        world.each<Lifetime>([&](xebble::Entity e, Lifetime& lt) {
            lt.remaining -= dt;
            // Fade out via alpha tint.
            float alpha = std::max(0.0f, lt.remaining / 2.0f);
            world.get<xebble::Sprite>(e).tint.a = static_cast<uint8_t>(alpha * 200.0f);
            if (lt.remaining <= 0.0f) {
                world.destroy(e); // deferred — safe inside each()
                ++stats.particles_died;
                --stats.entities_alive;
            }
        });

#ifdef XEBBLE_PROFILE
        constexpr int PROFILE_FRAME_LIMIT = 600;
        if (++stats.profile_frames >= PROFILE_FRAME_LIMIT) {
            ProfilerStop();
            std::exit(0);
        }
#endif
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& stats = world.resource<Stats>();
        auto& ui = world.resource<xebble::UIContext>();
        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 50}}, [&](auto& p) {
            p.text(u8"ex06 \u2014 ECS Deep Dive", {.color = {220, 220, 220}});
            {
                auto s = std::format("Entities alive: {:5d}  |  "
                                     "Bulk spawned: {:5d}  |  "
                                     "Bulk removed: {:5d}  |  "
                                     "Particles born: {:4d}  died: {:4d}",
                                     stats.entities_alive, stats.bulk_spawned, stats.bulk_removed,
                                     stats.particles_born, stats.particles_died);
                p.text(std::u8string(s.begin(), s.end()), {.color = {160, 220, 160}});
            }
            p.text(u8"[Space] Spawn 1000 entities   [Del/Bksp] Remove 1000 "
                   u8"entities   [Esc] Quit",
                   {.color = {180, 180, 100}});
        });
        xebble::debug_overlay(world, renderer);
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<ECSSystem>();

    return xebble::run(
        std::move(world),
        {
            .window = {.title = "ex06 — ECS Deep Dive", .width = 1280, .height = 720},
            .renderer = {.virtual_width = 640, .virtual_height = 360},
        });
}
