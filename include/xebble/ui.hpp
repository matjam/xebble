/// @file ui.hpp
/// @brief Immediate-mode UI system — panels, controls, and theming.
///
/// Xebble's UI layer is an **immediate-mode** system: there is no persistent
/// widget tree. Each frame you describe the interface by calling layout
/// functions inside a `UIContext::panel()` lambda. The context tracks hot/active
/// widget state internally and produces draw calls that are flushed to the
/// renderer by `UIFlushSystem` at the end of the frame.
///
/// ## Quick-start: a pause menu
///
/// @code
/// #include <xebble/ui.hpp>
/// using namespace xebble;
///
/// // In your UI system's draw() method:
/// void PauseMenuSystem::draw(World& world, Renderer& renderer) {
///     auto& ui = world.resource<UIContext>();
///
///     ui.panel("pause_menu",
///         PanelPlacement{ .anchor = Anchor::Center,
///                         .size   = {300.0f, 200.0f} },
///         [](PanelBuilder& p) {
///             p.text("-- PAUSED --");
///
///             if (p.button("Resume")) { resume_game(); }
///             if (p.button("Save"))   { save_game();   }
///             if (p.button("Quit"))   { quit_game();   }
///         });
/// }
/// @endcode
///
/// ## Theming
///
/// Customise colours and spacing via `UITheme`. A `UITheme` component on any
/// entity (or stored as a resource) is picked up by `UIInputSystem` and
/// applied globally.
///
/// @code
/// UITheme dark_dungeon{
///     .bg_color            = {10, 10, 20, 230},
///     .text_color          = {200, 180, 100, 255},   // parchment yellow
///     .button_color        = {40,  30,  60, 255},
///     .button_hover_color  = {70,  50,  90, 255},
///     .button_text_color   = {220, 200, 140, 255},
///     .padding             = 6.0f,
///     .margin              = 3.0f,
/// };
/// @endcode
///
/// ## System integration
///
/// Register the two built-in UI systems when building your World.
/// `UIInputSystem` must run **before** game logic systems so mouse state is
/// fresh, and `UIFlushSystem` must run **last** so UI is drawn on top.
///
/// @code
/// world.add_system<UIInputSystem>();
/// world.add_system<MyGameSystems>();
/// world.add_system<UIFlushSystem>();
/// @endcode
#pragma once

#include <xebble/event.hpp>
#include <xebble/font.hpp>
#include <xebble/msglog.hpp>
#include <xebble/renderer.hpp>
#include <xebble/system.hpp>
#include <xebble/types.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace xebble {

class World;

/// @brief Screen anchor point for panel placement.
///
/// Defines which corner or edge of the screen (or the centre) a panel is
/// positioned relative to. Used together with `PanelPlacement::offset` to
/// nudge the panel away from the anchor point.
///
/// ```
/// TopLeft ──── Top ──── TopRight
///    │                      │
///   Left     Center        Right
///    │                      │
/// BottomLeft─ Bottom─BottomRight
/// ```
enum class Anchor {
    TopLeft,     ///< Top-left corner of the screen.
    Top,         ///< Horizontally centred, pinned to the top edge.
    TopRight,    ///< Top-right corner of the screen.
    Left,        ///< Vertically centred, pinned to the left edge.
    Center,      ///< Centred both horizontally and vertically.
    Right,       ///< Vertically centred, pinned to the right edge.
    BottomLeft,  ///< Bottom-left corner of the screen.
    Bottom,      ///< Horizontally centred, pinned to the bottom edge.
    BottomRight, ///< Bottom-right corner of the screen.
};

/// @brief Describes where and how large a UI panel should be.
///
/// The final on-screen rectangle is derived from the anchor, the panel size,
/// and an optional pixel offset that shifts the panel away from the anchor.
///
/// @code
/// // A 320×240 panel centred on screen, shifted up by 40 pixels.
/// PanelPlacement{ .anchor = Anchor::Center,
///                 .size   = {320.0f, 240.0f},
///                 .offset = {0.0f, -40.0f} }
///
/// // A 200×300 inventory panel pinned to the right edge.
/// PanelPlacement{ .anchor = Anchor::Right,
///                 .size   = {200.0f, 300.0f},
///                 .offset = {-8.0f, 0.0f} }  // 8px gap from right edge
/// @endcode
struct PanelPlacement {
    Anchor anchor = Anchor::TopLeft; ///< Screen anchor point.
    Vec2 size = {};                  ///< Panel size in pixels {width, height}.
    Vec2 offset = {};                ///< Pixel offset from the anchor point.
};

/// @brief Style overrides for a text label widget.
///
/// When the default colour is `{0,0,0,0}` the theme's `text_color` is used.
struct TextStyle {
    Color color = {0, 0, 0, 0}; ///< Text colour override; {0,0,0,0} = use theme default.
};

/// @brief Style overrides for a button widget.
///
/// Any field left at `{0,0,0,0}` inherits the corresponding theme colour.
///
/// @code
/// ButtonStyle danger_btn{
///     .color      = {120, 30, 30, 255},
///     .hover_color= {180, 50, 50, 255},
///     .text_color = {255, 220, 220, 255},
/// };
/// if (p.button("Delete Save", danger_btn)) { delete_save_file(); }
/// @endcode
struct ButtonStyle {
    Color color = {0, 0, 0, 0};         ///< Normal button background colour.
    Color hover_color = {0, 0, 0, 0};   ///< Background colour when the cursor hovers.
    Color pressed_color = {0, 0, 0, 0}; ///< Background colour when mouse is held down.
    Color text_color = {0, 0, 0, 0};    ///< Button label text colour.
    float width = 0.0f;                 ///< Fixed width in pixels; 0 = fill panel content width.
};

/// @brief Style overrides for a checkbox widget.
///
/// Any field left at `{0,0,0,0}` inherits from the active theme.
struct CheckboxStyle {
    Color color = {0, 0, 0, 0};         ///< Unchecked box background colour.
    Color checked_color = {0, 0, 0, 0}; ///< Checked box background colour.
    Color text_color = {0, 0, 0, 0};    ///< Label text colour.
};

/// @brief Style overrides for a radio button widget.
///
/// Any field left at `{0,0,0,0}` inherits from the active theme.
struct RadioButtonStyle {
    Color color = {0, 0, 0, 0};          ///< Unselected dot background colour.
    Color selected_color = {0, 0, 0, 0}; ///< Selected dot background colour.
    Color text_color = {0, 0, 0, 0};     ///< Label text colour.
};

/// @brief Style overrides for a scrollable list widget.
///
/// Any field left at `{0,0,0,0}` inherits from the active theme.
struct ListStyle {
    Color color = {0, 0, 0, 0};          ///< Row background colour.
    Color selected_color = {0, 0, 0, 0}; ///< Highlighted row background colour.
    Color text_color = {0, 0, 0, 0};     ///< Row text colour.
    float visible_rows = 8;              ///< Number of rows visible without scrolling.
};

/// @brief Style overrides for a single-line text input widget.
///
/// Any field left at `{0,0,0,0}` inherits from the active theme.
struct TextInputStyle {
    Color color = {0, 0, 0, 0};        ///< Background colour when not focused.
    Color active_color = {0, 0, 0, 0}; ///< Background colour when focused.
    Color text_color = {0, 0, 0, 0};   ///< Input text colour.
};

/// @brief Style overrides for a progress bar widget.
///
/// Any field left at `{0,0,0,0}` inherits from the active theme.
///
/// @code
/// // Red health bar.
/// p.progress_bar(hp, max_hp, ProgressBarStyle{
///     .fill_color = {180, 40, 40, 255},
///     .bg_color   = {40,  20, 20, 255},
/// });
/// @endcode
struct ProgressBarStyle {
    Color fill_color = {0, 0, 0, 0}; ///< Filled portion colour.
    Color bg_color = {0, 0, 0, 0};   ///< Empty portion background colour.
    Color text_color = {0, 0, 0, 0}; ///< Optional overlay text colour.
    float height = 0.0f;             ///< Bar height in pixels; 0 = use glyph height + padding.
    bool show_text = false;          ///< If true, draw "value / max" centered on the bar.
};

/// @brief Style overrides for a horizontal slider widget.
///
/// Any field left at `{0,0,0,0}` inherits from the active theme.
///
/// @code
/// static float volume = 0.75f;
/// p.slider("volume", u8"Volume", volume, 0.0f, 1.0f);
/// @endcode
struct SliderStyle {
    Color track_color = {0, 0, 0, 0}; ///< Background track colour.
    Color thumb_color = {0, 0, 0, 0}; ///< Thumb/handle colour.
    Color text_color = {0, 0, 0, 0};  ///< Label text colour.
    bool show_value = true;           ///< If true, display the current value as text.
};

/// @brief Style overrides for a horizontal separator widget.
///
/// Any field left at `{0,0,0,0}` inherits from the active theme.
struct SeparatorStyle {
    Color color = {0, 0, 0, 0}; ///< Line colour; {0,0,0,0} = use theme text colour at half alpha.
    float thickness = 0.0f;     ///< Line thickness in pixels; 0 = 1 pixel.
    float top_margin = 0.0f;    ///< Extra space above the separator; 0 = use theme margin.
    float bottom_margin = 0.0f; ///< Extra space below the separator; 0 = use theme margin.
};

/// @brief Style overrides for a message log widget.
///
/// Any field left at `{0,0,0,0}` inherits from the active theme.
///
/// @code
/// p.message_log("combat_log", log, MessageLogStyle{
///     .visible_rows = 6,
///     .bg_color = {20, 20, 30, 200},
/// });
/// @endcode
struct MessageLogStyle {
    Color bg_color = {0, 0, 0, 0}; ///< Background colour for the log area.
    float visible_rows = 8;        ///< Number of text rows visible at once.
    bool auto_scroll = true;       ///< Automatically scroll to show the newest message.
};

/// @brief Visual theme controlling colours, spacing, and z-ordering for all UI.
///
/// Assign a theme to `UIContext` (via `set_theme()` or through
/// `UIInputSystem`'s ECS resource lookup) to apply it globally. Every widget
/// falls back to theme values when its per-widget style has `{0,0,0,0}`.
///
/// @code
/// UITheme fantasy_theme{
///     .bg_color           = {20, 15, 10, 240},   // very dark brown
///     .text_color         = {230, 210, 150, 255}, // aged parchment
///     .button_color       = {50,  35,  20, 255},
///     .button_hover_color = {80,  60,  30, 255},
///     .button_text_color  = {240, 220, 160, 255},
///     .checkbox_color         = {50,  35,  20, 255},
///     .checkbox_checked_color = {100, 160,  60, 255},
///     .input_color        = {30,  22,  12, 255},
///     .input_active_color = {45,  32,  18, 255},
///     .list_color         = {30,  22,  12, 255},
///     .list_selected_color= {70,  50,  25, 255},
///     .padding            = 5.0f,
///     .margin             = 3.0f,
///     .z_order            = 100.0f,
/// };
/// @endcode
struct UITheme {
    /// Font used to render all UI text. Can be a `BitmapFont*` (fixed-size,
    /// roguelike-grid style) or a `Font*` (TrueType, scalable).
    std::variant<const BitmapFont*, const Font*> font = static_cast<const BitmapFont*>(nullptr);

    Color bg_color = {20, 20, 30, 220};                  ///< Panel background fill colour.
    Color border_color = {100, 100, 130, 200};           ///< Border colour for panels and controls.
    float border_width = 1.0f;                           ///< Border thickness in pixels.
    Color text_color = {200, 200, 200, 255};             ///< Default label/text colour.
    Color button_color = {60, 60, 80, 255};              ///< Button background (normal).
    Color button_hover_color = {80, 80, 110, 255};       ///< Button background (hovered).
    Color button_pressed_color = {40, 40, 60, 255};      ///< Button background (mouse held down).
    Color button_text_color = {230, 230, 230, 255};      ///< Button label colour.
    Color checkbox_color = {60, 60, 80, 255};            ///< Checkbox background (unchecked).
    Color checkbox_checked_color = {100, 180, 100, 255}; ///< Checkbox background (checked).
    Color radio_color = {60, 60, 80, 255};               ///< Radio button background (unselected).
    Color radio_selected_color = {100, 180, 100, 255};   ///< Radio button background (selected).
    Color slider_track_color = {40, 40, 50, 255};        ///< Slider track background.
    Color slider_thumb_color = {100, 100, 140, 255};     ///< Slider thumb/handle.
    Color input_color = {40, 40, 50, 255};               ///< Text input background (inactive).
    Color input_active_color = {50, 50, 70, 255};        ///< Text input background (focused).
    Color list_color = {40, 40, 50, 255};                ///< List row background.
    Color list_selected_color = {70, 70, 100, 255};      ///< Selected list row background.
    Color progress_fill_color = {80, 160, 80, 255};      ///< Progress bar filled portion.
    Color progress_bg_color = {40, 40, 50, 255};         ///< Progress bar empty portion.
    Color progress_text_color = {230, 230, 230, 255};    ///< Progress bar overlay text.
    Color separator_color = {100, 100, 120, 128};        ///< Horizontal separator line.
    Color msglog_bg_color = {30, 30, 40, 200};           ///< Message log background.
    float padding = 6.0f;                                ///< Inner padding within a panel (px).
    float margin = 4.0f;                                 ///< Vertical gap between widgets (px).
    float z_order = 100.0f;                              ///< Base draw depth for all UI elements.
};

class UIContext;

/// @brief Fluent builder for laying out widgets inside a panel.
///
/// `PanelBuilder` is passed by reference into every `UIContext::panel()`
/// callback. It maintains a vertical cursor that advances after each widget,
/// producing a top-to-bottom stacked layout.
///
/// Use `horizontal()` to arrange multiple widgets side-by-side on one row.
///
/// @code
/// ui.panel("hud",
///     PanelPlacement{ .anchor = Anchor::TopLeft, .size = {200.0f, 150.0f} },
///     [](PanelBuilder& p) {
///         p.text("Hero Status");
///
///         // Two buttons on the same row.
///         p.horizontal([](PanelBuilder& row) {
///             if (row.button("Use Potion")) { use_potion(); }
///             if (row.button("Cast Spell")) { cast_spell(); }
///         });
///
///         // Toggle sound in-game.
///         static bool sound_on = true;
///         p.checkbox("Sound", sound_on);
///     });
/// @endcode
class PanelBuilder {
public:
    PanelBuilder(UIContext& ctx, Rect panel_rect, float z_base, std::string_view panel_id);

    /// @brief Draw a static text label.
    ///
    /// The label is drawn at the current cursor position and the cursor
    /// advances by one text line.
    ///
    /// @code
    /// p.text("Floor 3 – The Iron Mines");
    /// p.text("HP: 28 / 40", TextStyle{ .color = {200, 80, 80, 255} });
    /// @endcode
    void text(std::u8string_view text, TextStyle style = {});

    /// @brief Draw a clickable button and return `true` on the frame it is clicked.
    ///
    /// Buttons automatically highlight when hovered. The return value is `true`
    /// for exactly one frame when the user clicks the button.
    ///
    /// @code
    /// if (p.button("New Game")) { start_new_game(); }
    /// if (p.button("Load"))     { show_load_dialog(); }
    /// if (p.button("Quit",  ButtonStyle{ .color = {100, 30, 30, 255} }))
    ///     request_quit();
    /// @endcode
    [[nodiscard]] bool button(std::u8string_view label, ButtonStyle style = {});

    /// @brief Draw a labelled toggle checkbox. Modifies @p value in place.
    ///
    /// @code
    /// static bool show_fps = false;
    /// p.checkbox("Show FPS", show_fps);
    ///
    /// static bool fullscreen = false;
    /// p.checkbox("Fullscreen", fullscreen,
    ///            CheckboxStyle{ .checked_color = {80, 150, 80, 255} });
    /// @endcode
    void checkbox(std::u8string_view label, bool& value, CheckboxStyle style = {});

    /// @brief Draw a labelled radio button. Sets @p selected to @p value when clicked.
    ///
    /// Multiple radio buttons sharing the same `int& selected` form a group —
    /// only the one whose @p value matches @p selected is shown as active.
    ///
    /// @code
    /// static int difficulty = 1;
    /// p.radio_button(u8"Easy",   difficulty, 0);
    /// p.radio_button(u8"Normal", difficulty, 1);
    /// p.radio_button(u8"Hard",   difficulty, 2);
    /// @endcode
    void radio_button(std::u8string_view label, int& selected, int value,
                      RadioButtonStyle style = {});

    /// @brief Draw a horizontal slider for adjusting a floating-point value.
    ///
    /// Click or drag anywhere on the track to set the value. The slider
    /// responds to mouse-down dragging via `active_id_`.
    ///
    /// @param id     Unique string identifier.
    /// @param label  Text label drawn to the left of the slider.
    /// @param value  Current value (modified in place, clamped to [min, max]).
    /// @param min    Minimum value.
    /// @param max    Maximum value.
    /// @param style  Visual overrides.
    ///
    /// @code
    /// static float volume = 0.75f;
    /// p.slider("vol", u8"Volume", volume, 0.0f, 1.0f);
    /// @endcode
    void slider(std::string_view id, std::u8string_view label, float& value, float min, float max,
                SliderStyle style = {});

    /// @brief Draw a scrollable list of string items.
    ///
    /// @p id      Unique string identifier for scroll/selection state.
    /// @p items   All items to display (may exceed visible_rows).
    /// @p selected Index of the currently selected item (modified in place).
    ///
    /// @code
    /// static std::vector<std::string> saves = load_save_names();
    /// static int selected_save = 0;
    /// p.list("save_list", saves, selected_save);
    ///
    /// if (p.button("Load Selected")) {
    ///     load_game(saves[selected_save]);
    /// }
    /// @endcode
    void list(std::string_view id, std::span<const std::u8string> items, int& selected,
              ListStyle style = {});

    /// @brief Draw a single-line editable text input. Returns `true` when Enter is pressed.
    ///
    /// @p id     Unique string identifier for cursor/focus state.
    /// @p value  The current string contents (modified in place as the user types).
    ///
    /// @code
    /// static std::string player_name;
    /// if (p.text_input("name_input", player_name)) {
    ///     // User pressed Enter — confirm the name.
    ///     start_game_with_name(player_name);
    /// }
    /// @endcode
    [[nodiscard]] bool text_input(std::string_view id, std::u8string& value,
                                  TextInputStyle style = {});

    /// @brief Draw a horizontal progress bar showing value out of max.
    ///
    /// Useful for health bars, XP bars, loading indicators, and any other
    /// value/max display. Optionally overlays "value / max" text on the bar.
    ///
    /// @param value  Current value (clamped to [0, max]).
    /// @param max    Maximum value; must be > 0.
    /// @param style  Visual overrides.
    ///
    /// @code
    /// p.progress_bar(player_hp, player_max_hp);
    /// p.progress_bar(xp, xp_to_level, ProgressBarStyle{
    ///     .fill_color = {100, 100, 200, 255},
    ///     .show_text  = true,
    /// });
    /// @endcode
    void progress_bar(float value, float max, ProgressBarStyle style = {});

    /// @brief Draw a horizontal separator line between widget groups.
    ///
    /// @code
    /// p.text("Character Stats");
    /// p.separator();
    /// p.text("STR: 18");
    /// p.text("DEX: 14");
    /// @endcode
    void separator(SeparatorStyle style = {});

    /// @brief Draw a scrollable message log view.
    ///
    /// Displays the most recent messages from a `MessageLog`, with per-message
    /// colors preserved. Supports mouse-scroll to view older messages.
    ///
    /// @param id     Unique string identifier for scroll state.
    /// @param log    The MessageLog to display.
    /// @param style  Visual overrides.
    ///
    /// @code
    /// auto& log = world.resource<MessageLog>();
    /// p.message_log("game_log", log);
    /// @endcode
    void message_log(std::string_view id, const MessageLog& log, MessageLogStyle style = {});

    /// @brief Show a tooltip for the preceding widget when it is hovered.
    ///
    /// Call immediately after the widget you want to annotate. The tooltip
    /// is drawn near the mouse cursor and is always on top.
    ///
    /// @code
    /// p.checkbox(u8"VSync", vsync);
    /// p.tooltip(u8"Synchronise frame rate to monitor refresh.");
    ///
    /// if (p.button(u8"Quit")) { std::exit(0); }
    /// p.tooltip(u8"Exit the application.");
    /// @endcode
    void tooltip(std::u8string_view text);

    /// @brief Lay out child widgets horizontally on a single row.
    ///
    /// All widgets added inside @p fn are placed left-to-right. After the
    /// lambda returns the cursor advances past the tallest widget in the row.
    ///
    /// @code
    /// p.horizontal([](PanelBuilder& row) {
    ///     if (row.button("Attack")) { attack(); }
    ///     if (row.button("Defend")) { defend(); }
    ///     if (row.button("Flee"))   { flee();   }
    /// });
    /// @endcode
    template<typename Fn>
    void horizontal(Fn&& fn) {
        const float saved_y = cursor_y_;
        const float saved_x = content_x_;
        const float saved_w = content_width_;
        in_horizontal_ = true;
        horiz_cursor_x_ = content_x_;
        horiz_max_h_ = 0;
        std::forward<Fn>(fn)(*this);
        in_horizontal_ = false;
        cursor_y_ = saved_y + horiz_max_h_ + margin_;
        content_x_ = saved_x;
        content_width_ = saved_w;
    }

private:
    friend class UIContext;

    Rect next_control_rect(float height);
    // After next_control_rect() in horizontal mode, call this to correct the
    // cursor advance when the widget's actual width differs from content_width_.
    void correct_horiz_advance(float actual_w);
    [[nodiscard]] float text_height() const;
    [[nodiscard]] float measure_text_width(std::u8string_view text) const;

    /// @brief Build a scoped widget ID: "panel_id##label##N".
    ///
    /// The panel ID prefix prevents collisions between identically-labelled
    /// widgets in different panels. The per-panel sequence number (N)
    /// disambiguates two widgets with the same label inside the *same* panel.
    [[nodiscard]] std::string make_widget_id(std::string_view label);

    UIContext& ctx_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::string panel_id_;
    std::string last_widget_id_;
    Rect panel_rect_;
    float z_base_;
    float cursor_y_;
    float content_x_;
    float content_width_;
    float padding_;
    float margin_;
    int widget_seq_ = 0;
    bool in_horizontal_ = false;
    float horiz_cursor_x_ = 0;
    float horiz_max_h_ = 0;
};

/// @brief Per-frame immediate-mode UI context.
///
/// `UIContext` is the stateful core of the UI system. It accumulates draw
/// calls generated by `panel()` callbacks into batches, then flushes them to
/// the GPU via `flush()`. Mouse state and widget hot/active tracking live here
/// so the same `UIContext` can service multiple panels per frame.
///
/// In normal usage you never instantiate `UIContext` directly — `run()`
/// creates one and stores it as a World resource. Retrieve it from a system:
///
/// @code
/// void MyUISystem::draw(World& world, Renderer& renderer) {
///     auto& ui = world.resource<UIContext>();
///
///     ui.panel("stats_panel",
///         PanelPlacement{ .anchor = Anchor::TopRight, .size = {180.0f, 120.0f} },
///         [&](PanelBuilder& p) {
///             p.text(std::format("HP:  {} / {}", hp, max_hp));
///             p.text(std::format("MP:  {} / {}", mp, max_mp));
///             p.text(std::format("XP:  {}", xp));
///             p.text(std::format("Lvl: {}", level));
///         });
/// }
/// @endcode
class UIContext {
public:
    UIContext();
    ~UIContext();

    /// @brief Reset per-frame state and consume pending input events.
    ///
    /// Must be called once at the start of every frame, before any `panel()`
    /// calls. Normally handled automatically by `UIInputSystem`.
    ///
    /// @param events   Events from the current frame (mouse, keyboard).
    /// @param renderer Used to obtain the current framebuffer dimensions.
    void begin_frame(std::vector<Event>& events, const Renderer& renderer);

    /// @brief Declare a UI panel and lay out its widgets for this frame.
    ///
    /// The @p fn callback receives a `PanelBuilder` reference and should call
    /// widget methods (text, button, checkbox, list, text_input, horizontal)
    /// to build the panel layout. Returned values (e.g. `button()` returning
    /// `true`) are valid only during the execution of @p fn.
    ///
    /// @param id        Unique panel identifier (used for per-panel state).
    /// @param placement Size and anchor position of the panel.
    /// @param fn        Layout callback: `void fn(PanelBuilder&)`.
    ///
    /// @code
    /// ui.panel("inventory",
    ///     PanelPlacement{ .anchor = Anchor::Left, .size = {220.0f, 400.0f} },
    ///     [&](PanelBuilder& p) {
    ///         p.text("Inventory");
    ///         for (auto& item : player.inventory) {
    ///             if (p.button(item.name)) { use_item(item); }
    ///         }
    ///     });
    /// @endcode
    template<typename Fn>
    void panel(std::string_view id, PanelPlacement placement, Fn&& fn) {
        auto rect = resolve_placement(placement);
        draw_panel_bg(rect);

        const float z = theme_->z_order;
        PanelBuilder builder(*this, rect, z, id);
        std::forward<Fn>(fn)(builder);
    }

    /// @brief Draw a modal dialog: a semi-transparent backdrop over the entire
    /// screen followed by a centred panel.
    ///
    /// All mouse input outside the dialog is consumed by the backdrop, making
    /// underlying panels non-interactive while the modal is open.
    ///
    /// @param id        Unique panel identifier.
    /// @param size      Width and height of the dialog panel.
    /// @param fn        Layout callback: `void fn(PanelBuilder&)`.
    /// @param backdrop  Backdrop overlay colour (default: semi-transparent black).
    ///
    /// @code
    /// if (show_confirm) {
    ///     ui.modal("confirm_dlg", {300, 120}, [&](auto& p) {
    ///         p.text(u8"Are you sure?");
    ///         p.horizontal([&](auto& row) {
    ///             if (row.button(u8"Yes")) { do_thing(); show_confirm = false; }
    ///             if (row.button(u8"No"))  { show_confirm = false; }
    ///         });
    ///     });
    /// }
    /// @endcode
    template<typename Fn>
    void modal(std::string_view id, Vec2 size, Fn&& fn, Color backdrop = {0, 0, 0, 160}) {
        // Draw full-screen backdrop.
        const auto sw = static_cast<float>(screen_w_);
        const auto sh = static_cast<float>(screen_h_);
        const float modal_z = theme_->z_order + 5.0f;
        draw_rect({0, 0, sw, sh}, backdrop, modal_z);

        // Register the backdrop as a widget so it captures stray clicks.
        register_widget(std::string(id) + "##backdrop", {0, 0, sw, sh});

        // Centre the dialog panel.
        const float px = (sw - size.x) / 2.0f;
        const float py = (sh - size.y) / 2.0f;
        const Rect rect = {px, py, size.x, size.y};
        draw_rect(rect, theme_->bg_color, modal_z + 0.01f);
        if (theme_->border_width > 0.0f) {
            draw_border(rect, theme_->border_color, theme_->border_width, modal_z + 0.02f);
        }

        PanelBuilder builder(*this, rect, modal_z + 0.03f, id);
        std::forward<Fn>(fn)(builder);
    }

    /// @brief Flush all accumulated draw calls to the renderer.
    ///
    /// Submits all sprite instances produced by the current frame's panel
    /// calls. Must be called once after all panels have been declared, before
    /// the frame is presented. Normally handled automatically by
    /// `UIFlushSystem`.
    void flush(Renderer& renderer);

    /// @brief Set the theme pointer. Called by run() and UIInputSystem.
    void set_theme(const UITheme* theme) { theme_ = theme; }

    // Called by UIInputSystem each frame to lazily create the white texture.
    void ensure_white_texture(vk::Context& ctx);

private:
    friend class PanelBuilder;

    Rect resolve_placement(const PanelPlacement& p) const;
    void draw_panel_bg(Rect rect);
    void draw_rect(Rect rect, Color color, float z);
    void draw_border(Rect rect, Color color, float width, float z);
    void draw_text_at(std::u8string_view text, float x, float y, Color color, float z);
    float glyph_width() const;
    float glyph_height() const;

    void register_widget(std::string_view id, Rect rect);
    bool is_hot(std::string_view id) const;
    bool is_active(std::string_view id) const;
    bool is_clicked(std::string_view id) const;

    // 1×1 white texture used as the source for all filled rect draws.
    std::shared_ptr<Texture> white_texture_;

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
    std::unordered_map<std::string, bool> scroll_at_bottom_;
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

/// @brief ECS system that drives `UIContext` each frame.
///
/// `UIInputSystem` calls `UIContext::begin_frame()` at the start of every
/// update tick, feeding in the current events and renderer state. It also
/// picks up any `UITheme` resource from the World and applies it.
///
/// Register this system **before** any system that calls `UIContext::panel()`:
///
/// @code
/// world.add_system<UIInputSystem>();    // must be first
/// world.add_system<HUDSystem>();
/// world.add_system<MenuSystem>();
/// world.add_system<UIFlushSystem>();   // must be last
/// @endcode
class UIInputSystem : public System {
public:
    /// @brief Polls input events and prepares the UIContext for a new frame.
    void update(World& world, float dt) override;
};

/// @brief ECS system that flushes the UIContext draw calls to the renderer.
///
/// Call `UIFlushSystem::draw()` after all panels have been declared for the
/// frame. It calls `UIContext::flush()` which submits all batched UI sprites
/// to the renderer.
///
/// Register this system **last** so UI renders on top of the game world:
///
/// @code
/// world.add_system<GameRenderSystem>();
/// world.add_system<UIFlushSystem>();   // always last
/// @endcode
class UIFlushSystem : public System {
public:
    /// @brief Flushes all accumulated UI draw calls to the renderer.
    void draw(World& world, Renderer& renderer) override;
};

/// @brief Resolve a PanelPlacement to a screen-space Rect.
///
/// Converts an anchor + size + offset into an absolute pixel rectangle given
/// the current screen dimensions. Used internally by `UIContext::panel()` but
/// also available for custom layout calculations.
///
/// @param p         The panel placement to resolve.
/// @param screen_w  Current framebuffer width in pixels.
/// @param screen_h  Current framebuffer height in pixels.
/// @return          Absolute screen-space rectangle.
///
/// @code
/// // Manually compute where a centred 400×300 panel would be placed.
/// Rect r = resolve_panel_placement(
///     PanelPlacement{ .anchor = Anchor::Center, .size = {400.0f, 300.0f} },
///     1280, 720);
/// // r == Rect{ 440.0f, 210.0f, 400.0f, 300.0f }
/// @endcode
[[nodiscard]] Rect resolve_panel_placement(const PanelPlacement& p, uint32_t screen_w,
                                           uint32_t screen_h);

} // namespace xebble
