/// @file main.cpp  (ex05_ui)
/// @brief Immediate-mode UI — every widget type.
///
/// Demonstrates:
///   - UIContext::panel() with different Anchor positions
///   - text(), button(), checkbox(), list(), text_input()
///   - PanelBuilder::horizontal() row layout
///   - Multiple panels on screen simultaneously
///   - UITheme customisation

#include <xebble/xebble.hpp>
#include <xebble/embedded_fonts.hpp>

#include <format>
#include <string>
#include <vector>

namespace {

struct UIState {
    // Checkbox demo.
    bool show_stats  = true;
    bool fullscreen  = false;
    bool vsync       = true;

    // List demo.
    int  selected_item = 0;

    // Text input demo.
    std::u8string input_text  = u8"Type here\u2026";
    std::u8string last_submit;

    // Button counter.
    int button_clicks = 0;
};

class UISystem : public xebble::System {
    const std::vector<std::u8string> LIST_ITEMS = {
        u8"Option Alpha", u8"Option Beta", u8"Option Gamma",
        u8"Option Delta", u8"Option Epsilon", u8"Option Zeta",
        u8"Option Eta",   u8"Option Theta",
    };

public:
    void init(xebble::World& world) override {
        world.add_resource(UIState{});
        xebble::embedded_fonts::use_berkelium64(world);
    }

    void update(xebble::World& world, float) override {
        for (const auto& e : world.resource<xebble::EventQueue>().events) {
            if (e.type == xebble::EventType::KeyPress &&
                e.key().key == xebble::Key::Escape) std::exit(0);
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ui    = world.resource<xebble::UIContext>();
        auto& state = world.resource<UIState>();

        // --- Left panel: buttons + checkboxes ---
        ui.panel("left", {
                     .anchor = xebble::Anchor::Left,
                     .size   = {200, 1.0f},
                     .offset = {4, 0},
                 },
                 [&](auto& p) {
                     p.text(u8"Controls", {.color = {255, 220, 80}});

                     if (p.button(u8"Click Me"))
                         ++state.button_clicks;

                     { auto s = std::format("Clicked: {}", state.button_clicks);
                       p.text(std::u8string(s.begin(), s.end()), {.color = {180, 180, 180}}); }

                     p.text(u8""); // spacer
                     p.text(u8"Settings:", {.color = {200, 200, 255}});
                     p.checkbox(u8"Show Stats",  state.show_stats);
                     p.checkbox(u8"Fullscreen",  state.fullscreen);
                     p.checkbox(u8"VSync",       state.vsync);

                     p.text(u8""); // spacer
                     p.text(u8"[Esc] Quit", {.color = {150, 150, 150}});
                 });

        // --- Centre panel: list widget ---
        ui.panel("list_panel", {
                     .anchor = xebble::Anchor::Center,
                     .size   = {220, 220},
                 },
                 [&](auto& p) {
                     p.text(u8"Select an option:", {.color = {255, 220, 80}});
                     p.list("my_list", LIST_ITEMS, state.selected_item,
                            {.visible_rows = 6});
                     p.text(LIST_ITEMS[static_cast<size_t>(state.selected_item)],
                            {.color = {180, 255, 180}});
                 });

        // --- Right panel: text input ---
        ui.panel("right", {
                     .anchor = xebble::Anchor::Right,
                     .size   = {200, 1.0f},
                     .offset = {-4, 0},
                 },
                 [&](auto& p) {
                     p.text(u8"Text Input", {.color = {255, 220, 80}});
                     p.text(u8"Press Enter to submit:");
                     if (p.text_input("input_box", state.input_text))
                         state.last_submit = state.input_text;
                     if (!state.last_submit.empty())
                         p.text(state.last_submit, {.color = {180, 255, 180}});

                     p.text(u8""); // spacer
                     p.text(u8"Horizontal layout:", {.color = {200, 200, 255}});
                      p.horizontal([&](auto& row) {
                          if (row.button(u8"A")) state.button_clicks += 10;
                          if (row.button(u8"B")) state.button_clicks += 100;
                          if (row.button(u8"C")) state.button_clicks -= 5;
                      });
                 });

        // --- Bottom stats panel (conditionally shown) ---
        if (state.show_stats) {
            ui.panel("stats", {
                         .anchor = xebble::Anchor::Bottom,
                         .size   = {1.0f, 40},
                     },
                     [&](auto& p) {
                         { auto s = std::format("ex05 \u2014 UI Demo  |  Clicks: {}  VSync: {}  Fullscreen: {}",
                                            state.button_clicks,
                                            state.vsync ? "on" : "off",
                                            state.fullscreen ? "yes" : "no");
                           p.text(std::u8string(s.begin(), s.end()), {.color = {220, 220, 220}}); }
                         p.text(u8"All widgets: text / button / checkbox / list / text_input / horizontal",
                                {.color = {160, 160, 160}});
                     });
        }
        xebble::debug_overlay(world, renderer);
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<UISystem>();

    return xebble::run(std::move(world), {
        .window   = {.title = "ex05 \u2014 UI", .width = 1280, .height = 720},
        .renderer = {.virtual_width = 640, .virtual_height = 360},
    });
}
