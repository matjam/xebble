/// @file ui.cpp
/// @brief Immediate-mode UI system implementation.
#include "utf8.hpp"

#include <xebble/ecs.hpp>
#include <xebble/spritesheet.hpp>
#include <xebble/ui.hpp>
#include <xebble/world.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <ranges>
#include <utility>

namespace xebble {

// --- Helpers ---

namespace {

bool is_default_color(Color c) {
    return c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0;
}

Color pick_color(Color style_color, Color theme_color) {
    return is_default_color(style_color) ? theme_color : style_color;
}

// --- Placement resolution ---

float resolve_size(float s, float screen) {
    return (s > 0.0f && s <= 1.0f) ? s * screen : s;
}

} // namespace

Rect resolve_panel_placement(const PanelPlacement& p, uint32_t screen_w, uint32_t screen_h) {
    const auto sw = static_cast<float>(screen_w);
    const auto sh = static_cast<float>(screen_h);
    const float w = resolve_size(p.size.x, sw);
    const float h = resolve_size(p.size.y, sh);
    float x = 0;
    float y = 0;

    switch (p.anchor) {
    case Anchor::TopLeft:
        x = 0;
        y = 0;
        break;
    case Anchor::Top:
        x = (sw - w) / 2;
        y = 0;
        break;
    case Anchor::TopRight:
        x = sw - w;
        y = 0;
        break;
    case Anchor::Left:
        x = 0;
        y = (sh - h) / 2;
        break;
    case Anchor::Center:
        x = (sw - w) / 2;
        y = (sh - h) / 2;
        break;
    case Anchor::Right:
        x = sw - w;
        y = (sh - h) / 2;
        break;
    case Anchor::BottomLeft:
        x = 0;
        y = sh - h;
        break;
    case Anchor::Bottom:
        x = (sw - w) / 2;
        y = sh - h;
        break;
    case Anchor::BottomRight:
        x = sw - w;
        y = sh - h;
        break;
    }

    return {x + p.offset.x, y + p.offset.y, w, h};
}

// --- UIContext ---

UIContext::UIContext() = default;
UIContext::~UIContext() = default;

void UIContext::begin_frame(std::vector<Event>& events, const Renderer& renderer) {
    prev_rects_ = std::move(curr_rects_);
    curr_rects_.clear();
    batches_.clear();

    frame_events_ = &events;
    input_chars_.clear();

    screen_w_ = renderer.virtual_width();
    screen_h_ = renderer.virtual_height();

    mouse_clicked_ = false;

    for (auto& e : events) {
        if (e.consumed) {
            continue;
        }
        switch (e.type) {
        case EventType::MouseMove:
            mouse_pos_ = renderer.screen_to_virtual(e.mouse_move().position);
            break;
        case EventType::MousePress:
            if (e.mouse_button().button == MouseButton::Left) {
                mouse_down_ = true;
                mouse_clicked_ = true;
                mouse_pos_ = renderer.screen_to_virtual(e.mouse_button().position);
            }
            break;
        case EventType::MouseRelease:
            if (e.mouse_button().button == MouseButton::Left) {
                mouse_down_ = false;
                active_id_.clear();
            }
            break;
        case EventType::KeyPress:
        case EventType::KeyRepeat: {
            auto k = e.key().key;
            auto mods = e.key().mods;
            const int kv = static_cast<int>(k);
            // Printable characters: Space through Z (GLFW key codes 32-90)
            if (kv >= static_cast<int>(Key::Space) && kv <= static_cast<int>(Key::Z)) {
                char ch = static_cast<char>(kv);
                // Letters: apply lowercase unless shift is held
                if (kv >= static_cast<int>(Key::A) && kv <= static_cast<int>(Key::Z)) {
                    if (!mods.shift) {
                        ch = static_cast<char>(ch + 32); // to lowercase
                    }
                }
                input_chars_.push_back(ch);
            }
            break;
        }
        default:
            break;
        }
    }

    // Determine hot widget from prev_rects_.
    // Widgets are registered in draw order: containers first, children last.
    // We want the most-specific (innermost) hit, so we scan in reverse and
    // take the last match — i.e. the child registered latest wins over its
    // parent container.
    hot_id_.clear();
    for (const auto& wr : std::views::reverse(prev_rects_)) {
        if (mouse_pos_.x >= wr.rect.x && mouse_pos_.x <= wr.rect.x + wr.rect.w &&
            mouse_pos_.y >= wr.rect.y && mouse_pos_.y <= wr.rect.y + wr.rect.h) {
            hot_id_ = wr.id;
            break;
        }
    }

    // Track active widget: the widget the mouse went down on stays active
    // until the button is released (handled in MouseRelease above).
    if (mouse_clicked_ && !hot_id_.empty()) {
        active_id_ = hot_id_;
    }
}

void UIContext::flush(Renderer& renderer) {
    for (auto& batch : batches_) {
        if (!batch.instances.empty() && batch.texture != nullptr) {
            renderer.submit_instances(batch.instances, *batch.texture, batch.z_order);
        }
    }
    batches_.clear();
    frame_events_ = nullptr;
}

Rect UIContext::resolve_placement(const PanelPlacement& p) const {
    return resolve_panel_placement(p, screen_w_, screen_h_);
}

void UIContext::draw_panel_bg(Rect rect) {
    draw_rect(rect, theme_->bg_color, theme_->z_order);
    if (theme_->border_width > 0.0f) {
        draw_border(rect, theme_->border_color, theme_->border_width, theme_->z_order + 0.07f);
    }
}

void UIContext::ensure_white_texture(vk::Context& ctx) {
    if (white_texture_) {
        return;
    }
    // 1×1 opaque white RGBA pixel — used as the source for all filled rect draws.
    static constexpr std::array<uint8_t, 4> WHITE = {255, 255, 255, 255};
    auto result = Texture::create_from_pixels(ctx, WHITE.data(), 1, 1);
    if (result) {
        white_texture_ = std::make_shared<Texture>(std::move(*result));
    }
}

void UIContext::draw_rect(Rect rect, Color color, float z) {
    if (white_texture_ == nullptr) {
        return;
    }
    const Texture* tex = white_texture_.get();

    SpriteInstance inst{
        .pos_x = rect.x,
        .pos_y = rect.y,
        .uv_x = 0.0f,
        .uv_y = 0.0f,
        .uv_w = 1.0f,
        .uv_h = 1.0f,
        .quad_w = rect.w,
        .quad_h = rect.h,
        .r = static_cast<float>(color.r) / 255.0f,
        .g = static_cast<float>(color.g) / 255.0f,
        .b = static_cast<float>(color.b) / 255.0f,
        .a = static_cast<float>(color.a) / 255.0f,
        .scale = 1.0f,
        .rotation = 0.0f,
        .pivot_x = 0.0f,
        .pivot_y = 0.0f,
    };
    batches_.push_back({{inst}, tex, z});
}

void UIContext::draw_border(Rect rect, Color color, float width, float z) {
    // Top edge.
    draw_rect({rect.x, rect.y, rect.w, width}, color, z);
    // Bottom edge.
    draw_rect({rect.x, rect.y + rect.h - width, rect.w, width}, color, z);
    // Left edge (between top and bottom).
    draw_rect({rect.x, rect.y + width, width, rect.h - (2.0f * width)}, color, z);
    // Right edge (between top and bottom).
    draw_rect({rect.x + rect.w - width, rect.y + width, width, rect.h - (2.0f * width)}, color, z);
}

void UIContext::draw_text_at(std::u8string_view text, float x, float y, Color color, float z) {
    if (text.empty()) {
        return;
    }

    const float cr = static_cast<float>(color.r) / 255.0f;
    const float cg = static_cast<float>(color.g) / 255.0f;
    const float cb = static_cast<float>(color.b) / 255.0f;
    const float ca = static_cast<float>(color.a) / 255.0f;

    if (const auto* bf = std::get_if<const BitmapFont*>(&theme_->font)) {
        if (*bf == nullptr) {
            return;
        }
        const Texture* tex = &(*bf)->sheet().texture();
        std::vector<SpriteInstance> instances;
        const auto gw = static_cast<float>((*bf)->glyph_width());
        const auto gh = static_cast<float>((*bf)->glyph_height());

        float cx = x;
        for (const uint32_t cp : utf8::codepoints(std::u8string_view{text})) {
            auto gi = (*bf)->glyph_index(cp);
            if (!gi) {
                cx += gw;
                continue;
            } // advance past unknown glyphs
            auto r = (*bf)->sheet().region(*gi);
            instances.push_back(SpriteInstance{
                .pos_x = cx,
                .pos_y = y,
                .uv_x = r.x,
                .uv_y = r.y,
                .uv_w = r.w,
                .uv_h = r.h,
                .quad_w = gw,
                .quad_h = gh,
                .r = cr,
                .g = cg,
                .b = cb,
                .a = ca,
                .scale = 1.0f,
                .rotation = 0.0f,
                .pivot_x = 0.0f,
                .pivot_y = 0.0f,
            });
            cx += gw;
        }
        if (!instances.empty()) {
            batches_.push_back({std::move(instances), tex, z});
        }
    } else if (const auto* f = std::get_if<const Font*>(&theme_->font)) {
        if (*f == nullptr) {
            return;
        }
        const Texture* tex = &(*f)->texture();
        std::vector<SpriteInstance> instances;
        const float asc = (*f)->ascender();
        float cx = x;

        for (const uint32_t cp : utf8::codepoints(text)) {
            auto gm = (*f)->glyph(cp);
            if (!gm) {
                cx += (*f)->line_height() * 0.5f;
                continue;
            } // half-em skip
            instances.push_back(SpriteInstance{
                .pos_x = cx + gm->bearing_x,
                .pos_y = y + asc - gm->bearing_y,
                .uv_x = gm->uv.x,
                .uv_y = gm->uv.y,
                .uv_w = gm->uv.w,
                .uv_h = gm->uv.h,
                .quad_w = gm->width,
                .quad_h = gm->height,
                .r = cr,
                .g = cg,
                .b = cb,
                .a = ca,
                .scale = 1.0f,
                .rotation = 0.0f,
                .pivot_x = 0.0f,
                .pivot_y = 0.0f,
            });
            cx += gm->advance;
        }
        if (!instances.empty()) {
            batches_.push_back({std::move(instances), tex, z});
        }
    }
}

float UIContext::glyph_width() const {
    if (const auto* bf = std::get_if<const BitmapFont*>(&theme_->font)) {
        if (*bf != nullptr) {
            return static_cast<float>((*bf)->glyph_width());
        }
    } else if (const auto* f = std::get_if<const Font*>(&theme_->font)) {
        if (*f != nullptr) {
            auto gm = (*f)->glyph('M');
            if (gm) {
                return gm->advance;
            }
        }
    }
    return 8.0f;
}

float UIContext::glyph_height() const {
    if (const auto* bf = std::get_if<const BitmapFont*>(&theme_->font)) {
        if (*bf != nullptr) {
            return static_cast<float>((*bf)->glyph_height());
        }
    } else if (const auto* f = std::get_if<const Font*>(&theme_->font)) {
        if (*f != nullptr) {
            return (*f)->line_height();
        }
    }
    return 12.0f;
}

void UIContext::register_widget(std::string_view id, Rect rect) {
    curr_rects_.push_back({std::string(id), rect});
}

bool UIContext::is_hot(std::string_view id) const {
    return hot_id_ == id;
}

bool UIContext::is_active(std::string_view id) const {
    return active_id_ == id;
}

bool UIContext::is_clicked(std::string_view id) const {
    return is_hot(id) && mouse_clicked_;
}

// --- PanelBuilder ---

PanelBuilder::PanelBuilder(UIContext& ctx, Rect panel_rect, float z_base, std::string_view panel_id)
    : ctx_(ctx),
      panel_id_(panel_id),
      panel_rect_(panel_rect),
      z_base_(z_base),
      cursor_y_(panel_rect.y + ctx.theme_->padding),
      content_x_(panel_rect.x + ctx.theme_->padding),
      content_width_(panel_rect.w - (2 * ctx.theme_->padding)),
      padding_(ctx.theme_->padding),
      margin_(ctx.theme_->margin) {}

std::string PanelBuilder::make_widget_id(std::string_view label) {
    return std::string(panel_id_) + "##" + std::string(label) + "##" +
           std::to_string(widget_seq_++);
}

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

void PanelBuilder::correct_horiz_advance(float actual_w) {
    // Undo the content_width_ advance and replace with actual_w.
    horiz_cursor_x_ -= content_width_ + margin_;
    horiz_cursor_x_ += actual_w + margin_;
}

float PanelBuilder::text_height() const {
    return ctx_.glyph_height();
}
float PanelBuilder::measure_text_width(std::u8string_view text) const {
    if (const auto* f = std::get_if<const Font*>(&ctx_.theme_->font)) {
        if (*f != nullptr) {
            float w = 0.0f;
            for (const uint32_t cp : utf8::codepoints(text)) {
                auto gm = (*f)->glyph(cp);
                w += gm ? gm->advance : (*f)->line_height() * 0.5f;
            }
            return w;
        }
    }
    // Fixed-cell: count codepoints * cell width.
    return static_cast<float>(utf8::codepoints(text).count()) * ctx_.glyph_width();
}

void PanelBuilder::text(std::u8string_view text, TextStyle style) {
    const float h = text_height();
    auto r = next_control_rect(h);
    const Color color = pick_color(style.color, ctx_.theme_->text_color);
    ctx_.draw_text_at(text, r.x, r.y, color, z_base_ + 0.1f);
}

bool PanelBuilder::button(std::u8string_view label, ButtonStyle style) {
    const float h = text_height() + (padding_ * 2);
    auto r = next_control_rect(h);
    if (style.width > 0.0f) {
        r.w = style.width;
        if (in_horizontal_) {
            correct_horiz_advance(style.width);
        }
    } else if (in_horizontal_) {
        const float auto_w = measure_text_width(label) + (padding_ * 2);
        r.w = auto_w;
        correct_horiz_advance(auto_w);
    }

    const std::string id =
        make_widget_id(std::string_view(reinterpret_cast<const char*>(label.data()), label.size()));
    ctx_.register_widget(id, r);
    last_widget_id_ = id;

    const bool hovered = ctx_.is_hot(id);
    const bool active = ctx_.is_active(id);
    const bool pressed = active && ctx_.mouse_down_;
    const bool clicked = ctx_.is_clicked(id);

    Color bg;
    if (pressed) {
        bg = pick_color(style.pressed_color, ctx_.theme_->button_pressed_color);
    } else if (hovered) {
        bg = pick_color(style.hover_color, ctx_.theme_->button_hover_color);
    } else {
        bg = pick_color(style.color, ctx_.theme_->button_color);
    }
    const Color text_col = pick_color(style.text_color, ctx_.theme_->button_text_color);

    ctx_.draw_rect(r, bg, z_base_ + 0.05f);
    if (ctx_.theme_->border_width > 0.0f) {
        ctx_.draw_border(r, ctx_.theme_->border_color, ctx_.theme_->border_width, z_base_ + 0.07f);
    }
    ctx_.draw_text_at(label, r.x + padding_, r.y + padding_, text_col, z_base_ + 0.1f);

    if (clicked && ctx_.frame_events_ != nullptr) {
        for (auto& e : *ctx_.frame_events_) {
            if (e.type == EventType::MousePress && !e.consumed) {
                e.consumed = true;
                break;
            }
        }
    }
    return clicked;
}

void PanelBuilder::checkbox(std::u8string_view label, bool& value, CheckboxStyle style) {
    const float h = text_height() + (padding_ * 2);
    auto r = next_control_rect(h);
    const std::string id =
        make_widget_id(std::string_view(reinterpret_cast<const char*>(label.data()), label.size()));
    ctx_.register_widget(id, r);
    last_widget_id_ = id;

    const bool clicked = ctx_.is_clicked(id);
    if (clicked) {
        value = !value;
    }

    const float box_size = text_height();
    const Rect box_rect = {r.x + padding_, r.y + padding_, box_size, box_size};
    const Color box_color =
        value ? pick_color(style.checked_color, ctx_.theme_->checkbox_checked_color)
              : pick_color(style.color, ctx_.theme_->checkbox_color);
    ctx_.draw_rect(box_rect, box_color, z_base_ + 0.05f);
    if (ctx_.theme_->border_width > 0.0f) {
        ctx_.draw_border(box_rect, ctx_.theme_->border_color, ctx_.theme_->border_width,
                         z_base_ + 0.07f);
    }

    if (value) {
        const float inset = std::max(2.0f, box_size * 0.25f);
        const Rect inner = {box_rect.x + inset, box_rect.y + inset, box_size - (inset * 2),
                            box_size - (inset * 2)};
        ctx_.draw_rect(inner, pick_color(style.text_color, ctx_.theme_->text_color),
                       z_base_ + 0.1f);
    }

    const float text_x = box_rect.x + box_size + padding_;
    const Color text_col = pick_color(style.text_color, ctx_.theme_->text_color);
    ctx_.draw_text_at(label, text_x, r.y + padding_, text_col, z_base_ + 0.1f);

    if (clicked && ctx_.frame_events_ != nullptr) {
        for (auto& e : *ctx_.frame_events_) {
            if (e.type == EventType::MousePress && !e.consumed) {
                e.consumed = true;
                break;
            }
        }
    }
}

void PanelBuilder::radio_button(std::u8string_view label, int& selected, int value,
                                RadioButtonStyle style) {
    const float h = text_height() + (padding_ * 2);
    auto r = next_control_rect(h);

    // Auto-size in horizontal mode: box + gap + label + padding.
    const float box_size = text_height();
    if (in_horizontal_) {
        const float auto_w = padding_ + box_size + padding_ + measure_text_width(label) + padding_;
        r.w = auto_w;
        correct_horiz_advance(auto_w);
    }

    const std::string id =
        make_widget_id(std::string_view(reinterpret_cast<const char*>(label.data()), label.size()));
    ctx_.register_widget(id, r);
    last_widget_id_ = id;

    const bool clicked = ctx_.is_clicked(id);
    if (clicked) {
        selected = value;
    }

    const bool is_selected = (selected == value);

    const Rect box_rect = {r.x + padding_, r.y + padding_, box_size, box_size};
    const Color box_color =
        is_selected ? pick_color(style.selected_color, ctx_.theme_->radio_selected_color)
                    : pick_color(style.color, ctx_.theme_->radio_color);
    ctx_.draw_rect(box_rect, box_color, z_base_ + 0.05f);
    if (ctx_.theme_->border_width > 0.0f) {
        ctx_.draw_border(box_rect, ctx_.theme_->border_color, ctx_.theme_->border_width,
                         z_base_ + 0.07f);
    }

    if (is_selected) {
        const float inset = std::max(2.0f, box_size * 0.25f);
        const Rect inner = {box_rect.x + inset, box_rect.y + inset, box_size - (inset * 2),
                            box_size - (inset * 2)};
        ctx_.draw_rect(inner, pick_color(style.text_color, ctx_.theme_->text_color),
                       z_base_ + 0.1f);
    }

    const float text_x = box_rect.x + box_size + padding_;
    const Color text_col = pick_color(style.text_color, ctx_.theme_->text_color);
    ctx_.draw_text_at(label, text_x, r.y + padding_, text_col, z_base_ + 0.1f);

    if (clicked && ctx_.frame_events_ != nullptr) {
        for (auto& e : *ctx_.frame_events_) {
            if (e.type == EventType::MousePress && !e.consumed) {
                e.consumed = true;
                break;
            }
        }
    }
}

void PanelBuilder::slider(std::string_view id, std::u8string_view label, float& value, float min,
                          float max, SliderStyle style) {
    const float h = text_height() + (padding_ * 2);
    auto r = next_control_rect(h);
    const std::string id_str = make_widget_id(id);
    ctx_.register_widget(id_str, r);
    last_widget_id_ = id_str;

    const Color text_col = pick_color(style.text_color, ctx_.theme_->text_color);

    // Layout: [padding | label | padding | track | padding | value | padding]
    //
    // The value area is a fixed width based on the widest possible value string
    // so the track doesn't jitter as the number changes.  The track fills
    // whatever space remains.

    const float label_w = measure_text_width(label);

    // Measure the widest value string (try both endpoints and current value).
    float value_area_w = 0.0f;
    std::u8string value_str;
    if (style.show_value) {
        auto fmt_value = [&](float v) -> std::u8string {
            std::string s;
            if (min == std::floor(min) && max == std::floor(max)) {
                s = std::format("{}", static_cast<int>(v));
            } else {
                s = std::format("{:.1f}", v);
            }
            return {s.begin(), s.end()};
        };
        const std::u8string min_str = fmt_value(min);
        const std::u8string max_str = fmt_value(max);
        value_str = fmt_value(std::clamp(value, min, max));
        value_area_w = std::max({measure_text_width(min_str), measure_text_width(max_str),
                                 measure_text_width(value_str)});
    }

    const float track_x = r.x + padding_ + label_w + padding_;
    float track_end = r.x + r.w - padding_;
    if (style.show_value) {
        track_end -= value_area_w + padding_;
    }
    const float track_w = std::max(20.0f, track_end - track_x);
    const Rect track_rect = {track_x, r.y, track_w, h};

    // Draw label.
    ctx_.draw_text_at(label, r.x + padding_, r.y + padding_, text_col, z_base_ + 0.1f);

    // Draw track background.
    const Color track_col = pick_color(style.track_color, ctx_.theme_->slider_track_color);
    ctx_.draw_rect(track_rect, track_col, z_base_ + 0.05f);
    if (ctx_.theme_->border_width > 0.0f) {
        ctx_.draw_border(track_rect, ctx_.theme_->border_color, ctx_.theme_->border_width,
                         z_base_ + 0.07f);
    }

    // Handle click and drag.
    const bool is_active = ctx_.is_active(id_str);
    if (ctx_.is_clicked(id_str) || (is_active && ctx_.mouse_down_)) {
        const float mx = ctx_.mouse_pos_.x;
        const float fraction = std::clamp((mx - track_x) / track_w, 0.0f, 1.0f);
        value = min + (fraction * (max - min));
        if (ctx_.is_clicked(id_str) && ctx_.frame_events_ != nullptr) {
            for (auto& e : *ctx_.frame_events_) {
                if (e.type == EventType::MousePress && !e.consumed) {
                    e.consumed = true;
                    break;
                }
            }
        }
    }

    value = std::clamp(value, min, max);

    // Draw thumb.
    const float fraction = (max > min) ? (value - min) / (max - min) : 0.0f;
    const float thumb_w = std::max(6.0f, h * 0.4f);
    const float thumb_x = track_x + (fraction * (track_w - thumb_w));
    const Rect thumb_rect = {thumb_x, r.y + 2.0f, thumb_w, h - 4.0f};
    const Color thumb_col = pick_color(style.thumb_color, ctx_.theme_->slider_thumb_color);
    ctx_.draw_rect(thumb_rect, thumb_col, z_base_ + 0.08f);

    // Draw value text right-aligned in the fixed value area.
    if (style.show_value) {
        float vx = track_x + track_w + padding_;
        // Right-align within the value area.
        const float actual_w = measure_text_width(value_str);
        vx += value_area_w - actual_w;
        ctx_.draw_text_at(value_str, vx, r.y + padding_, text_col, z_base_ + 0.1f);
    }
}

void PanelBuilder::list(std::string_view id, std::span<const std::u8string> items, int& selected,
                        ListStyle style) {
    const float row_h = text_height() + (padding_ * 2);
    const float visible_rows = style.visible_rows;
    const float total_h = row_h * visible_rows;
    auto r = next_control_rect(total_h);

    const Color bg = pick_color(style.color, ctx_.theme_->list_color);
    ctx_.draw_rect(r, bg, z_base_ + 0.05f);
    if (ctx_.theme_->border_width > 0.0f) {
        ctx_.draw_border(r, ctx_.theme_->border_color, ctx_.theme_->border_width, z_base_ + 0.07f);
    }

    const std::string id_str = make_widget_id(id);
    last_widget_id_ = id_str;
    int& scroll_offset = ctx_.scroll_offsets_[id_str];

    ctx_.register_widget(id_str, r);

    if (ctx_.is_hot(id_str) && ctx_.frame_events_ != nullptr) {
        for (auto& e : *ctx_.frame_events_) {
            if (e.type == EventType::MouseScroll && !e.consumed) {
                scroll_offset -= static_cast<int>(e.mouse_scroll().dy);
                const int max_scroll =
                    std::max(0, static_cast<int>(items.size()) - static_cast<int>(visible_rows));
                scroll_offset = std::clamp(scroll_offset, 0, max_scroll);
                e.consumed = true;
                break;
            }
        }
    }

    const int max_scroll =
        std::max(0, static_cast<int>(items.size()) - static_cast<int>(visible_rows));
    scroll_offset = std::clamp(scroll_offset, 0, max_scroll);

    const int vis_count =
        std::min(static_cast<int>(visible_rows), static_cast<int>(items.size()) - scroll_offset);
    const Color text_col = pick_color(style.text_color, ctx_.theme_->text_color);
    const Color sel_color = pick_color(style.selected_color, ctx_.theme_->list_selected_color);

    for (int i = 0; i < vis_count; ++i) {
        const int item_idx = scroll_offset + i;
        if (item_idx < 0 || std::cmp_greater_equal(item_idx, items.size())) {
            break;
        }

        const float item_y = r.y + (static_cast<float>(i) * row_h);
        const Rect item_rect = {r.x, item_y, r.w, row_h};

        const std::string item_id = id_str + "##" + std::to_string(item_idx);
        ctx_.register_widget(item_id, item_rect);

        if (item_idx == selected) {
            ctx_.draw_rect(item_rect, sel_color, z_base_ + 0.06f);
        }

        if (ctx_.is_clicked(item_id)) {
            selected = item_idx;
            if (ctx_.frame_events_ != nullptr) {
                for (auto& e : *ctx_.frame_events_) {
                    if (e.type == EventType::MousePress && !e.consumed) {
                        e.consumed = true;
                        break;
                    }
                }
            }
        }

        ctx_.draw_text_at(items[item_idx], r.x + padding_, item_y + padding_, text_col,
                          z_base_ + 0.1f);
    }
}

bool PanelBuilder::text_input(std::string_view id, std::u8string& value, TextInputStyle style) {
    const float h = text_height() + (padding_ * 2);
    auto r = next_control_rect(h);
    const std::string id_str = make_widget_id(id);
    ctx_.register_widget(id_str, r);
    last_widget_id_ = id_str;

    bool is_focused = (ctx_.focused_input_id_ == id_str);
    bool submitted = false;

    if (ctx_.is_clicked(id_str)) {
        ctx_.focused_input_id_ = id_str;
        is_focused = true;
        ctx_.cursor_positions_[id_str] = value.size(); // byte offset
        if (ctx_.frame_events_ != nullptr) {
            for (auto& e : *ctx_.frame_events_) {
                if (e.type == EventType::MousePress && !e.consumed) {
                    e.consumed = true;
                    break;
                }
            }
        }
    }

    if (is_focused && ctx_.frame_events_ != nullptr) {
        size_t& cursor = ctx_.cursor_positions_[id_str];
        cursor = std::min(cursor, value.size());

        for (auto& e : *ctx_.frame_events_) {
            if (e.consumed) {
                continue;
            }
            if (e.type == EventType::KeyPress || e.type == EventType::KeyRepeat) {
                const Key k = e.key().key;
                if (k == Key::Enter) {
                    submitted = true;
                    ctx_.focused_input_id_.clear();
                    is_focused = false;
                    e.consumed = true;
                    break;
                } else if (k == Key::Escape) {
                    ctx_.focused_input_id_.clear();
                    is_focused = false;
                    e.consumed = true;
                    break;
                } else if (k == Key::Backspace) {
                    if (cursor > 0) {
                        // Step back over any UTF-8 continuation bytes (10xxxxxx).
                        do {
                            --cursor;
                        } while (cursor > 0 && (value[cursor] & 0xC0u) == 0x80u);
                        value.erase(cursor);
                    }
                    e.consumed = true;
                } else if (k == Key::Delete) {
                    if (cursor < value.size()) {
                        // Erase the codepoint starting at cursor.
                        size_t end = cursor + 1;
                        while (end < value.size() && (value[end] & 0xC0u) == 0x80u) {
                            ++end;
                        }
                        value.erase(cursor, end - cursor);
                    }
                    e.consumed = true;
                } else if (k == Key::Left) {
                    if (cursor > 0) {
                        do {
                            --cursor;
                        } while (cursor > 0 && (value[cursor] & 0xC0u) == 0x80u);
                    }
                    e.consumed = true;
                } else if (k == Key::Right) {
                    if (cursor < value.size()) {
                        ++cursor;
                        while (cursor < value.size() && (value[cursor] & 0xC0u) == 0x80u) {
                            ++cursor;
                        }
                    }
                    e.consumed = true;
                } else {
                    // Printable ASCII key — insert as char8_t (valid UTF-8 single byte).
                    const int kv = static_cast<int>(k);
                    if (kv >= static_cast<int>(Key::Space) && kv <= static_cast<int>(Key::Z)) {
                        auto ch = static_cast<char8_t>(kv);
                        if (kv >= static_cast<int>(Key::A) && kv <= static_cast<int>(Key::Z)) {
                            if (!e.key().mods.shift) {
                                ch = static_cast<char8_t>(ch + 32);
                            }
                        }
                        value.insert(value.begin() + static_cast<std::ptrdiff_t>(cursor), ch);
                        ++cursor;
                        e.consumed = true;
                    }
                }
            }
        }
    }

    const Color bg = is_focused ? pick_color(style.active_color, ctx_.theme_->input_active_color)
                                : pick_color(style.color, ctx_.theme_->input_color);
    ctx_.draw_rect(r, bg, z_base_ + 0.05f);
    if (ctx_.theme_->border_width > 0.0f) {
        ctx_.draw_border(r, ctx_.theme_->border_color, ctx_.theme_->border_width, z_base_ + 0.07f);
    }

    const Color text_col = pick_color(style.text_color, ctx_.theme_->text_color);
    if (is_focused) {
        size_t cursor = ctx_.cursor_positions_[id_str];
        cursor = std::min(cursor, value.size());
        std::u8string display = value;
        display.insert(cursor, 1, u8'|');
        ctx_.draw_text_at(display, r.x + padding_, r.y + padding_, text_col, z_base_ + 0.1f);
    } else {
        ctx_.draw_text_at(value, r.x + padding_, r.y + padding_, text_col, z_base_ + 0.1f);
    }

    return submitted;
}

void PanelBuilder::progress_bar(float value, float max, ProgressBarStyle style) {
    if (max <= 0.0f) {
        max = 1.0f;
    }
    float clamped = std::clamp(value, 0.0f, max);
    const float fraction = clamped / max;

    const float h = style.height > 0.0f ? style.height : text_height() + (padding_ * 2);
    auto r = next_control_rect(h);

    // Background (empty portion).
    const Color bg = pick_color(style.bg_color, ctx_.theme_->progress_bg_color);
    ctx_.draw_rect(r, bg, z_base_ + 0.05f);
    if (ctx_.theme_->border_width > 0.0f) {
        ctx_.draw_border(r, ctx_.theme_->border_color, ctx_.theme_->border_width, z_base_ + 0.07f);
    }

    // Filled portion.
    if (fraction > 0.0f) {
        const Color fill = pick_color(style.fill_color, ctx_.theme_->progress_fill_color);
        const Rect fill_rect = {r.x, r.y, r.w * fraction, r.h};
        ctx_.draw_rect(fill_rect, fill, z_base_ + 0.06f);
    }

    // Optional text overlay ("value / max").
    if (style.show_text) {
        // Format as integers if both are whole numbers, otherwise one decimal.
        std::u8string label;
        std::string s;
        if (value == std::floor(value) && max == std::floor(max)) {
            s = std::format("{} / {}", static_cast<int>(clamped), static_cast<int>(max));
        } else {
            s = std::format("{:.1f} / {:.1f}", clamped, max);
        }
        label = std::u8string(s.begin(), s.end());

        const float text_w = measure_text_width(label);
        const float text_x = std::floor(r.x + ((r.w - text_w) / 2.0f));
        const float text_y = std::floor(r.y + ((r.h - text_height()) / 2.0f));
        const Color text_col = pick_color(style.text_color, ctx_.theme_->progress_text_color);
        ctx_.draw_text_at(label, text_x, text_y, text_col, z_base_ + 0.1f);
    }
}

void PanelBuilder::separator(SeparatorStyle style) {
    const float thickness = style.thickness > 0.0f ? style.thickness : 1.0f;
    const float top_m = style.top_margin > 0.0f ? style.top_margin : margin_;
    const float bottom_m = style.bottom_margin > 0.0f ? style.bottom_margin : margin_;

    // Add extra top margin (subtract the default margin that next_control_rect adds).
    if (top_m > margin_) {
        cursor_y_ += top_m - margin_;
    }

    auto r = next_control_rect(thickness);

    const Color col = pick_color(style.color, ctx_.theme_->separator_color);
    ctx_.draw_rect(r, col, z_base_ + 0.05f);

    // Adjust cursor for custom bottom margin (next widget call will add its own margin_).
    if (bottom_m > margin_) {
        cursor_y_ += bottom_m - margin_;
    }
}

void PanelBuilder::message_log(std::string_view id, const MessageLog& log, MessageLogStyle style) {
    const float row_h = text_height() + padding_;
    auto visible_rows = static_cast<int>(style.visible_rows);
    const float total_h = row_h * static_cast<float>(visible_rows);
    auto r = next_control_rect(total_h);

    // Background.
    const Color bg = pick_color(style.bg_color, ctx_.theme_->msglog_bg_color);
    ctx_.draw_rect(r, bg, z_base_ + 0.05f);
    if (ctx_.theme_->border_width > 0.0f) {
        ctx_.draw_border(r, ctx_.theme_->border_color, ctx_.theme_->border_width, z_base_ + 0.07f);
    }

    if (log.empty()) {
        return;
    }

    const std::string id_str = make_widget_id(id);
    last_widget_id_ = id_str;
    int& scroll_offset = ctx_.scroll_offsets_[id_str];

    ctx_.register_widget(id_str, r);

    const int total_messages = static_cast<int>(log.size());
    const int max_scroll = std::max(0, total_messages - visible_rows);

    // Was the view pinned to the bottom last frame?  Default to true
    // for newly-seen widgets so they start scrolled to the bottom.
    auto [at_bottom_it, inserted] = ctx_.scroll_at_bottom_.try_emplace(id_str, true);
    bool& was_at_bottom = at_bottom_it->second;

    // Handle mouse scroll first so user input takes priority.
    bool user_scrolled = false;
    if (ctx_.is_hot(id_str) && ctx_.frame_events_ != nullptr) {
        for (auto& e : *ctx_.frame_events_) {
            if (e.type == EventType::MouseScroll && !e.consumed) {
                scroll_offset -= static_cast<int>(e.mouse_scroll().dy);
                user_scrolled = true;
                e.consumed = true;
                break;
            }
        }
    }

    // Auto-scroll: if the view was at the bottom last frame and the user
    // didn't just scroll away, snap to the new bottom.
    if (style.auto_scroll && !user_scrolled && was_at_bottom) {
        scroll_offset = max_scroll;
    }

    scroll_offset = std::clamp(scroll_offset, 0, max_scroll);

    // Remember whether we're at the bottom this frame for next frame's check.
    was_at_bottom = (scroll_offset >= max_scroll);

    // Draw visible messages.
    const int vis_count = std::min(visible_rows, total_messages - scroll_offset);
    for (int i = 0; i < vis_count; ++i) {
        const int msg_idx = scroll_offset + i;
        if (msg_idx < 0 || msg_idx >= total_messages) {
            break;
        }

        const auto& msg = log[static_cast<size_t>(msg_idx)];
        const float msg_y = r.y + (static_cast<float>(i) * row_h);

        // Convert LogColor to Color.
        const Color text_col = {msg.color.r, msg.color.g, msg.color.b, msg.color.a};
        ctx_.draw_text_at(msg.text, r.x + padding_, msg_y + (padding_ * 0.5f), text_col,
                          z_base_ + 0.1f);
    }

    // Draw a scrollbar track + thumb on the right edge when scrollable.
    if (max_scroll > 0) {
        const float bar_w = 3.0f;
        const float track_x = r.x + r.w - bar_w - 1.0f;

        // Track (subtle background).
        Color track_col = ctx_.theme_->separator_color;
        track_col.a = static_cast<uint8_t>(track_col.a / 2);
        const Rect track_rect = {track_x, r.y + 1.0f, bar_w, total_h - 2.0f};
        ctx_.draw_rect(track_rect, track_col, z_base_ + 0.08f);

        // Thumb (proportional size and position).
        const float content_h = total_h - 2.0f;
        const float thumb_h = std::max(8.0f, content_h * static_cast<float>(visible_rows) /
                                                 static_cast<float>(total_messages));
        const float thumb_travel = content_h - thumb_h;
        const float thumb_y = r.y + 1.0f +
                              (max_scroll > 0 ? thumb_travel * static_cast<float>(scroll_offset) /
                                                    static_cast<float>(max_scroll)
                                              : 0.0f);

        const Color thumb_col = ctx_.theme_->separator_color;
        const Rect thumb_rect = {track_x, thumb_y, bar_w, thumb_h};
        ctx_.draw_rect(thumb_rect, thumb_col, z_base_ + 0.09f);
    }
}

void PanelBuilder::tooltip(std::u8string_view text) {
    if (last_widget_id_.empty() || !ctx_.is_hot(last_widget_id_)) {
        return;
    }

    const float tip_w = measure_text_width(text) + (padding_ * 2);
    const float tip_h = text_height() + (padding_ * 2);

    // Position near the mouse, offset slightly down-right.
    float tip_x = ctx_.mouse_pos_.x + 12.0f;
    float tip_y = ctx_.mouse_pos_.y + 12.0f;

    // Clamp to screen bounds.
    const auto sw = static_cast<float>(ctx_.screen_w_);
    const auto sh = static_cast<float>(ctx_.screen_h_);
    if (tip_x + tip_w > sw) {
        tip_x = sw - tip_w;
    }
    if (tip_y + tip_h > sh) {
        tip_y = sh - tip_h;
    }

    const Rect tip_rect = {tip_x, tip_y, tip_w, tip_h};

    // Draw at a very high z so tooltips are always on top.
    const float tip_z = ctx_.theme_->z_order + 10.0f;
    ctx_.draw_rect(tip_rect, ctx_.theme_->bg_color, tip_z);
    if (ctx_.theme_->border_width > 0.0f) {
        ctx_.draw_border(tip_rect, ctx_.theme_->border_color, ctx_.theme_->border_width,
                         tip_z + 0.01f);
    }
    ctx_.draw_text_at(text, tip_x + padding_, tip_y + padding_, ctx_.theme_->text_color,
                      tip_z + 0.02f);
}

// --- Systems ---

void UIInputSystem::update(World& world, float /*dt*/) {
    auto& events = world.resource<EventQueue>().events;
    auto* renderer = world.resource<Renderer*>();
    auto& ui = world.resource<UIContext>();

    ui.ensure_white_texture(renderer->context());

    if (world.has_resource<UITheme>()) {
        ui.set_theme(&world.resource<UITheme>());
    }

    ui.begin_frame(events, *renderer);
}

void UIFlushSystem::draw(World& world, Renderer& renderer) {
    auto& ui = world.resource<UIContext>();
    ui.flush(renderer);
}

} // namespace xebble
