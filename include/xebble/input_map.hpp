/// @file input_map.hpp
/// @brief Named action layer for input — maps actions to key/mouse bindings.
///
/// `InputMap` is a World resource that translates raw key and mouse button
/// events into named actions, providing per-frame `is_pressed()`, `is_held()`,
/// and `is_released()` queries. This replaces the common boilerplate of
/// checking individual key codes in every system.
///
/// ## Setup
///
/// Bind actions in your system's `init()` or before calling `run()`:
///
/// @code
/// auto& input = world.resource<InputMap>();
/// input.bind("move_up",  Key::W);
/// input.bind("move_up",  Key::Up);     // multiple keys per action
/// input.bind("confirm",  Key::Enter);
/// input.bind("confirm",  Key::Space);
/// input.bind("attack",   MouseButton::Left);
/// @endcode
///
/// ## Querying
///
/// In your `update()`:
///
/// @code
/// auto& input = world.resource<InputMap>();
/// if (input.is_pressed("confirm"))  { /* first frame only */ }
/// if (input.is_held("move_up"))     { /* every frame while held */ }
/// if (input.is_released("attack"))  { /* frame the button went up */ }
/// @endcode
///
/// ## Serialization
///
/// Save and load player-configurable bindings:
///
/// @code
/// input.save_toml("controls.toml");
/// input.load_toml("controls.toml");
/// @endcode
///
/// ## Execution order
///
/// `InputMapSystem` runs after `UIInputSystem` (so UI gets first pick at
/// events) but before user systems. It reads the `EventQueue`, skips consumed
/// events, and updates the action state for the current frame. Raw events are
/// **not** consumed — your systems can still read them directly if needed.
#pragma once

#include <xebble/event.hpp>
#include <xebble/system.hpp>
#include <xebble/types.hpp>

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// String conversion (declared here, defined in input_map.cpp)
// ---------------------------------------------------------------------------

/// @brief Convert a Key enum value to its canonical string name (e.g. Key::W -> "W").
/// @return The string name, or "Unknown" for unrecognised values.
[[nodiscard]] std::string_view key_to_string(Key key);

/// @brief Parse a string name into a Key enum value.
/// @return The Key, or Key::Unknown if the string is not recognised.
[[nodiscard]] Key string_to_key(std::string_view name);

/// @brief Convert a MouseButton enum value to its string name (e.g. MouseButton::Left -> "Left").
[[nodiscard]] std::string_view mouse_button_to_string(MouseButton button);

/// @brief Parse a string name into a MouseButton enum value.
/// @return The MouseButton, or MouseButton::Left if the string is not recognised.
[[nodiscard]] MouseButton string_to_mouse_button(std::string_view name);

// ---------------------------------------------------------------------------
// InputMap
// ---------------------------------------------------------------------------

/// @brief Named action layer — maps string action names to key/mouse bindings.
///
/// Actions accumulate bindings: calling `bind("move_up", Key::W)` and then
/// `bind("move_up", Key::Up)` means either key activates the action.
///
/// State queries (`is_pressed`, `is_held`, `is_released`) reflect the current
/// frame's input. They are updated each frame by `InputMapSystem` before user
/// systems run.
class InputMap {
public:
    // -- Binding management -------------------------------------------------

    /// @brief Bind a keyboard key to a named action.
    ///
    /// If the key is already bound to this action, the call is a no-op.
    void bind(std::string_view action, Key key);

    /// @brief Bind a mouse button to a named action.
    ///
    /// If the button is already bound to this action, the call is a no-op.
    void bind(std::string_view action, MouseButton button);

    /// @brief Remove a specific key binding from an action.
    void unbind(std::string_view action, Key key);

    /// @brief Remove a specific mouse button binding from an action.
    void unbind(std::string_view action, MouseButton button);

    /// @brief Remove all bindings for a named action.
    void clear(std::string_view action);

    /// @brief Remove all actions and bindings.
    void clear_all();

    // -- Per-frame state queries --------------------------------------------

    /// @brief True on the first frame any bound key/button for @p action is pressed.
    ///
    /// Returns false if the action has no bindings or was not just pressed.
    [[nodiscard]] bool is_pressed(std::string_view action) const;

    /// @brief True every frame while any bound key/button for @p action is held down.
    [[nodiscard]] bool is_held(std::string_view action) const;

    /// @brief True on the frame the last bound key/button for @p action is released.
    [[nodiscard]] bool is_released(std::string_view action) const;

    // -- Serialization ------------------------------------------------------

    /// @brief Save all bindings to a TOML file.
    ///
    /// The file can be loaded later with `load_toml()` to restore bindings.
    /// Existing file contents are overwritten.
    void save_toml(std::string_view path) const;

    /// @brief Load bindings from a TOML file, replacing all current bindings.
    ///
    /// Returns false if the file could not be opened or parsed.
    [[nodiscard]] bool load_toml(std::string_view path);

    /// @brief Serialize all bindings to a TOML-formatted string.
    [[nodiscard]] std::string to_toml_string() const;

    /// @brief Load bindings from a TOML-formatted string, replacing all current bindings.
    ///
    /// Returns false if the string could not be parsed.
    [[nodiscard]] bool from_toml_string(std::string_view toml_str);

    // -- Introspection ------------------------------------------------------

    /// @brief Return the list of key bindings for a named action.
    [[nodiscard]] std::vector<Key> keys_for(std::string_view action) const;

    /// @brief Return the list of mouse button bindings for a named action.
    [[nodiscard]] std::vector<MouseButton> buttons_for(std::string_view action) const;

    /// @brief Return all registered action names.
    [[nodiscard]] std::vector<std::string> action_names() const;

private:
    friend class InputMapSystem;

    struct ActionBindings {
        std::vector<Key> keys;
        std::vector<MouseButton> buttons;
    };

    /// @brief Called by InputMapSystem each frame to update pressed/held/released state.
    void update(const std::vector<Event>& events);

    std::unordered_map<std::string, ActionBindings> bindings_;

    // Per-frame action state
    std::unordered_set<std::string> pressed_;
    std::unordered_set<std::string> held_;
    std::unordered_set<std::string> released_;

    // Raw key/mouse state tracking across frames
    // Key enum values go up to 348 (Menu); 512 slots is plenty.
    static constexpr size_t KEY_STATE_SIZE = 512;
    static constexpr size_t MOUSE_STATE_SIZE = 8;
    std::array<bool, KEY_STATE_SIZE> key_state_{};
    std::array<bool, MOUSE_STATE_SIZE> mouse_state_{};
};

// ---------------------------------------------------------------------------
// InputMapSystem
// ---------------------------------------------------------------------------

/// @brief System that updates the InputMap resource each frame.
///
/// This system reads the EventQueue, skips consumed events, and calls
/// `InputMap::update()` to refresh the pressed/held/released state.
///
/// It is automatically prepended by the engine in `inject_and_init()` so it
/// runs after `UIInputSystem` but before user systems.
class InputMapSystem : public System {
public:
    void update(World& world, float dt) override;
};

} // namespace xebble
