/// @file main.cpp  (ex16_graphics_settings)
/// @brief Typical in-game "Graphics Settings" screen.
///
/// Demonstrates:
///   - Enumerating native display modes via Window::available_display_modes()
///   - Generating pixel-perfect (integer-scaled) virtual resolutions from the
///     native display size (1/2, 1/3, 1/4 divisors labelled "pixel perfect")
///   - A curated list of common industry-standard resolutions (1080p, 1440p …)
///   - Aspect ratio displayed for the selected resolution
///   - Nearest-neighbour blit filter auto-selected for pixel-perfect modes
///   - Fullscreen and VSync checkboxes wired to Renderer::set_fullscreen()
///   - Applying settings (virtual resolution + display mode) via Renderer
///   - Reverting to previous settings on Cancel

#include <xebble/embedded_fonts.hpp>
#include <xebble/xebble.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <numeric>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Resolution entry shown in the list
// ---------------------------------------------------------------------------

struct ResolutionEntry {
    uint32_t width;
    uint32_t height;
    bool pixel_perfect;         // true for exact integer divisor >= 2 of a native res
    std::u8string label;        // list row text
    std::u8string aspect_ratio; // e.g. u8"16:9"
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Reduce w:h to lowest terms and return as a "W:H" string.
std::u8string aspect_ratio_str(uint32_t w, uint32_t h) {
    const uint32_t g = std::gcd(w, h);
    const auto s = std::format("{}:{}", w / g, h / g);
    return {s.begin(), s.end()};
}

/// Return true if (w, h) is already in entries.
bool already_in(const std::vector<ResolutionEntry>& entries, uint32_t w, uint32_t h) {
    return std::ranges::any_of(
        entries, [w, h](const ResolutionEntry& e) { return e.width == w && e.height == h; });
}

/// Return true if virtual resolution (vw, vh) scales to an exact integer
/// pixel scale on native display (nw, nh) under Fit mode.
///
/// Fit scale = min(nw/vw, nh/vh).  This is integer-pixel-perfect when both
/// nw and nh are exact multiples of vw and vh respectively at that scale —
/// i.e. nw % vw == 0  AND  nh % vh == 0  AND  nw/vw == nh/vh.
///
/// When the ratios differ (bars on one axis), the constraining axis still
/// lands on an integer multiple as long as both axes divide cleanly at the
/// same integer scale.  Example: 1280×720 on 5120×1440 → scale=2 on both
/// axes (5120/1280=4 but constrained by 1440/720=2; 1280×2=2560 ≤ 5120 ✓,
/// 720×2=1440 ≤ 1440 ✓) — pixel-perfect with pillarboxing.
bool is_pixel_perfect(uint32_t vw, uint32_t vh,
                      const std::vector<std::pair<uint32_t, uint32_t>>& natives) {
    return std::ranges::any_of(natives, [vw, vh](const std::pair<uint32_t, uint32_t>& n) {
        const uint32_t nw = n.first;
        const uint32_t nh = n.second;
        if (vw == 0 || vh == 0 || nw < vw || nh < vh) {
            return false;
        }
        // Both axes must divide evenly and yield the same integer scale.
        return (nw % vw == 0) && (nh % vh == 0) && (nw / vw == nh / vh);
    });
}

// ---------------------------------------------------------------------------
// Curated list of common industry-standard resolutions.
// These are added after the pixel-perfect entries so they appear below.
// ---------------------------------------------------------------------------

struct CommonRes {
    uint32_t w;
    uint32_t h;
    const char* name; // short human label, e.g. "1080p"
};

// clang-format off
constexpr auto COMMON_RESOLUTIONS = std::to_array<CommonRes>({
    // 16:9
    {7680, 4320, "8K UHD"},
    {5120, 2880, "5K"},
    {3840, 2160, "4K UHD"},
    {2560, 1440, "1440p QHD"},
    {1920, 1080, "1080p FHD"},
    {1280,  720, "720p HD"},
    {854,   480, "480p"},
    {640,   360, "360p"},
    // 21:9 ultrawide
    {5120, 2160, "5K ultrawide"},
    {3440, 1440, "1440p ultrawide"},
    {2560, 1080, "1080p ultrawide"},
    // 4:3 legacy
    {1600, 1200, "UXGA"},
    {1024,  768, "XGA"},
    {800,   600, "SVGA"},
    {640,   480, "VGA"},
    // 16:10
    {2560, 1600, "WQXGA"},
    {1920, 1200, "WUXGA"},
    {1280,  800, "WXGA"},
});
// clang-format on

// ---------------------------------------------------------------------------
// Build the resolution list.
//
// Phase 1 — pixel-perfect entries:
//   For each unique native (w, h) from available_display_modes(), generate
//   sub-resolutions at integer divisors d = 1..4 where result >= 320x180.
//   d=1 → "native",  d>=2 → "pixel perfect xD".
//
// Phase 2 — common resolutions:
//   Add entries from COMMON_RESOLUTIONS that aren't already in the list,
//   capped at < max_native_area so we don't list resolutions larger than
//   any connected display can show.
//
// Both phases sort their own sub-lists by area descending before merging.
// ---------------------------------------------------------------------------

std::vector<ResolutionEntry> build_resolution_list() {
    auto modes = xebble::Window::available_display_modes();

    // Collect unique native (w, h) pairs.
    std::vector<std::pair<uint32_t, uint32_t>> natives;
    for (const auto& m : modes) {
        auto entry = std::make_pair(m.pixel_width, m.pixel_height);
        if (std::ranges::find(natives, entry) == natives.end()) {
            natives.push_back(entry);
        }
    }

    // Phase 1: pixel-perfect entries.
    // Seed with exact integer-divisor sub-resolutions of each native.
    std::vector<ResolutionEntry> pp_entries;
    for (const auto& [nw, nh] : natives) {
        for (uint32_t d = 1; d <= 8; ++d) {
            const uint32_t w = nw / d;
            const uint32_t h = nh / d;
            // Only exact divisors (no remainder on either axis).
            if (nw % d != 0 || nh % d != 0) {
                continue;
            }
            if (w < 320 || h < 180) {
                break;
            }
            if (already_in(pp_entries, w, h)) {
                continue;
            }

            // A sub-resolution is pixel-perfect if the fit scale is an integer
            // on every axis against at least one native — use is_pixel_perfect()
            // rather than d>=2 so that e.g. a native entry that happens to be
            // an exact integer multiple of another native is also flagged.
            const bool pp = is_pixel_perfect(w, h, natives);
            std::string s;
            if (d == 1) {
                s = std::format("{}x{}  (native)", w, h);
            } else {
                s = std::format("{}x{}  (pixel perfect x{})", w, h, d);
            }
            pp_entries.push_back(
                {w, h, pp, std::u8string(s.begin(), s.end()), aspect_ratio_str(w, h)});
        }
    }
    std::ranges::sort(pp_entries, [](const ResolutionEntry& a, const ResolutionEntry& b) {
        return (a.width * a.height) > (b.width * b.height);
    });

    // Phase 2: common resolutions not already covered.
    // Cap at the largest native area so we don't list resolutions larger than
    // any connected display.
    uint32_t max_area = 0;
    for (const auto& [nw, nh] : natives) {
        max_area = std::max(max_area, nw * nh);
    }

    std::vector<ResolutionEntry> common_entries;
    for (const auto& cr : COMMON_RESOLUTIONS) {
        if (cr.w * cr.h > max_area) {
            continue; // larger than any display
        }
        if (already_in(pp_entries, cr.w, cr.h)) {
            continue; // already listed in phase 1
        }
        if (already_in(common_entries, cr.w, cr.h)) {
            continue;
        }
        // Check if this common resolution happens to be pixel-perfect on any
        // native display (e.g. 1280x720 on a 5120x1440 display: scale=2).
        const bool pp = is_pixel_perfect(cr.w, cr.h, natives);
        std::string s;
        if (pp) {
            s = std::format("{}x{}  ({}, pixel perfect)", cr.w, cr.h, cr.name);
        } else {
            s = std::format("{}x{}  ({})", cr.w, cr.h, cr.name);
        }
        common_entries.push_back(
            {cr.w, cr.h, pp, std::u8string(s.begin(), s.end()), aspect_ratio_str(cr.w, cr.h)});
    }
    std::ranges::sort(common_entries, [](const ResolutionEntry& a, const ResolutionEntry& b) {
        return (a.width * a.height) > (b.width * b.height);
    });

    // Merge: pixel-perfect first, then common.
    std::vector<ResolutionEntry> result;
    result.insert(result.end(), pp_entries.begin(), pp_entries.end());
    result.insert(result.end(), common_entries.begin(), common_entries.end());
    return result;
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
    } // namespace

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
        // Left column: resolution list + aspect ratio hint
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
                                {.visible_rows = 14});

                         // Aspect ratio hint below the list.
                         const auto& sel =
                             state.resolutions[static_cast<size_t>(state.pending_res_index)];
                         const auto ar_line = u8"Aspect ratio: " + sel.aspect_ratio;
                         p.text(ar_line, {.color = {180, 180, 120}});
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

                     // Show pending selection details.
                     if (!state.resolutions.empty()) {
                         const auto& sel =
                             state.resolutions[static_cast<size_t>(state.pending_res_index)];
                         const auto dim = std::format("{}x{}", sel.width, sel.height);
                         p.text(u8"Selected:", {.color = {160, 160, 160}});
                         p.text(std::u8string(dim.begin(), dim.end()), {.color = {220, 220, 220}});

                         // Indicate the blit filter that will be applied.
                         if (sel.pixel_perfect) {
                             p.text(u8"Filter: nearest (crisp)", {.color = {140, 220, 255}});
                         } else {
                             p.text(u8"Filter: bilinear (smooth)", {.color = {180, 180, 180}});
                         }
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

        // Set blit filter: nearest for pixel-perfect, bilinear otherwise.
        renderer.set_nearest_sample(sel.pixel_perfect);

        // Apply virtual resolution change (queued; fires next begin_frame).
        renderer.set_virtual_resolution(sel.width, sel.height);

        // Apply fullscreen toggle.
        renderer.set_fullscreen(state.pending_fullscreen);

        // Record what was applied.
        state.applied_res_index = state.pending_res_index;
        state.applied_fullscreen = state.pending_fullscreen;
        state.applied_vsync = state.pending_vsync;

        const auto filter_str =
            sel.pixel_perfect ? std::string("nearest") : std::string("bilinear");
        const auto msg = std::format(
            "Applied: {}x{}  filter={}  fullscreen={}  vsync={}", sel.width, sel.height, filter_str,
            state.applied_fullscreen ? "on" : "off", state.applied_vsync ? "on" : "off");
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
