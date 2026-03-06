/// @file ui.cpp
/// @brief Immediate-mode UI system implementation.
#include <xebble/ui.hpp>
#include <xebble/world.hpp>
#include <xebble/spritesheet.hpp>
#include <xebble/ecs.hpp>

#include <algorithm>

namespace xebble {

// --- Helpers ---

static bool is_default_color(Color c) {
    return c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0;
}

static Color pick_color(Color style_color, Color theme_color) {
    return is_default_color(style_color) ? theme_color : style_color;
}

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
        if (e.consumed) continue;
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
                }
                break;
            case EventType::KeyPress:
            case EventType::KeyRepeat: {
                auto k = e.key().key;
                auto mods = e.key().mods;
                int kv = static_cast<int>(k);
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

    // Determine hot widget from prev_rects_
    hot_id_.clear();
    for (auto& wr : prev_rects_) {
        if (mouse_pos_.x >= wr.rect.x && mouse_pos_.x <= wr.rect.x + wr.rect.w &&
            mouse_pos_.y >= wr.rect.y && mouse_pos_.y <= wr.rect.y + wr.rect.h) {
            hot_id_ = wr.id;
            break;
        }
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
}

void UIContext::draw_rect(Rect rect, Color color, float z) {
    const Texture* tex = nullptr;
    float u = 0, v = 0, uw = 0, vh_val = 0;

    if (auto* bf = std::get_if<const BitmapFont*>(&theme_->font)) {
        if (*bf) {
            tex = &(*bf)->sheet().texture();
            auto gi = (*bf)->glyph_index('#');
            if (gi) {
                auto r = (*bf)->sheet().region(*gi);
                u = r.x + r.w * 0.5f;
                v = r.y + r.h * 0.3f;
                uw = r.w * 0.01f;
                vh_val = r.h * 0.01f;
            }
        }
    } else if (auto* f = std::get_if<const Font*>(&theme_->font)) {
        if (*f) {
            tex = &(*f)->texture();
            u = 0.0f; v = 0.0f;
            uw = 0.001f; vh_val = 0.001f;
        }
    }

    if (!tex) return;

    SpriteInstance inst{
        .pos_x = rect.x, .pos_y = rect.y,
        .uv_x = u, .uv_y = v, .uv_w = uw, .uv_h = vh_val,
        .quad_w = rect.w, .quad_h = rect.h,
        .r = static_cast<float>(color.r) / 255.0f,
        .g = static_cast<float>(color.g) / 255.0f,
        .b = static_cast<float>(color.b) / 255.0f,
        .a = static_cast<float>(color.a) / 255.0f,
    };
    batches_.push_back({{inst}, tex, z});
}

void UIContext::draw_text_at(std::string_view text, float x, float y, Color color, float z) {
    if (text.empty()) return;

    float cr = static_cast<float>(color.r) / 255.0f;
    float cg = static_cast<float>(color.g) / 255.0f;
    float cb = static_cast<float>(color.b) / 255.0f;
    float ca = static_cast<float>(color.a) / 255.0f;

    if (auto* bf = std::get_if<const BitmapFont*>(&theme_->font)) {
        if (!*bf) return;
        const Texture* tex = &(*bf)->sheet().texture();
        std::vector<SpriteInstance> instances;
        float gw = static_cast<float>((*bf)->glyph_width());
        float gh = static_cast<float>((*bf)->glyph_height());

        for (size_t i = 0; i < text.size(); ++i) {
            auto gi = (*bf)->glyph_index(text[i]);
            if (!gi) continue;
            auto r = (*bf)->sheet().region(*gi);
            instances.push_back(SpriteInstance{
                .pos_x = x + static_cast<float>(i) * gw,
                .pos_y = y,
                .uv_x = r.x, .uv_y = r.y, .uv_w = r.w, .uv_h = r.h,
                .quad_w = gw, .quad_h = gh,
                .r = cr, .g = cg, .b = cb, .a = ca,
            });
        }
        if (!instances.empty()) {
            batches_.push_back({std::move(instances), tex, z});
        }
    } else if (auto* f = std::get_if<const Font*>(&theme_->font)) {
        if (!*f) return;
        const Texture* tex = &(*f)->texture();
        std::vector<SpriteInstance> instances;
        float lh = (*f)->line_height();
        float cx = x;

        for (size_t i = 0; i < text.size(); ++i) {
            auto gm = (*f)->glyph(text[i]);
            if (!gm) continue;
            instances.push_back(SpriteInstance{
                .pos_x = cx + gm->bearing_x,
                .pos_y = y + lh - gm->bearing_y,
                .uv_x = gm->uv.x, .uv_y = gm->uv.y,
                .uv_w = gm->uv.w, .uv_h = gm->uv.h,
                .quad_w = gm->width, .quad_h = gm->height,
                .r = cr, .g = cg, .b = cb, .a = ca,
            });
            cx += gm->advance;
        }
        if (!instances.empty()) {
            batches_.push_back({std::move(instances), tex, z});
        }
    }
}

float UIContext::glyph_width() const {
    if (auto* bf = std::get_if<const BitmapFont*>(&theme_->font)) {
        if (*bf) return static_cast<float>((*bf)->glyph_width());
    } else if (auto* f = std::get_if<const Font*>(&theme_->font)) {
        if (*f) {
            auto gm = (*f)->glyph('M');
            if (gm) return gm->advance;
        }
    }
    return 8.0f;
}

float UIContext::glyph_height() const {
    if (auto* bf = std::get_if<const BitmapFont*>(&theme_->font)) {
        if (*bf) return static_cast<float>((*bf)->glyph_height());
    } else if (auto* f = std::get_if<const Font*>(&theme_->font)) {
        if (*f) return (*f)->line_height();
    }
    return 12.0f;
}

void UIContext::register_widget(std::string_view id, Rect rect) {
    curr_rects_.push_back({std::string(id), rect});
}

bool UIContext::is_hot(std::string_view id) const {
    return hot_id_ == id;
}

bool UIContext::is_clicked(std::string_view id) const {
    return is_hot(id) && mouse_clicked_;
}

// --- PanelBuilder ---

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

void PanelBuilder::text(std::string_view text, TextStyle style) {
    float h = text_height();
    auto r = next_control_rect(h);
    Color color = pick_color(style.color, ctx_.theme_->text_color);
    ctx_.draw_text_at(text, r.x, r.y, color, z_base_ + 0.1f);
}

bool PanelBuilder::button(std::string_view label, ButtonStyle style) {
    float h = text_height() + padding_ * 2;
    auto r = next_control_rect(h);
    std::string id = std::string(label);
    ctx_.register_widget(id, r);

    bool hovered = ctx_.is_hot(id);
    bool clicked = ctx_.is_clicked(id);

    Color bg = hovered
        ? pick_color(style.hover_color, ctx_.theme_->button_hover_color)
        : pick_color(style.color, ctx_.theme_->button_color);
    Color text_col = pick_color(style.text_color, ctx_.theme_->button_text_color);

    ctx_.draw_rect(r, bg, z_base_ + 0.05f);
    ctx_.draw_text_at(label, r.x + padding_, r.y + padding_, text_col, z_base_ + 0.1f);

    if (clicked && ctx_.frame_events_) {
        for (auto& e : *ctx_.frame_events_) {
            if (e.type == EventType::MousePress && !e.consumed) {
                e.consumed = true;
                break;
            }
        }
    }
    return clicked;
}

void PanelBuilder::checkbox(std::string_view label, bool& value, CheckboxStyle style) {
    float h = text_height() + padding_ * 2;
    auto r = next_control_rect(h);
    std::string id = std::string(label);
    ctx_.register_widget(id, r);

    bool clicked = ctx_.is_clicked(id);
    if (clicked) {
        value = !value;
    }

    // Draw background box (small square on the left)
    float box_size = text_height();
    Rect box_rect = {r.x + padding_, r.y + padding_, box_size, box_size};
    Color box_color = value
        ? pick_color(style.checked_color, ctx_.theme_->checkbox_checked_color)
        : pick_color(style.color, ctx_.theme_->checkbox_color);
    ctx_.draw_rect(box_rect, box_color, z_base_ + 0.05f);

    // Draw "X" indicator if checked
    if (value) {
        ctx_.draw_text_at("X", box_rect.x, box_rect.y, pick_color(style.text_color, ctx_.theme_->text_color), z_base_ + 0.1f);
    }

    // Draw label text to the right of the box
    float text_x = box_rect.x + box_size + padding_;
    Color text_col = pick_color(style.text_color, ctx_.theme_->text_color);
    ctx_.draw_text_at(label, text_x, r.y + padding_, text_col, z_base_ + 0.1f);

    // Consume click event
    if (clicked && ctx_.frame_events_) {
        for (auto& e : *ctx_.frame_events_) {
            if (e.type == EventType::MousePress && !e.consumed) {
                e.consumed = true;
                break;
            }
        }
    }
}

void PanelBuilder::list(std::string_view id, std::span<const std::string> items,
                         int& selected, ListStyle style) {
    float row_h = text_height() + padding_ * 2;
    float visible_rows = style.visible_rows;
    float total_h = row_h * visible_rows;
    auto r = next_control_rect(total_h);

    // Draw background
    Color bg = pick_color(style.color, ctx_.theme_->list_color);
    ctx_.draw_rect(r, bg, z_base_ + 0.05f);

    // Manage scroll state
    std::string id_str(id);
    int& scroll_offset = ctx_.scroll_offsets_[id_str];

    // Handle scroll events
    // Register the whole list rect for scroll detection
    ctx_.register_widget(id_str, r);

    if (ctx_.is_hot(id_str) && ctx_.frame_events_) {
        for (auto& e : *ctx_.frame_events_) {
            if (e.type == EventType::MouseScroll && !e.consumed) {
                scroll_offset -= static_cast<int>(e.mouse_scroll().dy);
                // Clamp scroll offset
                int max_scroll = std::max(0, static_cast<int>(items.size()) - static_cast<int>(visible_rows));
                scroll_offset = std::clamp(scroll_offset, 0, max_scroll);
                e.consumed = true;
                break;
            }
        }
    }

    // Clamp scroll offset in case items changed
    int max_scroll = std::max(0, static_cast<int>(items.size()) - static_cast<int>(visible_rows));
    scroll_offset = std::clamp(scroll_offset, 0, max_scroll);

    // Draw visible items
    int vis_count = std::min(static_cast<int>(visible_rows), static_cast<int>(items.size()) - scroll_offset);
    Color text_col = pick_color(style.text_color, ctx_.theme_->text_color);
    Color sel_color = pick_color(style.selected_color, ctx_.theme_->list_selected_color);

    for (int i = 0; i < vis_count; ++i) {
        int item_idx = scroll_offset + i;
        if (item_idx < 0 || item_idx >= static_cast<int>(items.size())) break;

        float item_y = r.y + static_cast<float>(i) * row_h;
        Rect item_rect = {r.x, item_y, r.w, row_h};

        // Register each item as clickable
        std::string item_id = id_str + "##" + std::to_string(item_idx);
        ctx_.register_widget(item_id, item_rect);

        // Highlight selected item
        if (item_idx == selected) {
            ctx_.draw_rect(item_rect, sel_color, z_base_ + 0.06f);
        }

        // Handle click to select
        if (ctx_.is_clicked(item_id)) {
            selected = item_idx;
            if (ctx_.frame_events_) {
                for (auto& e : *ctx_.frame_events_) {
                    if (e.type == EventType::MousePress && !e.consumed) {
                        e.consumed = true;
                        break;
                    }
                }
            }
        }

        // Draw item text
        ctx_.draw_text_at(items[item_idx], r.x + padding_, item_y + padding_, text_col, z_base_ + 0.1f);
    }
}

bool PanelBuilder::text_input(std::string_view id, std::string& value, TextInputStyle style) {
    float h = text_height() + padding_ * 2;
    auto r = next_control_rect(h);
    std::string id_str(id);
    ctx_.register_widget(id_str, r);

    bool is_focused = (ctx_.focused_input_id_ == id_str);
    bool submitted = false;

    // Click to focus
    if (ctx_.is_clicked(id_str)) {
        ctx_.focused_input_id_ = id_str;
        is_focused = true;
        // Initialize cursor position
        ctx_.cursor_positions_[id_str] = value.size();
        if (ctx_.frame_events_) {
            for (auto& e : *ctx_.frame_events_) {
                if (e.type == EventType::MousePress && !e.consumed) {
                    e.consumed = true;
                    break;
                }
            }
        }
    }

    // Handle key input when focused
    if (is_focused && ctx_.frame_events_) {
        size_t& cursor = ctx_.cursor_positions_[id_str];
        if (cursor > value.size()) cursor = value.size();

        for (auto& e : *ctx_.frame_events_) {
            if (e.consumed) continue;
            if (e.type == EventType::KeyPress || e.type == EventType::KeyRepeat) {
                Key k = e.key().key;
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
                        value.erase(cursor - 1, 1);
                        cursor--;
                    }
                    e.consumed = true;
                } else if (k == Key::Delete) {
                    if (cursor < value.size()) {
                        value.erase(cursor, 1);
                    }
                    e.consumed = true;
                } else if (k == Key::Left) {
                    if (cursor > 0) cursor--;
                    e.consumed = true;
                } else if (k == Key::Right) {
                    if (cursor < value.size()) cursor++;
                    e.consumed = true;
                } else {
                    // Check for printable character
                    int kv = static_cast<int>(k);
                    if (kv >= static_cast<int>(Key::Space) && kv <= static_cast<int>(Key::Z)) {
                        char ch = static_cast<char>(kv);
                        if (kv >= static_cast<int>(Key::A) && kv <= static_cast<int>(Key::Z)) {
                            if (!e.key().mods.shift) {
                                ch = static_cast<char>(ch + 32);
                            }
                        }
                        value.insert(value.begin() + static_cast<std::ptrdiff_t>(cursor), ch);
                        cursor++;
                        e.consumed = true;
                    }
                }
            }
        }
    }

    // Draw background
    Color bg = is_focused
        ? pick_color(style.active_color, ctx_.theme_->input_active_color)
        : pick_color(style.color, ctx_.theme_->input_color);
    ctx_.draw_rect(r, bg, z_base_ + 0.05f);

    // Draw text with cursor
    Color text_col = pick_color(style.text_color, ctx_.theme_->text_color);
    if (is_focused) {
        size_t cursor = ctx_.cursor_positions_[id_str];
        if (cursor > value.size()) cursor = value.size();
        std::string display = value;
        display.insert(cursor, "|");
        ctx_.draw_text_at(display, r.x + padding_, r.y + padding_, text_col, z_base_ + 0.1f);
    } else {
        ctx_.draw_text_at(value, r.x + padding_, r.y + padding_, text_col, z_base_ + 0.1f);
    }

    return submitted;
}

// --- Systems ---

void UIInputSystem::update(World& world, float) {
    auto& events = world.resource<EventQueue>().events;
    auto* renderer = world.resource<Renderer*>();
    auto& ui = world.resource<UIContext>();

    if (world.has_resource<UITheme>()) {
        ui.theme_ = &world.resource<UITheme>();
    }

    ui.begin_frame(events, *renderer);
}

void UIFlushSystem::draw(World& world, Renderer& renderer) {
    auto& ui = world.resource<UIContext>();
    ui.flush(renderer);
}

} // namespace xebble
