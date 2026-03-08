/// @file main.cpp  (ex16_graphics_settings)
/// @brief Typical in-game "Graphics Settings" screen.
///
/// Demonstrates Window::monitors() and Window::available_resolutions() — the
/// library API that returns a sorted, annotated list of candidate virtual
/// resolutions for a settings menu, scoped to a specific physical monitor so
/// that pixel-perfect detection is always accurate for the display the player
/// will actually see the game on.

#include <xebble/embedded_fonts.hpp>
#include <xebble/xebble.hpp>

#include <format>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Persistent state
// ---------------------------------------------------------------------------

struct GraphicsState {
    // Monitor list (built once in init).
    std::vector<xebble::MonitorInfo> monitors;
    std::vector<std::u8string> monitor_labels;
    int selected_monitor_index = 0;

    // Current (applied) settings.
    int applied_res_index = 0;
    bool applied_fullscreen = false;
    bool applied_vsync = true;

    // Pending (UI-selected, not yet applied) settings.
    int pending_res_index = 0;
    bool pending_fullscreen = false;
    bool pending_vsync = true;

    // Resolution list for the currently selected monitor.
    std::vector<xebble::ResolutionInfo> resolutions;
    std::vector<std::u8string> res_labels;

    // Feedback message shown after Apply / Revert.
    std::u8string status_msg;
};

// Rebuild the resolution list for the monitor at `index`.
void rebuild_resolutions(GraphicsState& state, int index) {
    state.selected_monitor_index = index;
    state.resolutions.clear();
    state.res_labels.clear();
    state.pending_res_index = 0;

    if (index < 0 || static_cast<size_t>(index) >= state.monitors.size()) {
        return;
    }

    state.resolutions = xebble::Window::available_resolutions(
        state.monitors[static_cast<size_t>(index)], xebble::ScaleMode::Fit);

    for (const auto& r : state.resolutions) {
        state.res_labels.emplace_back(r.label.begin(), r.label.end());
    }
}

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

class GraphicsSettingsSystem : public xebble::System {
public:
    void init(xebble::World& world) override {
        xebble::embedded_fonts::use_berkelium64(world);
        world.add_resource(GraphicsState{});

        auto& state = world.resource<GraphicsState>();

        // Enumerate monitors.
        state.monitors = xebble::Window::monitors();
        for (const auto& m : state.monitors) {
            state.monitor_labels.emplace_back(m.label.begin(), m.label.end());
        }

        // Default to primary monitor (index 0 — monitors() puts primary first).
        rebuild_resolutions(state, 0);

        // Pre-select the entry matching the current virtual resolution.
        auto* renderer = world.resource<xebble::Renderer*>();
        const uint32_t cur_w = renderer->virtual_width();
        const uint32_t cur_h = renderer->virtual_height();
        const auto n = static_cast<int>(state.resolutions.size());
        for (int i = 0; i < n; ++i) {
            const auto& r = state.resolutions[static_cast<size_t>(i)];
            if (r.width == cur_w && r.height == cur_h) {
                state.applied_res_index = i;
                state.pending_res_index = i;
                break;
            }
        }
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
        auto& state = world.resource<GraphicsState>();

        // ---------------------------------------------------------------
        // Title bar
        // ---------------------------------------------------------------
        ui.panel("title", {.anchor = xebble::Anchor::Top, .size = {1.0f, 36}}, [&](auto& p) {
            p.text(u8"Graphics Settings", {.color = {255, 220, 80}});
            p.text(u8"[Esc] Quit", {.color = {160, 160, 160}});
        });

        // ---------------------------------------------------------------
        // Left column: monitor selector + resolution list
        // ---------------------------------------------------------------
        ui.panel("res_panel",
                 {
                     .anchor = xebble::Anchor::Left,
                     .size = {420, 1.0f},
                     .offset = {4, 40},
                 },
                 [&](auto& p) {
                     // Monitor selector
                     p.text(u8"Display", {.color = {200, 200, 255}});
                     if (state.monitor_labels.empty()) {
                         p.text(u8"(no monitors detected)", {.color = {200, 100, 100}});
                     } else {
                         const int prev = state.selected_monitor_index;
                         p.list("mon_list", state.monitor_labels, state.selected_monitor_index,
                                {.visible_rows = static_cast<float>(state.monitor_labels.size())});
                         if (state.selected_monitor_index != prev) {
                             rebuild_resolutions(state, state.selected_monitor_index);
                         }
                     }

                     p.text(u8"");

                     // Resolution list for selected monitor
                     p.text(u8"Resolution", {.color = {200, 200, 255}});
                     if (state.res_labels.empty()) {
                         p.text(u8"(no display modes detected)", {.color = {200, 100, 100}});
                     } else {
                         p.list("res_list", state.res_labels, state.pending_res_index,
                                {.visible_rows = 12});
                     }
                 });

        // ---------------------------------------------------------------
        // Right column: options + buttons
        // ---------------------------------------------------------------
        ui.panel("options_panel",
                 {
                     .anchor = xebble::Anchor::Right,
                     .size = {220, 1.0f},
                     .offset = {-4, 40},
                 },
                 [&](auto& p) {
                     p.text(u8"Options", {.color = {200, 200, 255}});
                     p.text(u8"");

                     p.checkbox(u8"Fullscreen", state.pending_fullscreen);
                     p.checkbox(u8"VSync", state.pending_vsync);

                     p.text(u8"");

                     if (!state.resolutions.empty()) {
                         const auto& sel =
                             state.resolutions[static_cast<size_t>(state.pending_res_index)];

                         const auto dim = std::format("{}x{}", sel.width, sel.height);
                         p.text(u8"Selected:", {.color = {160, 160, 160}});
                         p.text(std::u8string(dim.begin(), dim.end()), {.color = {220, 220, 220}});

                         if (sel.pixel_perfect) {
                             const auto info = std::format("Pixel perfect x{}", sel.scale_factor);
                             p.text(std::u8string(info.begin(), info.end()),
                                    {.color = {140, 255, 140}});
                             p.text(u8"Filter: nearest (crisp)", {.color = {140, 220, 255}});
                         } else {
                             p.text(u8"Filter: bilinear (smooth)", {.color = {180, 180, 180}});
                         }
                     }

                     p.text(u8"");

                     if (p.button(u8"Apply")) {
                         apply_settings(state, renderer);
                     }
                     if (p.button(u8"Revert")) {
                         state.pending_res_index = state.applied_res_index;
                         state.pending_fullscreen = state.applied_fullscreen;
                         state.pending_vsync = state.applied_vsync;
                         state.status_msg = u8"Reverted to applied settings.";
                     }

                     if (!state.status_msg.empty()) {
                         p.text(u8"");
                         p.text(state.status_msg, {.color = {140, 220, 140}});
                     }
                 });

        // ---------------------------------------------------------------
        // Bottom status bar
        // ---------------------------------------------------------------
        ui.panel(
            "status_bar", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 36}}, [&](auto& p) {
                const auto cur = std::format("Virtual: {}x{}  |  Fullscreen: {}  |  VSync: {}",
                                             renderer.virtual_width(), renderer.virtual_height(),
                                             state.applied_fullscreen ? "on" : "off",
                                             state.applied_vsync ? "on" : "off");
                p.text(std::u8string(cur.begin(), cur.end()), {.color = {220, 220, 220}});
                p.text(u8"ex16 \u2014 Graphics Settings  |  [Esc] Quit",
                       {.color = {150, 150, 150}});
            });

        xebble::debug_overlay(world, renderer);
    }

private:
    static void apply_settings(GraphicsState& state, xebble::Renderer& renderer) {
        if (state.resolutions.empty()) {
            return;
        }
        const auto& sel = state.resolutions[static_cast<size_t>(state.pending_res_index)];

        renderer.set_nearest_sample(sel.pixel_perfect);
        renderer.set_virtual_resolution(sel.width, sel.height);
        renderer.set_fullscreen(state.pending_fullscreen);

        state.applied_res_index = state.pending_res_index;
        state.applied_fullscreen = state.pending_fullscreen;
        state.applied_vsync = state.pending_vsync;

        const auto msg = std::format(
            "Applied: {}x{}  filter={}  fullscreen={}  vsync={}", sel.width, sel.height,
            sel.pixel_perfect ? "nearest" : "bilinear", state.applied_fullscreen ? "on" : "off",
            state.applied_vsync ? "on" : "off");
        state.status_msg = std::u8string(msg.begin(), msg.end());
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<GraphicsSettingsSystem>();

    return xebble::run(
        std::move(world),
        {
            .window = {.title = "ex16 \u2014 Graphics Settings", .width = 1280, .height = 720},
            .renderer = {.virtual_width = 960, .virtual_height = 540},
        });
}
