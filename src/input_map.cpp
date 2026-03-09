/// @file input_map.cpp
/// @brief InputMap implementation — action state update, TOML serialization, key/mouse string
/// conversion.
#include <xebble/ecs.hpp>
#include <xebble/input_map.hpp>
#include <xebble/log.hpp>
#include <xebble/world.hpp>

#include <toml++/toml.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

namespace xebble {

// ---------------------------------------------------------------------------
// Key <-> string conversion tables
// ---------------------------------------------------------------------------

namespace {

struct KeyEntry {
    Key key;
    std::string_view name;
};

// Sorted by enum value for binary search in key_to_string.
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays)
constexpr KeyEntry KEY_TABLE[] = {
    {Key::Space, "Space"},
    {Key::Apostrophe, "Apostrophe"},
    {Key::Comma, "Comma"},
    {Key::Minus, "Minus"},
    {Key::Period, "Period"},
    {Key::Slash, "Slash"},
    {Key::Num0, "Num0"},
    {Key::Num1, "Num1"},
    {Key::Num2, "Num2"},
    {Key::Num3, "Num3"},
    {Key::Num4, "Num4"},
    {Key::Num5, "Num5"},
    {Key::Num6, "Num6"},
    {Key::Num7, "Num7"},
    {Key::Num8, "Num8"},
    {Key::Num9, "Num9"},
    {Key::Semicolon, "Semicolon"},
    {Key::Equal, "Equal"},
    {Key::A, "A"},
    {Key::B, "B"},
    {Key::C, "C"},
    {Key::D, "D"},
    {Key::E, "E"},
    {Key::F, "F"},
    {Key::G, "G"},
    {Key::H, "H"},
    {Key::I, "I"},
    {Key::J, "J"},
    {Key::K, "K"},
    {Key::L, "L"},
    {Key::M, "M"},
    {Key::N, "N"},
    {Key::O, "O"},
    {Key::P, "P"},
    {Key::Q, "Q"},
    {Key::R, "R"},
    {Key::S, "S"},
    {Key::T, "T"},
    {Key::U, "U"},
    {Key::V, "V"},
    {Key::W, "W"},
    {Key::X, "X"},
    {Key::Y, "Y"},
    {Key::Z, "Z"},
    {Key::LeftBracket, "LeftBracket"},
    {Key::Backslash, "Backslash"},
    {Key::RightBracket, "RightBracket"},
    {Key::GraveAccent, "GraveAccent"},
    {Key::Escape, "Escape"},
    {Key::Enter, "Enter"},
    {Key::Tab, "Tab"},
    {Key::Backspace, "Backspace"},
    {Key::Insert, "Insert"},
    {Key::Delete, "Delete"},
    {Key::Right, "Right"},
    {Key::Left, "Left"},
    {Key::Down, "Down"},
    {Key::Up, "Up"},
    {Key::PageUp, "PageUp"},
    {Key::PageDown, "PageDown"},
    {Key::Home, "Home"},
    {Key::End, "End"},
    {Key::CapsLock, "CapsLock"},
    {Key::ScrollLock, "ScrollLock"},
    {Key::NumLock, "NumLock"},
    {Key::PrintScreen, "PrintScreen"},
    {Key::Pause, "Pause"},
    {Key::F1, "F1"},
    {Key::F2, "F2"},
    {Key::F3, "F3"},
    {Key::F4, "F4"},
    {Key::F5, "F5"},
    {Key::F6, "F6"},
    {Key::F7, "F7"},
    {Key::F8, "F8"},
    {Key::F9, "F9"},
    {Key::F10, "F10"},
    {Key::F11, "F11"},
    {Key::F12, "F12"},
    {Key::LeftShift, "LeftShift"},
    {Key::LeftControl, "LeftControl"},
    {Key::LeftAlt, "LeftAlt"},
    {Key::LeftSuper, "LeftSuper"},
    {Key::RightShift, "RightShift"},
    {Key::RightControl, "RightControl"},
    {Key::RightAlt, "RightAlt"},
    {Key::RightSuper, "RightSuper"},
    {Key::Menu, "Menu"},
};
// NOLINTEND(cppcoreguidelines-avoid-c-arrays)

struct MouseEntry {
    MouseButton button;
    std::string_view name;
};

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays)
constexpr MouseEntry MOUSE_TABLE[] = {
    {MouseButton::Left, "Left"},
    {MouseButton::Right, "Right"},
    {MouseButton::Middle, "Middle"},
};
// NOLINTEND(cppcoreguidelines-avoid-c-arrays)

} // namespace

std::string_view key_to_string(Key key) {
    for (const auto& entry : KEY_TABLE) {
        if (entry.key == key)
            return entry.name;
    }
    return "Unknown";
}

Key string_to_key(std::string_view name) {
    for (const auto& entry : KEY_TABLE) {
        if (entry.name == name)
            return entry.key;
    }
    return Key::Unknown;
}

std::string_view mouse_button_to_string(MouseButton button) {
    for (const auto& entry : MOUSE_TABLE) {
        if (entry.button == button)
            return entry.name;
    }
    return "Left";
}

MouseButton string_to_mouse_button(std::string_view name) {
    for (const auto& entry : MOUSE_TABLE) {
        if (entry.name == name)
            return entry.button;
    }
    return MouseButton::Left;
}

// ---------------------------------------------------------------------------
// InputMap — binding management
// ---------------------------------------------------------------------------

void InputMap::bind(std::string_view action, Key key) {
    auto& b = bindings_[std::string(action)];
    if (std::find(b.keys.begin(), b.keys.end(), key) == b.keys.end())
        b.keys.push_back(key);
}

void InputMap::bind(std::string_view action, MouseButton button) {
    auto& b = bindings_[std::string(action)];
    if (std::find(b.buttons.begin(), b.buttons.end(), button) == b.buttons.end())
        b.buttons.push_back(button);
}

void InputMap::unbind(std::string_view action, Key key) {
    auto it = bindings_.find(std::string(action));
    if (it == bindings_.end())
        return;
    auto& keys = it->second.keys;
    keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
}

void InputMap::unbind(std::string_view action, MouseButton button) {
    auto it = bindings_.find(std::string(action));
    if (it == bindings_.end())
        return;
    auto& buttons = it->second.buttons;
    buttons.erase(std::remove(buttons.begin(), buttons.end(), button), buttons.end());
}

void InputMap::clear(std::string_view action) {
    bindings_.erase(std::string(action));
}

void InputMap::clear_all() {
    bindings_.clear();
    pressed_.clear();
    held_.clear();
    released_.clear();
    key_state_.fill(false);
    mouse_state_.fill(false);
}

// ---------------------------------------------------------------------------
// InputMap — per-frame queries
// ---------------------------------------------------------------------------

bool InputMap::is_pressed(std::string_view action) const {
    return pressed_.contains(std::string(action));
}

bool InputMap::is_held(std::string_view action) const {
    return held_.contains(std::string(action));
}

bool InputMap::is_released(std::string_view action) const {
    return released_.contains(std::string(action));
}

// ---------------------------------------------------------------------------
// InputMap — introspection
// ---------------------------------------------------------------------------

std::vector<Key> InputMap::keys_for(std::string_view action) const {
    auto it = bindings_.find(std::string(action));
    if (it == bindings_.end())
        return {};
    return it->second.keys;
}

std::vector<MouseButton> InputMap::buttons_for(std::string_view action) const {
    auto it = bindings_.find(std::string(action));
    if (it == bindings_.end())
        return {};
    return it->second.buttons;
}

std::vector<std::string> InputMap::action_names() const {
    std::vector<std::string> names;
    names.reserve(bindings_.size());
    for (const auto& [name, _] : bindings_)
        names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

// ---------------------------------------------------------------------------
// InputMap — per-frame state update
// ---------------------------------------------------------------------------

void InputMap::update(const std::vector<Event>& events) {
    pressed_.clear();
    released_.clear();

    for (const auto& e : events) {
        if (e.consumed)
            continue;

        switch (e.type) {
        case EventType::KeyPress: {
            auto ki = static_cast<size_t>(static_cast<int>(e.key().key));
            if (ki < KEY_STATE_SIZE && !key_state_[ki]) {
                key_state_[ki] = true;
                // Check which actions this key is bound to
                for (const auto& [action, bindings] : bindings_) {
                    for (Key k : bindings.keys) {
                        if (k == e.key().key) {
                            if (!held_.contains(action)) {
                                pressed_.insert(action);
                                held_.insert(action);
                            }
                            break;
                        }
                    }
                }
            }
            break;
        }

        case EventType::KeyRelease: {
            auto ki = static_cast<size_t>(static_cast<int>(e.key().key));
            if (ki < KEY_STATE_SIZE) {
                key_state_[ki] = false;
                // Check which actions this key is bound to
                for (const auto& [action, bindings] : bindings_) {
                    if (!held_.contains(action))
                        continue;
                    bool any_still_held = false;
                    for (Key k : bindings.keys) {
                        auto kk = static_cast<size_t>(static_cast<int>(k));
                        if (kk < KEY_STATE_SIZE && key_state_[kk]) {
                            any_still_held = true;
                            break;
                        }
                    }
                    if (!any_still_held) {
                        // Also check mouse buttons
                        for (MouseButton btn : bindings.buttons) {
                            auto bi = static_cast<size_t>(static_cast<int>(btn));
                            if (bi < MOUSE_STATE_SIZE && mouse_state_[bi]) {
                                any_still_held = true;
                                break;
                            }
                        }
                    }
                    if (!any_still_held) {
                        held_.erase(action);
                        released_.insert(action);
                    }
                }
            }
            break;
        }

        case EventType::MousePress: {
            auto bi = static_cast<size_t>(static_cast<int>(e.mouse_button().button));
            if (bi < MOUSE_STATE_SIZE && !mouse_state_[bi]) {
                mouse_state_[bi] = true;
                for (const auto& [action, bindings] : bindings_) {
                    for (MouseButton btn : bindings.buttons) {
                        if (btn == e.mouse_button().button) {
                            if (!held_.contains(action)) {
                                pressed_.insert(action);
                                held_.insert(action);
                            }
                            break;
                        }
                    }
                }
            }
            break;
        }

        case EventType::MouseRelease: {
            auto bi = static_cast<size_t>(static_cast<int>(e.mouse_button().button));
            if (bi < MOUSE_STATE_SIZE) {
                mouse_state_[bi] = false;
                for (const auto& [action, bindings] : bindings_) {
                    if (!held_.contains(action))
                        continue;
                    bool any_still_held = false;
                    for (MouseButton btn : bindings.buttons) {
                        auto bk = static_cast<size_t>(static_cast<int>(btn));
                        if (bk < MOUSE_STATE_SIZE && mouse_state_[bk]) {
                            any_still_held = true;
                            break;
                        }
                    }
                    if (!any_still_held) {
                        for (Key k : bindings.keys) {
                            auto kk = static_cast<size_t>(static_cast<int>(k));
                            if (kk < KEY_STATE_SIZE && key_state_[kk]) {
                                any_still_held = true;
                                break;
                            }
                        }
                    }
                    if (!any_still_held) {
                        held_.erase(action);
                        released_.insert(action);
                    }
                }
            }
            break;
        }

        default:
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// InputMap — TOML serialization
// ---------------------------------------------------------------------------

std::string InputMap::to_toml_string() const {
    toml::table root;
    toml::table actions_tbl;

    // Sort action names for deterministic output
    auto names = action_names();

    for (const auto& name : names) {
        auto it = bindings_.find(name);
        if (it == bindings_.end())
            continue;
        const auto& b = it->second;

        toml::table action_tbl;
        if (!b.keys.empty()) {
            toml::array keys_arr;
            for (Key k : b.keys)
                keys_arr.push_back(std::string(key_to_string(k)));
            action_tbl.insert("keys", std::move(keys_arr));
        }
        if (!b.buttons.empty()) {
            toml::array mouse_arr;
            for (MouseButton btn : b.buttons)
                mouse_arr.push_back(std::string(mouse_button_to_string(btn)));
            action_tbl.insert("mouse", std::move(mouse_arr));
        }
        actions_tbl.insert(name, std::move(action_tbl));
    }

    root.insert("actions", std::move(actions_tbl));

    std::ostringstream oss;
    oss << root;
    return oss.str();
}

bool InputMap::from_toml_string(std::string_view toml_str) {
    try {
        auto tbl = toml::parse(toml_str);
        auto* actions = tbl["actions"].as_table();
        if (!actions)
            return false;

        // Clear existing bindings (but preserve held state for a clean transition)
        bindings_.clear();

        for (const auto& [action_name, val] : *actions) {
            auto* action_tbl = val.as_table();
            if (!action_tbl)
                continue;

            std::string action(action_name.str());

            if (auto* keys_arr = (*action_tbl)["keys"].as_array()) {
                for (const auto& k : *keys_arr) {
                    if (auto* s = k.as_string()) {
                        Key key = string_to_key(s->get());
                        if (key != Key::Unknown)
                            bind(action, key);
                    }
                }
            }
            if (auto* mouse_arr = (*action_tbl)["mouse"].as_array()) {
                for (const auto& m : *mouse_arr) {
                    if (auto* s = m.as_string())
                        bind(action, string_to_mouse_button(s->get()));
                }
            }
        }
        return true;
    } catch (const toml::parse_error& err) {
        log(LogLevel::Error, std::string("InputMap: TOML parse error: ") + err.what());
        return false;
    }
}

void InputMap::save_toml(std::string_view path) const {
    auto filepath = std::string(path);
    std::ofstream file{filepath};
    if (!file) {
        log(LogLevel::Error, "InputMap: could not open file for writing: " + filepath);
        return;
    }
    file << to_toml_string();
}

bool InputMap::load_toml(std::string_view path) {
    auto filepath = std::string(path);
    std::ifstream file{filepath};
    if (!file) {
        log(LogLevel::Error, "InputMap: could not open file for reading: " + filepath);
        return false;
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return from_toml_string(oss.str());
}

// ---------------------------------------------------------------------------
// InputMapSystem
// ---------------------------------------------------------------------------

void InputMapSystem::update(World& world, float /*dt*/) {
    if (!world.has_resource<InputMap>())
        return;
    auto& input_map = world.resource<InputMap>();
    const auto& eq = world.resource<EventQueue>();
    input_map.update(eq.events);
}

} // namespace xebble
