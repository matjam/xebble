/// @file main.cpp  (ex13_serial)
/// @brief World serialization: snapshot/restore round-trip with
///        ComponentName<T> and ResourceName<T> opt-in traits.
///
/// Demonstrates:
///   - Defining trivially-copyable components and opting them into
///     serialization via `ComponentName<T>` specialization
///   - `world.register_serializable_component<T>()`
///   - Opting resources into serialization via `ResourceName<T>`
///   - `world.add_serializable_resource<T>()`
///   - `world.snapshot()` → `std::vector<uint8_t>` binary blob
///   - `world.restore(blob)` round-trip
///   - Verifying the restored world matches the original

#include <xebble/xebble.hpp>
#include "../shared/pixel_atlas.hpp"

#include <format>
#include <optional>
#include <string>

// ---------------------------------------------------------------------------
// Serializable component types (must be trivially copyable and outside any
// anonymous namespace, because the ComponentName specializations must be
// explicit specializations at namespace scope).
// ---------------------------------------------------------------------------

struct HP {
    int current = 0;
    int maximum = 0;
};

struct Gold {
    int amount = 0;
};

struct SerialPos {
    float x = 0.0f;
    float y = 0.0f;
};

// ---------------------------------------------------------------------------
// Serializable resource
// ---------------------------------------------------------------------------

struct WorldSeed { uint64_t value = 0; };

// ---------------------------------------------------------------------------
// ComponentName / ResourceName specializations (outside anonymous namespace,
// outside namespace xebble, at global scope).
// ---------------------------------------------------------------------------

template<> struct xebble::ComponentName<HP>
    { static constexpr std::string_view value = "ex13::HP"; };

template<> struct xebble::ComponentName<Gold>
    { static constexpr std::string_view value = "ex13::Gold"; };

template<> struct xebble::ComponentName<SerialPos>
    { static constexpr std::string_view value = "ex13::SerialPos"; };

template<> struct xebble::ResourceName<WorldSeed>
    { static constexpr std::string_view value = "ex13::WorldSeed"; };

// ---------------------------------------------------------------------------
// Demo state (not serializable — just for display).
// ---------------------------------------------------------------------------

namespace {

struct SerialState {
    std::vector<uint8_t> blob;
    bool                 has_snapshot  = false;
    int                  entity_count  = 0;
    int                  restored_count = 0;
    std::string          status;
};

class SerialSystem : public xebble::System {
    std::optional<xebble::SpriteSheet> sheet_;

public:
    void init(xebble::World& world) override {
        auto* renderer = world.resource<xebble::Renderer*>();
        sheet_ = pixel_atlas::create(renderer->context());

        // Register serializable component types.
        world.register_serializable_component<HP>();
        world.register_serializable_component<Gold>();
        world.register_serializable_component<SerialPos>();

        // Add a serializable resource.
        world.add_serializable_resource(WorldSeed{99999});

        world.add_resource(SerialState{});

        // Spawn some entities with serializable components.
        spawn_entities(world);
        world.resource<SerialState>().entity_count = count_entities(world);
    }

    void update(xebble::World& world, float) override {
        auto& state = world.resource<SerialState>();

        for (const auto& ev : world.resource<xebble::EventQueue>().events) {
            if (ev.type != xebble::EventType::KeyPress) continue;
            auto k = ev.key().key;
            if (k == xebble::Key::Escape) std::exit(0);

            if (k == xebble::Key::S) {
                // Snapshot the current world state.
                state.blob        = world.snapshot();
                state.has_snapshot = true;
                state.entity_count = count_entities(world);
                state.status = std::format("Snapshot: {:d} bytes, {:d} entities",
                                           state.blob.size(), state.entity_count);
            }

            if (k == xebble::Key::R && state.has_snapshot) {
                // Restore world state from the blob.
                // restore() clears all pools and recreates serializable components.
                // Non-serializable components (Position, Sprite) must be re-added
                // manually from the restored SerialPos data.
                if (auto result = world.restore(state.blob); result) {
                    reattach_visuals(world);
                    state.restored_count = count_entities(world);
                    state.status = std::format("Restored: {:d} entities from {:d}-byte blob",
                                               state.restored_count, state.blob.size());
                } else {
                    state.status = "Restore FAILED: " + std::string(result.error().message);
                }
            }

            if (k == xebble::Key::N) {
                // Spawn more entities to show the diff before/after restore.
                spawn_entities(world);
                state.entity_count = count_entities(world);
                state.status = std::format("Now have {:d} entities (pre-restore)",
                                           state.entity_count);
            }

            if (k == xebble::Key::X) {
                // Destroy all HP entities to demonstrate restore brings them back.
                std::vector<xebble::Entity> to_kill;
                world.each<HP>([&](xebble::Entity e, HP&) { to_kill.push_back(e); });
                for (auto e : to_kill) world.destroy(e);
                state.entity_count = count_entities(world);
                state.status = std::format("Killed all HP entities — now {:d}",
                                           state.entity_count);
            }
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& state = world.resource<SerialState>();
        auto& ui    = world.resource<xebble::UIContext>();

        // Gather entity data for display.
        struct Row { int hp; int gold; float x; float y; };
        std::vector<Row> rows;
        world.each<HP, Gold, SerialPos>([&](xebble::Entity,
                HP& hp, Gold& g, SerialPos& pos) {
            rows.push_back({hp.current, g.amount, pos.x, pos.y});
        });

        uint64_t seed_val = 0;
        if (world.has_resource<WorldSeed>())
            seed_val = world.resource<WorldSeed>().value;

        ui.panel("serial_main", {.anchor = xebble::Anchor::Center, .size = {540, 280}},
            [&](auto& p) {
                p.text(u8"ex13 \u2014 World Serialization", {.color = {220, 220, 100}});
                p.text(u8"[S] Snapshot  [R] Restore  [N] Spawn more  [X] Kill all  [Esc] Quit",
                       {.color = {160, 160, 200}});
                { auto s = std::format("WorldSeed resource: {:d}", seed_val);
                  p.text(std::u8string(s.begin(), s.end()), {.color = {200, 180, 100}}); }
                { auto s = std::format("Entities with HP+Gold+SerialPos: {:d}", (int)rows.size());
                  p.text(std::u8string(s.begin(), s.end()), {.color = {180, 220, 180}}); }

                for (size_t i = 0; i < std::min(rows.size(), size_t(6)); ++i) {
                    auto& r = rows[i];
                    auto s = std::format("  #{:d}: HP {:d}/{:d}  Gold {:d}  pos ({:.0f},{:.0f})",
                                       (int)i, r.hp, r.hp, r.gold, r.x, r.y);
                    p.text(std::u8string(s.begin(), s.end()), {.color = {180, 200, 220}});
                }

                if (!state.status.empty()) {
                    auto s = "Status: " + state.status;
                    p.text(std::u8string(s.begin(), s.end()), {.color = {220, 220, 80}});
                }

                if (state.has_snapshot) {
                    auto s = std::format("Blob size: {:d} bytes", state.blob.size());
                    p.text(std::u8string(s.begin(), s.end()), {.color = {160, 200, 160}});
                }
            });

        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 20}},
            [](auto& p) {
                p.text(u8"ex13 \u2014 Serialization  |  [Esc] Quit",
                       {.color = {200, 200, 200}});
            });
        xebble::debug_overlay(world, renderer);
    }

private:
    void spawn_entities(xebble::World& world) {
        static xebble::Rng rng(77);
        for (int i = 0; i < 4; ++i) {
            world.build_entity()
                .with(HP{rng.range(5, 20), 20})
                .with(Gold{rng.range(0, 100)})
                .with(SerialPos{rng.range(10, 630) * 1.0f,
                                rng.range(10, 350) * 1.0f})
                .with(xebble::Position{rng.range(10, 620) * 1.0f,
                                       rng.range(10, 340) * 1.0f})
                .with(xebble::Sprite{&*sheet_, pixel_atlas::TILE_HERO, 2.0f})
                .build();
        }
    }

    // After restore(), serializable components (HP, Gold, SerialPos) are back
    // but non-serializable ones (Position, Sprite) were cleared.
    // Re-attach them from the restored SerialPos data so entities are visible.
    void reattach_visuals(xebble::World& world) {
        world.each<HP, SerialPos>([&](xebble::Entity e, HP&, SerialPos& sp) {
            world.add<xebble::Position>(e, {sp.x, sp.y});
            world.add<xebble::Sprite>(e, {&*sheet_, pixel_atlas::TILE_HERO, 2.0f});
        });
    }

    int count_entities(xebble::World& world) {
        int n = 0;
        world.each<HP>([&](xebble::Entity, HP&) { ++n; });
        return n;
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<SerialSystem>();

    return xebble::run(std::move(world), {
        .window   = {.title = "ex13 — Serialization", .width = 1280, .height = 720},
        .renderer = {.virtual_width = 640, .virtual_height = 360},
    });
}
