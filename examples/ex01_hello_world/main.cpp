/// @file main.cpp  (ex01_hello_world)
/// @brief Minimal "Hello World" — opens a window and draws a colored sprite.
///
/// Demonstrates:
///   - World setup + xebble::run()
///   - Accessing the Renderer from a resource to create a GPU texture
///   - Creating a SpriteSheet from a procedural pixel texture
///   - Spawning an entity with Position + Sprite
///   - Basic system structure (init / update / draw)
///   - Reading EventQueue and quitting on Escape
///   - Displaying text via UIContext::panel()

#include "../shared/pixel_atlas.hpp"

#include <xebble/xebble.hpp>

#include <cmath>
#include <format>
#include <optional>

namespace {

class HelloSystem : public xebble::System {
    std::optional<xebble::SpriteSheet> sheet_;
    xebble::Entity hero_{};
    float time_ = 0.0f;

public:
    void init(xebble::World& world) override {
        // Create a procedural spritesheet from the pixel atlas helper.
        auto* renderer = world.resource<xebble::Renderer*>();
        sheet_ = pixel_atlas::create(renderer->context());

        // Spawn a single "hero" sprite in the centre of the virtual screen.
        // Position is in virtual pixels (640×360 virtual resolution).
        hero_ = world.build_entity()
                    .with(xebble::Position{310.0f, 168.0f})
                    .with(xebble::Sprite{&*sheet_, pixel_atlas::TILE_HERO, /*z_order=*/5.0f})
                    .build();
    }

    void update(xebble::World& world, float dt) override {
        time_ += dt;

        // Quit on Escape.
        for (const auto& e : world.resource<xebble::EventQueue>().events) {
            if (e.type == xebble::EventType::KeyPress && e.key().key == xebble::Key::Escape) {
                std::exit(0);
            }
        }

        // Bob the sprite up and down using a sine wave.
        auto& pos = world.get<xebble::Position>(hero_);
        pos.y = 168.0f + std::sin(time_ * 2.0f) * 30.0f;

        // Cycle the tint colour through the rainbow.
        auto& spr = world.get<xebble::Sprite>(hero_);
        float hue = std::fmod(time_ * 0.5f, 1.0f);
        // Simple HSV→RGB (S=V=1).
        float h6 = hue * 6.0f;
        int hi = static_cast<int>(h6);
        float f = h6 - hi;
        uint8_t lo = static_cast<uint8_t>((1.0f - f) * 255.0f);
        uint8_t hi8 = static_cast<uint8_t>(f * 255.0f);
        switch (hi % 6) {
        case 0:
            spr.tint = {255, hi8, 0};
            break;
        case 1:
            spr.tint = {lo, 255, 0};
            break;
        case 2:
            spr.tint = {0, 255, hi8};
            break;
        case 3:
            spr.tint = {0, lo, 255};
            break;
        case 4:
            spr.tint = {hi8, 0, 255};
            break;
        default:
            spr.tint = {255, 0, lo};
            break;
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ui = world.resource<xebble::UIContext>();
        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 34}}, [&](auto& p) {
            p.text(u8"ex01 \u2014 Hello World  |  [Esc] Quit", {.color = {220, 220, 220}});
            p.text(u8"A rainbow-tinted sprite bobbing with a sine wave.",
                   {.color = {160, 160, 160}});
        });
        xebble::debug_overlay(world, renderer);
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<HelloSystem>();

    return xebble::run(std::move(world),
                       {
                           .window = {.title = "ex01 — Hello World", .width = 1280, .height = 720},
                           .renderer = {.virtual_width = 640, .virtual_height = 360},
                       });
}
