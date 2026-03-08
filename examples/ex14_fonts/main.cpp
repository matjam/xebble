/// @file main.cpp  (ex14_fonts)
/// @brief Embedded font showcase — fixed-cell and proportional pixel fonts.
///
/// Demonstrates:
///   - Loading the three embedded fonts: PetMe64 (8x8), PetMe642Y (8x16),
///     Berkelium64 (proportional ~8px)
///   - Switching UITheme font per panel section
///   - Unicode rendering: Latin extended, box-drawing, block elements,
///     C64 PUA graphics, and the multiplication sign that msglog uses
///   - Direct text layout via UIContext::draw_text_at for mixed-font rows
///   - How fixed-cell and proportional metrics differ visually

#include <xebble/embedded_fonts.hpp>
#include <xebble/xebble.hpp>

#include <format>
#include <memory>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// Sample strings exercising different Unicode ranges.
// Stored as u8string_view so they flow directly into the u8-typed text API.
// ---------------------------------------------------------------------------

// Printable ASCII
static constexpr std::u8string_view ASCII_SAMPLE = u8"The quick brown fox jumps over the lazy dog.";

// Latin extended (é à ü ñ ç ø)
static constexpr std::u8string_view LATIN_EXT = u8"caf\u00E9 na\u00EFve r\u00E9sum\u00E9 "
                                                u8"\u00E9\u00E0\u00FC\u00F1\u00E7\u00F8";

// Box-drawing (┌─────┐) + block elements (█▓▒░▖▗▘▙)
static constexpr std::u8string_view BOX_SAMPLE = u8"\u250C\u2500\u2500\u2500\u2500\u2500\u2510 "
                                                 u8"\u2588\u2593\u2592\u2591"
                                                 u8"\u2596\u2597\u2598\u2599";

// Arrows (←↑→↓), maths (×÷±≠≤≥), suits (★♦♣♠♥)
static constexpr std::u8string_view SYMBOL_SAMPLE = u8"\u2190\u2191\u2192\u2193  "
                                                    u8"\u00D7\u00F7\u00B1\u2260\u2264\u2265  "
                                                    u8"\u2605\u2666\u2663\u2660\u2665";

// Mixed sentence: £ (U+00A3), • (U+2022), × (U+00D7), ❤ (U+2764)
static constexpr std::u8string_view UNICODE_SENTENCE =
    u8"Cost: \u00A35.00  \u2022  Score: 9\u00D73 = 27  \u2764";

// ---------------------------------------------------------------------------
// FontShowcaseSystem
// ---------------------------------------------------------------------------

class FontShowcaseSystem : public xebble::System {
    // Owned font objects — created in init(), live for the lifetime of the system.
    std::unique_ptr<xebble::BitmapFont> petme64_;
    std::unique_ptr<xebble::BitmapFont> petme642y_;
    std::unique_ptr<xebble::Font> berkelium64_;

    // Per-font UIThemes so we can swap in draw().
    xebble::UITheme theme_petme64_;
    xebble::UITheme theme_petme642y_;
    xebble::UITheme theme_berkelium_;

    float time_ = 0.0f;

public:
    void init(xebble::World& world) override {
        auto* renderer = world.resource<xebble::Renderer*>();
        auto& ctx = renderer->context();

        // Load embedded fonts — all three created once at startup.
        auto f64 = xebble::embedded_fonts::petme64::create(ctx);
        if (f64)
            petme64_ = std::make_unique<xebble::BitmapFont>(std::move(*f64));

        auto f642y = xebble::embedded_fonts::petme642y::create(ctx);
        if (f642y)
            petme642y_ = std::make_unique<xebble::BitmapFont>(std::move(*f642y));

        auto fberk = xebble::embedded_fonts::berkelium64::create(ctx);
        if (fberk)
            berkelium64_ = std::make_unique<xebble::Font>(std::move(*fberk));

        // Build per-font themes (same colours, different font pointer).
        auto base_theme = xebble::UITheme{
            .bg_color = {15, 15, 25, 210},
            .text_color = {220, 220, 220, 255},
            .padding = 4.0f,
            .margin = 2.0f,
            .z_order = 50.0f,
        };

        theme_petme64_ = base_theme;
        theme_petme642y_ = base_theme;
        theme_berkelium_ = base_theme;

        if (petme64_)
            theme_petme64_.font = petme64_.get();
        if (petme642y_)
            theme_petme642y_.font = petme642y_.get();
        if (berkelium64_)
            theme_berkelium_.font = berkelium64_.get();
    }

    void update(xebble::World& world, float dt) override {
        time_ += dt;
        for (const auto& e : world.resource<xebble::EventQueue>().events) {
            if (e.type == xebble::EventType::KeyPress && e.key().key == xebble::Key::Escape)
                std::exit(0);
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ui = world.resource<xebble::UIContext>();

        // ----------------------------------------------------------------
        // Title bar (uses default theme — embedded 8x12 font)
        // ----------------------------------------------------------------
        ui.panel(
            "title",
            {
                .anchor = xebble::Anchor::Top,
                .size = {1.0f, 20},
            },
            [](auto& p) {
                p.text(
                    u8"ex14 -- Font Showcase  |  PetMe64 / PetMe642Y / Berkelium64  |  [Esc] Quit",
                    {.color = {255, 220, 80}});
            });

        // ----------------------------------------------------------------
        // Left column: PetMe64 (8x8 fixed-cell)
        // ----------------------------------------------------------------
        ui.set_theme(&theme_petme64_);
        ui.panel("petme64",
                 {
                     .anchor = xebble::Anchor::TopLeft,
                     .size = {210, 310},
                     .offset = {2, 22},
                 },
                 [&](auto& p) {
                     p.text(u8"PetMe64  8x8 fixed", {.color = {100, 200, 255}});
                     p.text(std::u8string(ASCII_SAMPLE.substr(0, 24)));
                     p.text(std::u8string(ASCII_SAMPLE.substr(24)));
                     p.text(u8"");
                     p.text(u8"Latin extended:", {.color = {180, 255, 180}});
                     p.text(std::u8string(LATIN_EXT));
                     p.text(u8"");
                     p.text(u8"Box + blocks:", {.color = {180, 255, 180}});
                     p.text(std::u8string(BOX_SAMPLE));
                     p.text(u8"");
                     p.text(u8"Symbols:", {.color = {180, 255, 180}});
                     p.text(std::u8string(SYMBOL_SAMPLE));
                     p.text(u8"");
                     p.text(u8"Unicode mixed:", {.color = {180, 255, 180}});
                     p.text(std::u8string(UNICODE_SENTENCE));
                     p.text(u8"");
                     // Animated rainbow row to show glyph coverage
                     p.text(u8"0123456789  !@#$%^&*()");
                     p.text(u8"ABCDEFGHIJKLMNOPQRSTUVWX");
                     p.text(u8"abcdefghijklmnopqrstuvwx");
                 });

        // ----------------------------------------------------------------
        // Middle column: PetMe642Y (8x16 fixed-cell, double height)
        // ----------------------------------------------------------------
        ui.set_theme(&theme_petme642y_);
        ui.panel("petme642y",
                 {
                     .anchor = xebble::Anchor::Top,
                     .size = {212, 310},
                     .offset = {0, 22},
                 },
                 [&](auto& p) {
                     p.text(u8"PetMe642Y  8x16", {.color = {255, 180, 100}});
                     p.text(std::u8string(ASCII_SAMPLE.substr(0, 24)));
                     p.text(std::u8string(ASCII_SAMPLE.substr(24)));
                     p.text(u8"");
                     p.text(u8"Box + blocks:", {.color = {255, 200, 150}});
                     p.text(std::u8string(BOX_SAMPLE));
                     p.text(u8"");
                     p.text(u8"Symbols:", {.color = {255, 200, 150}});
                     p.text(std::u8string(SYMBOL_SAMPLE));
                     p.text(u8"");
                     p.text(std::u8string(UNICODE_SENTENCE));
                 });

        // ----------------------------------------------------------------
        // Right column: Berkelium64 (proportional)
        // ----------------------------------------------------------------
        ui.set_theme(&theme_berkelium_);
        ui.panel("berkelium",
                 {
                     .anchor = xebble::Anchor::TopRight,
                     .size = {210, 310},
                     .offset = {-2, 22},
                 },
                 [&](auto& p) {
                     p.text(u8"Berkelium64  prop ~8px", {.color = {200, 150, 255}});
                     p.text(std::u8string(ASCII_SAMPLE.substr(0, 24)));
                     p.text(std::u8string(ASCII_SAMPLE.substr(24)));
                     p.text(u8"");
                     p.text(u8"Latin extended:", {.color = {220, 180, 255}});
                     p.text(std::u8string(LATIN_EXT));
                     p.text(u8"");
                     p.text(u8"Symbols:", {.color = {220, 180, 255}});
                     p.text(std::u8string(SYMBOL_SAMPLE));
                     p.text(u8"");
                     p.text(std::u8string(UNICODE_SENTENCE));
                     p.text(u8"");
                     // Proportional spacing is visible here — notice 'i' vs 'W'
                     p.text(u8"iiii vs WWWW  (prop)", {.color = {180, 180, 180}});
                     p.text(u8"1234567890");
                 });

        // ----------------------------------------------------------------
        // Bottom: side-by-side metrics comparison
        // ----------------------------------------------------------------
        ui.set_theme(&theme_petme64_);
        ui.panel("compare",
                 {
                     .anchor = xebble::Anchor::Bottom,
                     .size = {1.0f, 38},
                 },
                 [&](auto& p) {
                     p.text(u8"Fixed-cell: every glyph advances by exactly cell_w pixels.  "
                            u8"Proportional: advance varies per glyph (i < m < W).",
                            {.color = {200, 200, 160}});
                     {
                         auto s = std::format(
                             "PetMe64 cell=8x8  |  PetMe642Y cell=8x16  |  "
                             "Berkelium64 line_h={}px  |  time={:.1f}s",
                             berkelium64_ ? static_cast<int>(berkelium64_->line_height()) : 0,
                             time_);
                         p.text(std::u8string(s.begin(), s.end()), {.color = {150, 150, 150}});
                     }
                 });
        xebble::debug_overlay(world, renderer);
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<FontShowcaseSystem>();

    return xebble::run(
        std::move(world),
        {
            .window = {.title = "ex14 -- Font Showcase", .width = 1280, .height = 720},
            .renderer = {.virtual_width = 960, .virtual_height = 540},
        });
}
