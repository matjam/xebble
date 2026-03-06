/// @file ui.hpp
/// @brief Immediate-mode UI system — panels, controls, and theming.
#pragma once

#include <xebble/types.hpp>
#include <xebble/event.hpp>
#include <xebble/renderer.hpp>
#include <xebble/font.hpp>
#include <xebble/system.hpp>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace xebble {

class World;

enum class Anchor {
    TopLeft, Top, TopRight,
    Left, Center, Right,
    BottomLeft, Bottom, BottomRight,
};

struct PanelPlacement {
    Anchor anchor = Anchor::TopLeft;
    Vec2 size = {};
    Vec2 offset = {};
};

struct TextStyle {
    Color color = {0, 0, 0, 0};
};

struct ButtonStyle {
    Color color = {0, 0, 0, 0};
    Color hover_color = {0, 0, 0, 0};
    Color text_color = {0, 0, 0, 0};
};

struct CheckboxStyle {
    Color color = {0, 0, 0, 0};
    Color checked_color = {0, 0, 0, 0};
    Color text_color = {0, 0, 0, 0};
};

struct ListStyle {
    Color color = {0, 0, 0, 0};
    Color selected_color = {0, 0, 0, 0};
    Color text_color = {0, 0, 0, 0};
    float visible_rows = 8;
};

struct TextInputStyle {
    Color color = {0, 0, 0, 0};
    Color active_color = {0, 0, 0, 0};
    Color text_color = {0, 0, 0, 0};
};

struct UITheme {
    std::variant<const BitmapFont*, const Font*> font = static_cast<const BitmapFont*>(nullptr);
    Color bg_color = {20, 20, 30, 220};
    Color text_color = {200, 200, 200, 255};
    Color button_color = {60, 60, 80, 255};
    Color button_hover_color = {80, 80, 110, 255};
    Color button_text_color = {230, 230, 230, 255};
    Color checkbox_color = {60, 60, 80, 255};
    Color checkbox_checked_color = {100, 180, 100, 255};
    Color input_color = {40, 40, 50, 255};
    Color input_active_color = {50, 50, 70, 255};
    Color list_color = {40, 40, 50, 255};
    Color list_selected_color = {70, 70, 100, 255};
    float padding = 4.0f;
    float margin = 2.0f;
    float z_order = 100.0f;
};

class UIContext;

class PanelBuilder {
public:
    PanelBuilder(UIContext& ctx, Rect panel_rect, float z_base);

    void text(std::string_view text, TextStyle style = {});
    bool button(std::string_view label, ButtonStyle style = {});
    void checkbox(std::string_view label, bool& value, CheckboxStyle style = {});
    void list(std::string_view id, std::span<const std::string> items,
              int& selected, ListStyle style = {});
    bool text_input(std::string_view id, std::string& value, TextInputStyle style = {});

    template<typename Fn>
    void horizontal(Fn&& fn) {
        float saved_y = cursor_y_;
        float saved_x = content_x_;
        float saved_w = content_width_;
        in_horizontal_ = true;
        horiz_cursor_x_ = content_x_;
        horiz_max_h_ = 0;
        fn(*this);
        in_horizontal_ = false;
        cursor_y_ = saved_y + horiz_max_h_ + margin_;
        content_x_ = saved_x;
        content_width_ = saved_w;
    }

private:
    friend class UIContext;

    Rect next_control_rect(float height);
    float text_height() const;
    float measure_text_width(std::string_view text) const;

    UIContext& ctx_;
    Rect panel_rect_;
    float z_base_;
    float cursor_y_;
    float content_x_;
    float content_width_;
    float padding_;
    float margin_;
    bool in_horizontal_ = false;
    float horiz_cursor_x_ = 0;
    float horiz_max_h_ = 0;
};

class UIContext {
public:
    UIContext();
    ~UIContext();

    void begin_frame(std::vector<Event>& events, const Renderer& renderer);

    template<typename Fn>
    void panel(std::string_view id, PanelPlacement placement, Fn&& fn) {
        auto rect = resolve_placement(placement);
        draw_panel_bg(rect);

        float z = theme_->z_order;
        PanelBuilder builder(*this, rect, z);
        fn(builder);
    }

    void flush(Renderer& renderer);

private:
    friend class PanelBuilder;

    Rect resolve_placement(const PanelPlacement& p) const;
    void draw_panel_bg(Rect rect);
    void draw_rect(Rect rect, Color color, float z);
    void draw_text_at(std::string_view text, float x, float y, Color color, float z);
    float glyph_width() const;
    float glyph_height() const;

    void register_widget(std::string_view id, Rect rect);
    bool is_hot(std::string_view id) const;
    bool is_clicked(std::string_view id) const;

    const UITheme* theme_ = nullptr;
    uint32_t screen_w_ = 0;
    uint32_t screen_h_ = 0;
    Vec2 mouse_pos_ = {};
    bool mouse_clicked_ = false;
    bool mouse_down_ = false;

    std::string hot_id_;
    std::string active_id_;
    std::string focused_input_id_;

    struct WidgetRect {
        std::string id;
        Rect rect;
    };
    std::vector<WidgetRect> prev_rects_;
    std::vector<WidgetRect> curr_rects_;

    std::unordered_map<std::string, int> scroll_offsets_;
    std::unordered_map<std::string, size_t> cursor_positions_;
    std::vector<char> input_chars_;

    struct DrawBatch {
        std::vector<SpriteInstance> instances;
        const Texture* texture;
        float z_order;
    };
    std::vector<DrawBatch> batches_;

    std::vector<Event>* frame_events_ = nullptr;
};

class UIInputSystem : public System {
public:
    void update(World& world, float dt) override;
};

class UIFlushSystem : public System {
public:
    void draw(World& world, Renderer& renderer) override;
};

/// @brief Resolve a PanelPlacement to a screen Rect.
Rect resolve_panel_placement(const PanelPlacement& p, uint32_t screen_w, uint32_t screen_h);

} // namespace xebble
