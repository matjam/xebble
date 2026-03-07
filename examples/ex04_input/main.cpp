/// @file main.cpp  (ex04_input)
/// @brief Keyboard and mouse input handling.
///
/// Demonstrates:
///   - KeyPress / KeyRelease / KeyRepeat events
///   - MouseMove / MousePress / MouseScroll events
///   - Renderer::screen_to_virtual() for mouse coordinate conversion
///   - Modifier key detection (Shift, Ctrl, Alt)
///   - Moving a sprite with the keyboard; clicking to teleport it

#include <xebble/xebble.hpp>
#include "../shared/pixel_atlas.hpp"

#include <format>
#include <optional>
#include <string>

namespace {

struct InputState {
    float        cursor_vx = 0, cursor_vy = 0; // virtual-pixel mouse position
    std::string  last_key  = "(none)";
    std::string  last_mouse= "(none)";
    float        scroll_y  = 0.0f;
    bool         shift = false, ctrl = false, alt = false;
};

struct PlayerTag {};

class InputSystem : public xebble::System {
    std::optional<xebble::SpriteSheet> sheet_;
    xebble::Entity                     player_{};
    float speed_ = 80.0f; // pixels/sec

public:
    void init(xebble::World& world) override {
        auto& ctx = world.resource<xebble::Renderer*>()->context();
        sheet_ = pixel_atlas::create(ctx);

        world.register_component<PlayerTag>();

        player_ = world.build_entity()
            .with(xebble::Position{312.0f, 168.0f})
            .with(xebble::Sprite{&*sheet_, pixel_atlas::TILE_HERO, 5.0f})
            .with(PlayerTag{})
            .build();

        world.add_resource(InputState{});
    }

    void update(xebble::World& world, float dt) override {
        auto& state   = world.resource<InputState>();
        auto* renderer= world.resource<xebble::Renderer*>();
        auto& eq      = world.resource<xebble::EventQueue>();
        auto& pos     = world.get<xebble::Position>(player_);

        // Movement via held keys (use KeyRepeat for held key events).
        float dx = 0, dy = 0;
        for (const auto& e : eq.events) {
            if (e.type == xebble::EventType::KeyPress ||
                e.type == xebble::EventType::KeyRepeat) {
                switch (e.key().key) {
                    case xebble::Key::W: case xebble::Key::Up:    dy -= 1; break;
                    case xebble::Key::S: case xebble::Key::Down:  dy += 1; break;
                    case xebble::Key::A: case xebble::Key::Left:  dx -= 1; break;
                    case xebble::Key::D: case xebble::Key::Right: dx += 1; break;
                    case xebble::Key::Escape: std::exit(0); break;
                    default: break;
                }
                // Record last key + modifiers.
                if (e.type == xebble::EventType::KeyPress) {
                    auto m = e.key().mods;
                    state.shift = m.shift; state.ctrl = m.ctrl; state.alt = m.alt;
                    state.last_key = std::format("key={} shift={} ctrl={} alt={}",
                        static_cast<int>(e.key().key),
                        m.shift ? "Y" : "N",
                        m.ctrl  ? "Y" : "N",
                        m.alt   ? "Y" : "N");
                }
            }
            if (e.type == xebble::EventType::KeyRelease) {
                auto m = e.key().mods;
                state.shift = m.shift; state.ctrl = m.ctrl; state.alt = m.alt;
            }
            // Mouse move — convert screen coords to virtual pixels.
            if (e.type == xebble::EventType::MouseMove) {
                auto vp = renderer->screen_to_virtual(e.mouse_move().position);
                state.cursor_vx = vp.x;
                state.cursor_vy = vp.y;
            }
            // Mouse click — teleport sprite.
            if (e.type == xebble::EventType::MousePress) {
                auto vp = renderer->screen_to_virtual(e.mouse_button().position);
                pos.x = vp.x - pixel_atlas::TILE_W / 2.0f;
                pos.y = vp.y - pixel_atlas::TILE_H / 2.0f;
                auto btn = e.mouse_button().button;
                state.last_mouse = std::format("click btn={} at ({:.0f},{:.0f})",
                    static_cast<int>(btn), vp.x, vp.y);
            }
            // Scroll — grow / shrink sprite via tint alpha.
            if (e.type == xebble::EventType::MouseScroll) {
                state.scroll_y += e.mouse_scroll().dy;
            }
        }

        // Diagonal normalise.
        if (dx != 0 && dy != 0) { dx *= 0.707f; dy *= 0.707f; }
        pos.x = std::clamp(pos.x + dx * speed_ * dt, 0.0f, 624.0f);
        pos.y = std::clamp(pos.y + dy * speed_ * dt, 0.0f, 304.0f);

        // Use scroll to tint alpha.
        auto& spr = world.get<xebble::Sprite>(player_);
        uint8_t alpha = static_cast<uint8_t>(
            std::clamp(128.0f + state.scroll_y * 10.0f, 30.0f, 255.0f));
        spr.tint.a = alpha;
    }

    void draw(xebble::World& world, xebble::Renderer&) override {
        auto& ui    = world.resource<xebble::UIContext>();
        auto& state = world.resource<InputState>();
        auto& pos   = world.get<xebble::Position>(player_);

        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 48}},
            [&](auto& p) {
                p.text(u8"ex04 \u2014 Input  |  WASD/Arrows move  Left-click teleports  Scroll = alpha  Esc quit",
                       {.color = {220, 220, 220}});
                { auto s = std::format("Mouse virtual ({:.0f}, {:.0f})   Sprite ({:.0f}, {:.0f})",
                                   state.cursor_vx, state.cursor_vy, pos.x, pos.y);
                  p.text(std::u8string(s.begin(), s.end()), {.color = {180, 220, 180}}); }
                { auto s = std::format("Key: {}   Mouse: {}", state.last_key, state.last_mouse);
                  p.text(std::u8string(s.begin(), s.end()), {.color = {180, 180, 220}}); }
            });
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<InputSystem>();

    return xebble::run(std::move(world), {
        .window   = {.title = "ex04 — Input", .width = 1280, .height = 720},
        .renderer = {.virtual_width = 640, .virtual_height = 360},
    });
}
