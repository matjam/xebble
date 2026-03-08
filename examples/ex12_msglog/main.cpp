/// @file main.cpp  (ex12_msglog)
/// @brief Message log: push, deduplication, filtering, scrollback.
///
/// Demonstrates:
///   - `MessageLog` construction with a capacity
///   - `log.push(text, color, category)` — color-coded, categorized messages
///   - Automatic deduplication (repeated messages collapse to "text ×N")
///   - `log.visible(n)` — last N messages for HUD display
///   - `log.filtered(category, n)` — category-filtered view
///   - `log.newest()` / `log.oldest()` / `log[i]` random access

#include <xebble/xebble.hpp>

#include <format>
#include <string>
#include <vector>

namespace {

struct LogState {
    xebble::MessageLog log{50};
    float  auto_timer   = 0.0f;
    bool   auto_spam    = false;
    int    spam_counter = 0;
    std::string filter_cat;   // empty = show all
};

class MsgLogSystem : public xebble::System {
public:
    void init(xebble::World& world) override {
        LogState ls;
        // Pre-populate with some messages.
        ls.log.push(u8"Welcome to the dungeon.", {200, 200, 200}, "story");
        ls.log.push(u8"You find a rusty sword.", {200, 180, 100}, "loot");
        ls.log.push(u8"A goblin appears!",       {220, 100, 100}, "combat");
        ls.log.push(u8"You attack the goblin for 4 damage.", {200, 200, 200}, "combat");
        ls.log.push(u8"The goblin hits you for 2 damage.",   {220, 80,  80}, "combat");
        ls.log.push(u8"You attack the goblin for 4 damage.", {200, 200, 200}, "combat");
        ls.log.push(u8"The goblin dies!",                    {100, 220, 100}, "combat");
        ls.log.push(u8"You find a gold coin.",               {220, 200, 80}, "loot");
        world.add_resource(std::move(ls));
    }

    void update(xebble::World& world, float dt) override {
        auto& ls = world.resource<LogState>();

        for (const auto& ev : world.resource<xebble::EventQueue>().events) {
            if (ev.type != xebble::EventType::KeyPress) continue;
            auto k = ev.key().key;
            if (k == xebble::Key::Escape) std::exit(0);

            // Push various message types.
            if (k == xebble::Key::C)
                ls.log.push(u8"You strike the enemy for 7 damage.",
                            {220, 200, 200}, "combat");
            if (k == xebble::Key::L)
                ls.log.push(u8"You find a healing potion.",
                            {100, 220, 100}, "loot");
            if (k == xebble::Key::S)
                ls.log.push(u8"You descend deeper into the dungeon.",
                            {180, 140, 255}, "story");
            // Pressing D repeatedly demonstrates deduplication.
            if (k == xebble::Key::D)
                ls.log.push(u8"You miss!", {200, 180, 180}, "combat");
            // Clear.
            if (k == xebble::Key::X)
                ls.log.clear();
            // Auto-spam toggle.
            if (k == xebble::Key::A)
                ls.auto_spam = !ls.auto_spam;
            // Filter toggles.
            if (k == xebble::Key::F1) ls.filter_cat = "";
            if (k == xebble::Key::F2) ls.filter_cat = "combat";
            if (k == xebble::Key::F3) ls.filter_cat = "loot";
            if (k == xebble::Key::F4) ls.filter_cat = "story";
        }

        // Auto-spam: push a message every 0.4 s.
        if (ls.auto_spam) {
            ls.auto_timer += dt;
            if (ls.auto_timer >= 0.4f) {
                ls.auto_timer = 0.0f;
                ++ls.spam_counter;
                const std::u8string msgs[] = {
                    u8"You miss!", u8"Critical hit!", u8"The orc growls.", u8"Gold clatters."
                };
                const char* cats[] = {"combat", "combat", "story", "loot"};
                xebble::LogColor cols[] = {
                    {200, 180, 180}, {255, 220, 50}, {180, 180, 220}, {220, 200, 80}
                };
                int i = ls.spam_counter % 4;
                ls.log.push(msgs[i], cols[i], cats[i]);
            }
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ls = world.resource<LogState>();
        auto& ui = world.resource<xebble::UIContext>();

        // Main log panel.
        ui.panel("log", {.anchor = xebble::Anchor::Left,
                         .size   = {300, 260},
                         .offset = {10, 0}},
            [&](auto& p) {
                p.text(u8"Message Log", {.color = {220, 220, 100}});
                const char* filter_label = ls.filter_cat.empty() ? "all"
                                         : ls.filter_cat.c_str();
                { auto s = std::format("Filter: {:s}  |  {:d}/{:d} msgs",
                                   filter_label,
                                   (int)ls.log.size(), (int)ls.log.capacity());
                  p.text(std::u8string(s.begin(), s.end()), {.color = {160, 160, 180}}); }

                // Choose messages to display.
                std::vector<const xebble::LogMessage*> msgs;
                if (ls.filter_cat.empty())
                    msgs = ls.log.visible(10);
                else
                    msgs = ls.log.filtered(ls.filter_cat, 10);

                for (auto* m : msgs) {
                    xebble::Color c{m->color.r, m->color.g, m->color.b, m->color.a};
                    p.text(m->text, {.color = c});
                }
            });

        // Controls panel.
        ui.panel("controls", {.anchor = xebble::Anchor::Right,
                              .size   = {280, 220},
                              .offset = {-10, 0}},
            [&](auto& p) {
                p.text(u8"Controls", {.color = {220, 220, 100}});
                p.text(u8"[C] Combat msg    [L] Loot msg",    {.color = {180, 200, 180}});
                p.text(u8"[S] Story msg     [D] Repeat (\u00D7N)", {.color = {180, 200, 180}});
                p.text(u8"[A] Auto-spam     [X] Clear log",   {.color = {180, 200, 180}});
                p.text(u8"[F1] All  [F2] Combat  [F3] Loot",  {.color = {180, 180, 220}});
                p.text(u8"[F4] Story",                         {.color = {180, 180, 220}});
                p.text(u8"[Esc] Quit",                         {.color = {160, 160, 160}});
                if (!ls.log.empty()) {
                    auto& n = ls.log.newest();
                    xebble::Color nc{n.color.r, n.color.g, n.color.b, n.color.a};
                    std::u8string newest_label = u8"Newest: ";
                    newest_label += n.text;
                    p.text(newest_label, {.color = nc});
                }
            });

        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 20}},
            [&](auto& p) {
                { auto s = std::format("ex12 \u2014 MessageLog  |  {:s}  |  [Esc] Quit",
                                   ls.auto_spam ? "AUTO-SPAM ON" : "auto-spam off");
                  p.text(std::u8string(s.begin(), s.end()), {.color = {200, 200, 200}}); }
            });
        xebble::debug_overlay(world, renderer);
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<MsgLogSystem>();

    return xebble::run(std::move(world), {
        .window   = {.title = "ex12 — MessageLog", .width = 1280, .height = 720},
        .renderer = {.virtual_width = 640, .virtual_height = 360},
    });
}
