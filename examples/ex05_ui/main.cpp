/// @file main.cpp  (ex05_ui)
/// @brief Immediate-mode UI — every widget type.
///
/// Demonstrates:
///   - All widget types: text, button, checkbox, list, text_input,
///     progress_bar, separator, message_log, horizontal
///   - Button pressed state (hold mouse down to see)
///   - Panel borders on all controls
///   - Duplicate button labels across panels (ID deduplication)

#include <xebble/embedded_fonts.hpp>
#include <xebble/xebble.hpp>

#include <array>
#include <format>
#include <string>
#include <vector>

namespace {

struct UIState {
    // Checkbox demo.
    bool show_fps = false;
    bool fullscreen = false;
    bool vsync = true;

    // Radio button demo.
    int difficulty = 1; // 0=Easy, 1=Normal, 2=Hard

    // Slider demo.
    float volume = 0.75f;
    float brightness = 0.5f;

    // List demo.
    int selected_item = 0;

    // Text input demo.
    std::u8string input_text;
    std::u8string last_submit;

    // Button counter.
    int clicks = 0;

    // Progress bar demo.
    float hp = 75.0f;
    float hp_max = 100.0f;
    float xp = 230.0f;
    float xp_max = 500.0f;

    // Message log demo.
    xebble::MessageLog log{64};
    bool log_seeded = false;
    xebble::Rng rng{42};

    // Modal demo.
    bool show_modal = false;
};

struct LogMessage {
    std::u8string text;
    xebble::LogColor color;
};

// clang-format off
const std::array<LogMessage, 15> RANDOM_MESSAGES = {{
    {u8"A rat scurries past your feet.",          {180, 180, 180}},
    {u8"You hear dripping water echoing.",        {140, 180, 200}},
    {u8"A skeleton lunges from the shadows!",     {255, 100, 100}},
    {u8"You find a health potion. +20 HP",        {100, 255, 100}},
    {u8"The torch flickers ominously.",           {255, 200, 100}},
    {u8"You disarm a floor trap.",                {200, 200, 255}},
    {u8"An arrow flies past your head!",          {255, 130, 80}},
    {u8"You discover a hidden passage.",          {180, 255, 180}},
    {u8"The ground trembles beneath you.",        {200, 160, 120}},
    {u8"A gelatinous cube blocks the corridor.",  {100, 220, 200}},
    {u8"You found 25 gold!",                      {255, 215, 0}},
    {u8"The air grows cold. Something is near.",  {160, 160, 255}},
    {u8"You step on a mimic. It bites! -8 HP",   {255, 80, 80}},
    {u8"A healing spring! HP fully restored.",    {80, 255, 120}},
    {u8"You levelled up!",                        {255, 255, 100}},
}};
// clang-format on

const int NUM_RANDOM_MESSAGES = static_cast<int>(std::size(RANDOM_MESSAGES));

class UISystem : public xebble::System {
    std::vector<std::u8string> ITEMS = {
        u8"Longsword", u8"Leather Armour", u8"Health Potion", u8"Scroll of Fire", u8"Iron Shield",
        u8"Torch",     u8"Rope (50 ft)",   u8"Lockpick Set",  u8"Healing Salve",  u8"Rations (5)",
    };

public:
    void init(xebble::World& world) override {
        world.add_resource(UIState{});
        xebble::embedded_fonts::use_berkelium64(world);
    }

    void update(xebble::World& world, float /*dt*/) override {
        for (const auto& e : world.resource<xebble::EventQueue>().events) {
            if (e.type == xebble::EventType::KeyPress && e.key().key == xebble::Key::Escape) {
                std::exit(0);
            }
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ui = world.resource<xebble::UIContext>();
        auto& state = world.resource<UIState>();

        // Seed the message log once.
        if (!state.log_seeded) {
            state.log.push(u8"Welcome to the dungeon.", {180, 180, 255});
            state.log.push(u8"You see a goblin.", {200, 200, 200});
            state.log.push(u8"The goblin attacks! -5 HP", {255, 100, 100});
            state.log.push(u8"You swing your sword.", {200, 200, 200});
            state.log.push(u8"Critical hit! 18 damage.", {255, 220, 80});
            state.log.push(u8"The goblin is defeated.", {100, 255, 100});
            state.log.push(u8"You found 12 gold.", {255, 215, 0});
            state.log_seeded = true;
        }

        // Layout constants.  Virtual screen is 960 x 540.
        //
        //   8px margin from edges, 8px gap between columns/rows.
        //
        //   Left column:  8..474  (466 px wide)
        //   Right column: 486..952 (466 px wide)
        //
        //   Top row:      8..288  (280 px tall)
        //   Bottom row:   296..532 (236 px tall)
        //
        constexpr float COL_W = 466.0f;
        constexpr float TOP_H = 280.0f;
        constexpr float BOT_H = 236.0f;
        constexpr float MARGIN = 8.0f;
        constexpr float GAP = 8.0f;

        // --- Top-left: Controls (buttons, checkboxes, progress bars) ---
        ui.panel("controls",
                 {
                     .anchor = xebble::Anchor::TopLeft,
                     .size = {COL_W, TOP_H},
                     .offset = {MARGIN, MARGIN},
                 },
                 [&](auto& p) {
                     p.text(u8"Controls", {.color = {255, 220, 80}});
                     p.separator();

                     // Buttons.
                     p.horizontal([&](auto& row) {
                         if (row.button(u8"Click Me")) {
                             ++state.clicks;
                         }
                         if (row.button(u8"Reset")) {
                             state.clicks = 0;
                         }
                     });
                     {
                         auto s = std::format("Clicks: {}", state.clicks);
                         p.text(std::u8string(s.begin(), s.end()), {.color = {180, 180, 180}});
                     }

                     p.separator();

                     // Checkboxes.
                     p.checkbox(u8"Show FPS", state.show_fps);
                     p.checkbox(u8"Fullscreen", state.fullscreen);
                     p.checkbox(u8"VSync", state.vsync);

                     p.separator();

                     // Progress bars.
                     p.progress_bar(state.hp, state.hp_max,
                                    {.fill_color = {180, 40, 40, 255}, .show_text = true});
                     p.progress_bar(state.xp, state.xp_max,
                                    {.fill_color = {60, 60, 200, 255}, .show_text = true});
                     p.horizontal([&](auto& row) {
                         if (row.button(u8"Damage")) {
                             state.hp = std::max(0.0f, state.hp - 10.0f);
                         }
                         if (row.button(u8"Heal")) {
                             state.hp = std::min(state.hp_max, state.hp + 10.0f);
                         }
                         if (row.button(u8"+XP")) {
                             state.xp = std::min(state.xp_max, state.xp + 50.0f);
                         }
                     });
                 });

        // --- Top-right: Inventory (list + duplicate-ID demo) ---
        ui.panel("inventory",
                 {
                     .anchor = xebble::Anchor::TopLeft,
                     .size = {COL_W, TOP_H},
                     .offset = {MARGIN + COL_W + GAP, MARGIN},
                 },
                 [&](auto& p) {
                     p.text(u8"Inventory", {.color = {255, 220, 80}});
                     p.separator();
                     p.list("inv_list", ITEMS, state.selected_item, {.visible_rows = 6});
                     p.text(ITEMS[static_cast<size_t>(state.selected_item)],
                            {.color = {180, 255, 180}});
                     p.separator();
                     // "Ok" button — same label as in the log panel below.
                     if (p.button(u8"Ok")) {
                         const auto& name = ITEMS[static_cast<size_t>(state.selected_item)];
                         auto s = std::format("Equipped: ");
                         std::u8string msg(s.begin(), s.end());
                         msg += name;
                         state.log.push(msg, {100, 200, 255});
                     }
                 });

        // --- Bottom-left: Text input, radio buttons, sliders, tooltips ---
        ui.panel("input",
                 {
                     .anchor = xebble::Anchor::TopLeft,
                     .size = {COL_W, BOT_H},
                     .offset = {MARGIN, MARGIN + TOP_H + GAP},
                 },
                 [&](auto& p) {
                     p.text(u8"Text Input", {.color = {255, 220, 80}});
                     if (p.text_input("name", state.input_text)) {
                         state.last_submit = state.input_text;
                     }
                     if (!state.last_submit.empty()) {
                         p.text(state.last_submit, {.color = {180, 255, 180}});
                     }

                     p.separator();

                     // Radio buttons + sliders side by side conceptually.
                     p.text(u8"Difficulty:", {.color = {200, 200, 255}});
                     p.horizontal([&](auto& row) {
                         row.radio_button(u8"Easy", state.difficulty, 0);
                         row.radio_button(u8"Normal", state.difficulty, 1);
                         row.radio_button(u8"Hard", state.difficulty, 2);
                     });

                     p.slider("vol", u8"Volume", state.volume, 0.0f, 1.0f);
                     p.tooltip(u8"Adjust the master volume.");

                     p.separator();

                     if (p.button(u8"Confirm Quit")) {
                         state.show_modal = true;
                     }
                     p.tooltip(u8"Opens a confirmation dialog.");
                 });

        // --- Bottom-right: Combat log (message_log + duplicate-ID demo) ---
        ui.panel("log",
                 {
                     .anchor = xebble::Anchor::TopLeft,
                     .size = {COL_W, BOT_H},
                     .offset = {MARGIN + COL_W + GAP, MARGIN + TOP_H + GAP},
                 },
                 [&](auto& p) {
                     p.text(u8"Combat Log", {.color = {255, 220, 80}});
                     p.separator();
                     p.message_log("combat_log", state.log, {.visible_rows = 6});
                     p.separator();
                     // "Ok" button — same label as in the inventory panel.
                     // Gets a different widget ID because different panel.
                     p.horizontal([&](auto& row) {
                         if (row.button(u8"Random Event")) {
                             auto idx = state.rng.range(NUM_RANDOM_MESSAGES - 1);
                             const auto& msg = RANDOM_MESSAGES[idx];
                             state.log.push(msg.text, msg.color);
                         }
                         if (row.button(u8"Ok")) {
                             state.log.push(u8"You press onward\u2026", {200, 200, 200});
                         }
                     });
                 });

        // --- Modal dialog (shown when Confirm Quit is pressed) ---
        if (state.show_modal) {
            ui.modal("quit_dlg", {280, 100}, [&](auto& p) {
                p.text(u8"Are you sure you want to quit?");
                p.separator();
                p.horizontal([&](auto& row) {
                    if (row.button(u8"Yes")) {
                        std::exit(0);
                    }
                    if (row.button(u8"No")) {
                        state.show_modal = false;
                    }
                });
            });
        }

        xebble::debug_overlay(world, renderer);
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<UISystem>();

    return xebble::run(std::move(world),
                       {
                           .window = {.title = "ex05 \u2014 UI", .width = 1280, .height = 720},
                           .renderer = {.virtual_width = 960, .virtual_height = 540},
                       });
}
