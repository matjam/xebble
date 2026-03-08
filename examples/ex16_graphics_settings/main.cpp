/// @file main.cpp  (ex16_graphics_settings)
/// @brief Typical in-game "Graphics Settings" screen.
///
/// Demonstrates:
///   - Enumerating native display modes via Window::available_display_modes()
///   - Generating pixel-perfect (integer-scaled) virtual resolutions from the
///     native display size (1/2, 1/3, 1/4 divisors labelled "pixel perfect")
///   - Fullscreen and VSync checkboxes
///   - Applying settings (virtual resolution + display mode) via Renderer
///   - Reverting to previous settings on Cancel

#include <xebble/embedded_fonts.hpp>
#include <xebble/xebble.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Resolution entry shown in the list
// ---------------------------------------------------------------------------

struct ResolutionEntry {
    uint32_t width;
    uint32_t height;
    std::u8string label; // e.g. u8"960x540  (pixel perfect x2)"
};

// ---------------------------------------------------------------------------
// Build the resolution list from native display modes.
//
// Strategy:
//   1. Collect unique (w, h) pairs from Window::available_display_modes().
//   2. For each unique native resolution, add sub-entries for every integer
//      divisor d in {1,2,3,4} that still gives a res >= 320x180, labelled
//      "pixel perfect xd" (d=1 is the native resolution itself).
//   3. Deduplicate and sort by area descending.
// ---------------------------------------------------------------------------

std::vector<ResolutionEntry> build_resolution_list() {
    auto modes = xebble::Window::available_display_modes();

    // Collect unique native (w, h) pairs (modes may repeat resolutions at
    // different refresh rates).
    std::vector<std::pair<uint32_t, uint32_t>> natives;
    for (const auto& m : modes) {
        auto entry = std::make_pair(m.pixel_width, m.pixel_height);
        if (std::ranges::find(natives, entry) == natives.end()) {
            natives.push_back(entry);
        }
    }

    std::vector<ResolutionEntry> entries;

    for (const auto& [nw, nh] : natives) {
        // Divisor 1 = native resolution itself ("native").
        // Divisors 2..4 = pixel-perfect sub-resolutions.
        for (uint32_t d = 1; d <= 4; ++d) {
            const uint32_t w = nw / d;
            const uint32_t h = nh / d;
            if (w < 320 || h < 180) {
                break; // too small — stop increasing divisor
            }

            // Build a UTF-8 label.  std::format uses char; convert to u8string.
            std::string s;
            if (d == 1) {
                s = std::format("{}x{}  (native)", w, h);
            } else {
                s = std::format("{}x{}  (pixel perfect x{})", w, h, d);
            }

            // Deduplicate: skip if this (w,h) is already in the list.
            bool dup = false;
            for (const auto& e : entries) {
                if (e.width == w && e.height == h) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                entries.push_back({w, h, std::u8string(s.begin(), s.end())});
            }
        }
    }

    // Sort largest area first.
    std::ranges::sort(entries, [](const ResolutionEntry& a, const ResolutionEntry& b) {
        return (a.width * a.height) > (b.width * b.height);
    });

    return entries;
}

// ---------------------------------------------------------------------------
// Persistent state
// ---------------------------------------------------------------------------

struct GraphicsState {
    // Current (applied) settings.
    int applied_res_index = 0;
    bool applied_fullscreen = false;
    bool applied_vsync = true;

    // Pending (UI-selected, not yet applied) settings.
    int pending_res_index = 0;
    bool pending_fullscreen = false;
    bool pending_vsync = true;

    // Resolution list (built once in init).
    std::vector<ResolutionEntry> resolutions;
    // Labels extracted from resolutions for the list widget.
    std::vector<std::u8string> res_labels;

    // Feedback message shown after Apply.
    std::u8string status_msg;
};

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

class GraphicsSettingsSystem : public xebble::System {
public:
    void init(xebble::World& world) override {
        xebble::embedded_fonts::use_berkelium64(world);
        world.add_resource(GraphicsState{});

        auto& state = world.resource<GraphicsState>();
        state.resolutions = build_resolution_list();

        // Extract labels for the list widget.
        for (const auto& e : state.resolutions) {
            state.res_labels.push_back(e.label);
        }

        // Find the index that matches the current renderer virtual resolution.
        auto* renderer = world.resource<xebble::Renderer*>();
        const uint32_t cur_w = renderer->virtual_width();
        const uint32_t cur_h = renderer->virtual_height();
        const auto n = static_cast<int>(state.resolutions.size());
        for (int i = 0; i < n; ++i) {
            const auto& res = state.resolutions[static_cast<size_t>(i)];
            if (res.width == cur_w && res.height == cur_h) {
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
        // Title bar (top strip)
        // ---------------------------------------------------------------
        ui.panel("title",
                 {
                     .anchor = xebble::Anchor::Top,
                     .size = {1.0f, 36},
                 },
                 [&](auto& p) {
                     p.text(u8"Graphics Settings", {.color = {255, 220, 80}});
                     p.text(u8"[Esc] Quit", {.color = {160, 160, 160}});
                 });

        // ---------------------------------------------------------------
        // Left column: resolution list
        // ---------------------------------------------------------------
        ui.panel("res_panel",
                 {
                     .anchor = xebble::Anchor::Left,
                     .size = {320, 1.0f},
                     .offset = {4, 40},
                 },
                 [&](auto& p) {
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
                     .size = {240, 1.0f},
                     .offset = {-4, 40},
                 },
                 [&](auto& p) {
                     p.text(u8"Options", {.color = {200, 200, 255}});
                     p.text(u8""); // spacer

                     p.checkbox(u8"Fullscreen", state.pending_fullscreen);
                     p.checkbox(u8"VSync", state.pending_vsync);

                     p.text(u8""); // spacer

                     // Show the pending selection label for clarity.
                     if (!state.resolutions.empty()) {
                         const auto& sel =
                             state.resolutions[static_cast<size_t>(state.pending_res_index)];
                         const auto dim = std::format("{}x{}", sel.width, sel.height);
                         p.text(u8"Selected resolution:", {.color = {160, 160, 160}});
                         p.text(std::u8string(dim.begin(), dim.end()), {.color = {220, 220, 220}});
                         p.text(sel.label, {.color = {140, 200, 140}});
                     }

                     p.text(u8""); // spacer

                     // Apply button — commit pending settings.
                     if (p.button(u8"Apply")) {
                         apply_settings(state, renderer);
                     }

                     // Revert button — discard pending changes.
                     if (p.button(u8"Revert")) {
                         state.pending_res_index = state.applied_res_index;
                         state.pending_fullscreen = state.applied_fullscreen;
                         state.pending_vsync = state.applied_vsync;
                         state.status_msg = u8"Reverted to applied settings.";
                     }

                     // Status message.
                     if (!state.status_msg.empty()) {
                         p.text(u8"");
                         p.text(state.status_msg, {.color = {140, 220, 140}});
                     }
                 });

        // ---------------------------------------------------------------
        // Bottom status bar
        // ---------------------------------------------------------------
        ui.panel("status_bar",
                 {
                     .anchor = xebble::Anchor::Bottom,
                     .size = {1.0f, 36},
                 },
                 [&](auto& p) {
                     const auto cur =
                         std::format("Virtual: {}x{}  |  Fullscreen: {}  |  VSync: {}",
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

        // Apply virtual resolution change.
        renderer.set_virtual_resolution(sel.width, sel.height);

        // Record what was applied.
        state.applied_res_index = state.pending_res_index;
        state.applied_fullscreen = state.pending_fullscreen;
        state.applied_vsync = state.pending_vsync;

        const auto msg = std::format("Applied: {}x{}  fullscreen={}  vsync={}", sel.width,
                                     sel.height, state.applied_fullscreen ? "on" : "off",
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
