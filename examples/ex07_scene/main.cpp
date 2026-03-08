/// @file main.cpp  (ex07_scene)
/// @brief Scene management: SceneRouter + SceneStack push/pop/replace.
///
/// Demonstrates:
///   - `SceneRouter` with multiple named scenes
///   - `xebble::run(SceneRouter, config)` overload
///   - `SceneTransition::push()` — overlay a scene while the one below keeps drawing
///   - `SceneTransition::pop()` — return to the previous scene
///   - `SceneTransition::replace()` — switch scenes without stacking
///   - Passing a payload between scenes via `std::any`
///   - `DrawBelow::Yes` for transparent overlays

#include <xebble/xebble.hpp>

#include <format>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// Payload type forwarded between scenes
// ---------------------------------------------------------------------------
struct FloorNumber {
    int value = 1;
};

// ---------------------------------------------------------------------------
// Title scene
// ---------------------------------------------------------------------------
class TitleSystem : public xebble::System {
public:
    void update(xebble::World& world, float) override {
        for (const auto& ev : world.resource<xebble::EventQueue>().events) {
            if (ev.type != xebble::EventType::KeyPress)
                continue;
            auto k = ev.key().key;
            if (k == xebble::Key::Escape)
                std::exit(0);
            if (k == xebble::Key::Enter || k == xebble::Key::Space) {
                // Replace title with gameplay, passing floor 1 as payload.
                world.resource<xebble::SceneTransition>() =
                    xebble::SceneTransition::replace("gameplay", FloorNumber{1});
            }
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ui = world.resource<xebble::UIContext>();
        ui.panel("title", {.anchor = xebble::Anchor::Center, .size = {400, 160}}, [](auto& p) {
            p.text(u8"ex07 \u2014 Scene Manager", {.color = {220, 220, 100}});
            p.text(u8"TITLE SCREEN", {.color = {255, 255, 255}});
            p.text(u8"[Enter/Space] \u2192 Gameplay", {.color = {180, 220, 180}});
            p.text(u8"[Esc] Quit", {.color = {160, 160, 160}});
        });
        xebble::debug_overlay(world, renderer);
    }
};

// ---------------------------------------------------------------------------
// Gameplay scene
// ---------------------------------------------------------------------------
class GameplaySystem : public xebble::System {
    int floor_ = 1;
    float time_ = 0.0f;

public:
    void init(xebble::World& world) override {
        // Retrieve the floor number from the concrete payload resource.
        if (world.has_resource<FloorNumber>())
            floor_ = world.resource<FloorNumber>().value;
    }

    void update(xebble::World& world, float dt) override {
        time_ += dt;
        for (const auto& ev : world.resource<xebble::EventQueue>().events) {
            if (ev.type != xebble::EventType::KeyPress)
                continue;
            auto k = ev.key().key;
            if (k == xebble::Key::Escape)
                // Push the pause overlay on top; gameplay keeps drawing below.
                world.resource<xebble::SceneTransition>() =
                    xebble::SceneTransition::push("pause", {}, xebble::DrawBelow::Yes);
            if (k == xebble::Key::N) {
                // Descend to the next floor (replace this scene with fresh payload).
                world.resource<xebble::SceneTransition>() =
                    xebble::SceneTransition::replace("gameplay", FloorNumber{floor_ + 1});
            }
            if (k == xebble::Key::T) {
                // Jump back to title.
                world.resource<xebble::SceneTransition>() =
                    xebble::SceneTransition::pop_all_and_push("title");
            }
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ui = world.resource<xebble::UIContext>();
        // Draw the gameplay "dungeon" background panel.
        ui.panel("game_bg", {.anchor = xebble::Anchor::Center, .size = {480, 200}},
                 [this](auto& p) {
                     {
                         auto s = std::format("GAMEPLAY \u2014 Floor {:d}", floor_);
                         p.text(std::u8string(s.begin(), s.end()), {.color = {100, 200, 100}});
                     }
                     {
                         auto s = std::format("Time on floor: {:.1f}s", time_);
                         p.text(std::u8string(s.begin(), s.end()), {.color = {160, 220, 160}});
                     }
                     p.text(u8"[Esc] Pause overlay    [N] Next floor    [T] Back to title",
                            {.color = {160, 160, 200}});
                 });
        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 20}}, [](auto& p) {
            p.text(u8"ex07 \u2014 Scene Manager  |  [Esc] Quit from pause",
                   {.color = {200, 200, 200}});
        });
        xebble::debug_overlay(world, renderer);
    }
};

// ---------------------------------------------------------------------------
// Pause overlay (pushed on top of Gameplay; DrawBelow::Yes)
// ---------------------------------------------------------------------------
class PauseSystem : public xebble::System {
public:
    void update(xebble::World& world, float) override {
        for (const auto& ev : world.resource<xebble::EventQueue>().events) {
            if (ev.type != xebble::EventType::KeyPress)
                continue;
            auto k = ev.key().key;
            if (k == xebble::Key::Escape || k == xebble::Key::P)
                // Pop back to gameplay.
                world.resource<xebble::SceneTransition>() = xebble::SceneTransition::pop();
            if (k == xebble::Key::Q)
                std::exit(0);
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ui = world.resource<xebble::UIContext>();
        // Semi-transparent overlay panel.
        ui.panel("pause", {.anchor = xebble::Anchor::Center, .size = {300, 130}}, [](auto& p) {
            p.text(u8"-- PAUSED --", {.color = {255, 220, 80}});
            p.text(u8"[Esc/P] Resume", {.color = {180, 220, 180}});
            p.text(u8"[Q] Quit to desktop", {.color = {220, 100, 100}});
        });
        xebble::debug_overlay(world, renderer);
    }
};

} // namespace

int main() {
    xebble::SceneRouter router;

    router.add_scene("title", [](std::any) {
        xebble::World w;
        w.add_system<TitleSystem>();
        return w;
    });

    router.add_scene("gameplay", [](std::any payload) {
        xebble::World w;
        // Unpack the payload into its concrete type so world.resource<FloorNumber>()
        // works — storing it as std::any causes a double-wrap that any_cast rejects.
        if (payload.has_value())
            w.add_resource<FloorNumber>(std::any_cast<FloorNumber>(payload));
        w.add_system<GameplaySystem>();
        return w;
    });

    router.add_scene("pause", [](std::any) {
        xebble::World w;
        w.add_system<PauseSystem>();
        return w;
    });

    router.set_initial("title");

    return xebble::run(
        std::move(router),
        {
            .window = {.title = "ex07 — Scene Manager", .width = 1280, .height = 720},
            .renderer = {.virtual_width = 640, .virtual_height = 360},
        });
}
