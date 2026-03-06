/// @file ui.cpp
/// @brief Immediate-mode UI system implementation.
#include <xebble/ui.hpp>
#include <xebble/world.hpp>
#include <xebble/spritesheet.hpp>

#include <algorithm>

namespace xebble {

// --- Placement resolution ---

static float resolve_size(float s, float screen) {
    return (s > 0.0f && s <= 1.0f) ? s * screen : s;
}

Rect resolve_panel_placement(const PanelPlacement& p, uint32_t screen_w, uint32_t screen_h) {
    float sw = static_cast<float>(screen_w);
    float sh = static_cast<float>(screen_h);
    float w = resolve_size(p.size.x, sw);
    float h = resolve_size(p.size.y, sh);
    float x = 0, y = 0;

    switch (p.anchor) {
        case Anchor::TopLeft:     x = 0;            y = 0;            break;
        case Anchor::Top:         x = (sw - w) / 2; y = 0;            break;
        case Anchor::TopRight:    x = sw - w;        y = 0;            break;
        case Anchor::Left:        x = 0;            y = (sh - h) / 2; break;
        case Anchor::Center:      x = (sw - w) / 2; y = (sh - h) / 2; break;
        case Anchor::Right:       x = sw - w;        y = (sh - h) / 2; break;
        case Anchor::BottomLeft:  x = 0;            y = sh - h;        break;
        case Anchor::Bottom:      x = (sw - w) / 2; y = sh - h;        break;
        case Anchor::BottomRight: x = sw - w;        y = sh - h;        break;
    }

    return {x + p.offset.x, y + p.offset.y, w, h};
}

// --- UIContext stubs ---

UIContext::UIContext() = default;
UIContext::~UIContext() = default;

void UIContext::begin_frame(std::vector<Event>&, const Renderer&) {}
void UIContext::flush(Renderer&) {}
Rect UIContext::resolve_placement(const PanelPlacement& p) const {
    return resolve_panel_placement(p, screen_w_, screen_h_);
}
void UIContext::draw_panel_bg(Rect) {}
void UIContext::draw_rect(Rect, Color, float) {}
void UIContext::draw_text_at(std::string_view, float, float, Color, float) {}
float UIContext::glyph_width() const { return 8.0f; }
float UIContext::glyph_height() const { return 12.0f; }
void UIContext::register_widget(std::string_view, Rect) {}
bool UIContext::is_hot(std::string_view) const { return false; }
bool UIContext::is_clicked(std::string_view) const { return false; }

// --- PanelBuilder stubs ---

PanelBuilder::PanelBuilder(UIContext& ctx, Rect panel_rect, float z_base)
    : ctx_(ctx), panel_rect_(panel_rect), z_base_(z_base),
      cursor_y_(panel_rect.y + ctx.theme_->padding),
      content_x_(panel_rect.x + ctx.theme_->padding),
      content_width_(panel_rect.w - 2 * ctx.theme_->padding),
      padding_(ctx.theme_->padding), margin_(ctx.theme_->margin) {}

Rect PanelBuilder::next_control_rect(float height) {
    Rect r;
    if (in_horizontal_) {
        r = {horiz_cursor_x_, cursor_y_, content_width_, height};
        horiz_cursor_x_ += content_width_ + margin_;
        horiz_max_h_ = std::max(horiz_max_h_, height);
    } else {
        r = {content_x_, cursor_y_, content_width_, height};
        cursor_y_ += height + margin_;
    }
    return r;
}

float PanelBuilder::text_height() const { return ctx_.glyph_height(); }
float PanelBuilder::measure_text_width(std::string_view text) const {
    return static_cast<float>(text.size()) * ctx_.glyph_width();
}

void PanelBuilder::text(std::string_view, TextStyle) {}
bool PanelBuilder::button(std::string_view, ButtonStyle) { return false; }
void PanelBuilder::checkbox(std::string_view, bool&, CheckboxStyle) {}
void PanelBuilder::list(std::string_view, std::span<const std::string>, int&, ListStyle) {}
bool PanelBuilder::text_input(std::string_view, std::string&, TextInputStyle) { return false; }

// --- Systems stubs ---

void UIInputSystem::update(World&, float) {}
void UIFlushSystem::draw(World&, Renderer&) {}

} // namespace xebble
