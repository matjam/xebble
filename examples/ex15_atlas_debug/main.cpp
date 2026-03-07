/// @file main.cpp  (ex15_atlas_debug)
/// @brief Renders the Berkelium64 glyph atlas at 4× zoom so every pixel is
///        visible.  Also shows the font rendered at normal size for comparison.
///
/// Layout (640×360 virtual):
///   Top area  : atlas rendered at 4× scale (2048×16 → but we show a 320-wide
///               window into it, scrollable with LEFT/RIGHT, zoomed 4×).
///               Actually we render the full atlas width compressed to fit, or
///               we tile it in strips.  Since atlas is 2048×16, at 4× that is
///               8192×64 — too wide.  Instead we render it in 6 horizontal
///               strips of 320 virtual-px each at 4× zoom (320px/4 = 80 atlas
///               columns per strip), stacked vertically.  Each strip is 64px
///               tall (16 atlas rows × 4).
///   Bottom area: normal-size Berkelium64 text for comparison.
///   [LEFT]/[RIGHT] or press nothing — shows all strips stacked.
///   [Esc] quit.

#include <xebble/xebble.hpp>
#include <xebble/embedded_fonts.hpp>

#include <format>
#include <memory>

namespace {

// Atlas constants (match embedded_berkelium64.cpp)
constexpr uint32_t ATLAS_W = 2048u;
constexpr uint32_t ATLAS_H = 16u;

// How many virtual pixels wide each rendered strip is.
// At 4× zoom: each strip shows STRIP_VW/4 = 80 atlas columns.
constexpr float ZOOM      = 4.0f;
constexpr float STRIP_VW  = 320.0f;           // virtual pixels wide per strip
constexpr float STRIP_VH  = ATLAS_H * ZOOM;   // 64 virtual pixels tall
constexpr float ATLAS_COLS_PER_STRIP = STRIP_VW / ZOOM;  // 80 atlas columns

// Number of strips needed to cover the full atlas width.
// ceil(2048 / 80) = 26 strips
constexpr int NUM_STRIPS = (ATLAS_W + (int)ATLAS_COLS_PER_STRIP - 1) / (int)ATLAS_COLS_PER_STRIP;

class AtlasDebugSystem : public xebble::System {
    std::unique_ptr<xebble::Font> font_;
    xebble::UITheme theme_berk_;
    float time_ = 0.0f;

public:
    void init(xebble::World& world) override {
        auto* renderer = world.resource<xebble::Renderer*>();
        auto& ctx = renderer->context();

        auto fb = xebble::embedded_fonts::berkelium64::create(ctx);
        if (fb) font_ = std::make_unique<xebble::Font>(std::move(*fb));

        theme_berk_ = xebble::UITheme{
            .bg_color   = {15, 15, 25, 200},
            .text_color = {220, 220, 220, 255},
            .padding    = 4.0f,
            .margin     = 2.0f,
            .z_order    = 50.0f,
        };
        if (font_) theme_berk_.font = font_.get();
    }

    void update(xebble::World& world, float dt) override {
        time_ += dt;
        for (const auto& e : world.resource<xebble::EventQueue>().events) {
            if (e.type == xebble::EventType::KeyPress &&
                e.key().key == xebble::Key::Escape)
                std::exit(0);
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        if (!font_) return;
        const xebble::Texture& atlas_tex = font_->texture();

        // ----------------------------------------------------------------
        // Render the atlas in strips, stacked vertically from y=0.
        // Each strip: 320 virtual px wide, STRIP_VH px tall.
        // We fit as many strips as will fit in the top 250px of the screen.
        // ----------------------------------------------------------------
        const float uw = float(ATLAS_W);   // atlas texel width
        const float uh = float(ATLAS_H);   // atlas texel height

        // How many strips fit in the display area (max ~250px, leaving room below)
        const float display_area_h = 252.0f;
        const int   strips_visible = std::min(
            NUM_STRIPS,
            (int)(display_area_h / STRIP_VH)   // how many fit
        );

        for (int s = 0; s < strips_visible; ++s) {
            // Atlas X range this strip covers: [s*80, (s+1)*80) texels
            float atlas_x0 = float(s) * ATLAS_COLS_PER_STRIP;
            float atlas_x1 = std::min(atlas_x0 + ATLAS_COLS_PER_STRIP, float(ATLAS_W));
            float strip_w_atlas = atlas_x1 - atlas_x0;  // actual texels (may be < 80 for last)

            float uv_x = atlas_x0 / uw;
            float uv_y = 0.0f;
            float uv_w = strip_w_atlas / uw;
            float uv_h = 1.0f;   // full height of atlas

            // Quad size in virtual pixels (4× zoom)
            float quad_w = strip_w_atlas * ZOOM;
            float quad_h = STRIP_VH;

            // Position: strips tile left→right, then wrap to next row
            // Lay them in two columns of 160 px each to fit 640px wide:
            // Actually simpler: lay them horizontally, two per row
            //   column 0: x=0,   column 1: x=320
            //   row advances every 2 strips
            float pos_x = (s % 2) * STRIP_VW;
            float pos_y = (s / 2) * STRIP_VH;

            xebble::SpriteInstance inst{
                .pos_x = pos_x,  .pos_y = pos_y,
                .uv_x  = uv_x,   .uv_y  = uv_y,
                .uv_w  = uv_w,   .uv_h  = uv_h,
                .quad_w = quad_w, .quad_h = quad_h,
                .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                .scale = 1.0f, .rotation = 0.0f, .pivot_x = 0.0f, .pivot_y = 0.0f,
            };
            renderer.submit_instances({&inst, 1}, atlas_tex, 1.0f);
        }

        // ----------------------------------------------------------------
        // Info label at top-right corner
        // ----------------------------------------------------------------
        auto& ui = world.resource<xebble::UIContext>();
        ui.panel("info", {
            .anchor = xebble::Anchor::TopRight,
            .size   = {260, 18},
            .offset = {0, 0},
        }, [&](auto& p) {
            p.text(u8"ex15: Berkelium64 atlas 4x zoom",
                   {.color = {255, 220, 80}});
        });

        // ----------------------------------------------------------------
        // Bottom panel: normal-size font rendering for comparison
        // ----------------------------------------------------------------
        ui.set_theme(&theme_berk_);
        ui.panel("sample", {
            .anchor = xebble::Anchor::Bottom,
            .size   = {1.0f, 90},
        }, [&](auto& p) {
            p.text(u8"Normal size Berkelium64 (compare with atlas above):",
                   {.color = {180, 180, 255}});
            p.text(u8"The quick brown fox jumps over the lazy dog.");
            p.text(u8"ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789");
            p.text(u8"g p y q j  (descenders)  | ( ) [ ] { }  _ $");
            { auto s = std::format("ASCENDER=7  LINE_HEIGHT=9  time={:.1f}s", time_);
              p.text(std::u8string(s.begin(), s.end()), {.color = {150, 150, 150}}); }
        });
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<AtlasDebugSystem>();

    return xebble::run(std::move(world), {
        .window   = {.title  = "ex15 -- Atlas Debug",
                     .width  = 1280,
                     .height = 720},
        .renderer = {.virtual_width  = 640,
                     .virtual_height = 360},
    });
}
