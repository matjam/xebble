#include <xebble/input_map.hpp>

#include <gtest/gtest.h>

using namespace xebble;

// ---------------------------------------------------------------------------
// Key/MouseButton string conversion
// ---------------------------------------------------------------------------

TEST(KeyString, RoundTripAllNamedKeys) {
    // Every key that key_to_string returns a non-"Unknown" name for should
    // round-trip through string_to_key.
    for (int i = -1; i < 512; ++i) {
        auto k = static_cast<Key>(i);
        auto name = key_to_string(k);
        if (name != "Unknown") {
            EXPECT_EQ(string_to_key(name), k) << "key=" << i << " name=" << name;
        }
    }
}

TEST(KeyString, UnknownKeyReturnsUnknown) {
    EXPECT_EQ(key_to_string(Key::Unknown), "Unknown");
    EXPECT_EQ(key_to_string(static_cast<Key>(9999)), "Unknown");
}

TEST(KeyString, UnrecognisedStringReturnsUnknown) {
    EXPECT_EQ(string_to_key("NotAKey"), Key::Unknown);
    EXPECT_EQ(string_to_key(""), Key::Unknown);
}

TEST(KeyString, SpecificKeys) {
    EXPECT_EQ(key_to_string(Key::W), "W");
    EXPECT_EQ(key_to_string(Key::Escape), "Escape");
    EXPECT_EQ(key_to_string(Key::Enter), "Enter");
    EXPECT_EQ(key_to_string(Key::Space), "Space");
    EXPECT_EQ(key_to_string(Key::F1), "F1");
    EXPECT_EQ(key_to_string(Key::LeftShift), "LeftShift");
}

TEST(MouseButtonString, RoundTrip) {
    EXPECT_EQ(mouse_button_to_string(MouseButton::Left), "Left");
    EXPECT_EQ(mouse_button_to_string(MouseButton::Right), "Right");
    EXPECT_EQ(mouse_button_to_string(MouseButton::Middle), "Middle");

    EXPECT_EQ(string_to_mouse_button("Left"), MouseButton::Left);
    EXPECT_EQ(string_to_mouse_button("Right"), MouseButton::Right);
    EXPECT_EQ(string_to_mouse_button("Middle"), MouseButton::Middle);
}

// ---------------------------------------------------------------------------
// Binding management
// ---------------------------------------------------------------------------

TEST(InputMap, BindAndQueryKeys) {
    InputMap map;
    map.bind("move_up", Key::W);
    map.bind("move_up", Key::Up);

    auto keys = map.keys_for("move_up");
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0], Key::W);
    EXPECT_EQ(keys[1], Key::Up);
}

TEST(InputMap, BindDuplicateIsNoop) {
    InputMap map;
    map.bind("jump", Key::Space);
    map.bind("jump", Key::Space);
    EXPECT_EQ(map.keys_for("jump").size(), 1u);
}

TEST(InputMap, BindMouseButton) {
    InputMap map;
    map.bind("attack", MouseButton::Left);
    auto buttons = map.buttons_for("attack");
    ASSERT_EQ(buttons.size(), 1u);
    EXPECT_EQ(buttons[0], MouseButton::Left);
}

TEST(InputMap, UnbindKey) {
    InputMap map;
    map.bind("move_up", Key::W);
    map.bind("move_up", Key::Up);
    map.unbind("move_up", Key::W);
    auto keys = map.keys_for("move_up");
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0], Key::Up);
}

TEST(InputMap, UnbindMouseButton) {
    InputMap map;
    map.bind("attack", MouseButton::Left);
    map.unbind("attack", MouseButton::Left);
    EXPECT_TRUE(map.buttons_for("attack").empty());
}

TEST(InputMap, ClearAction) {
    InputMap map;
    map.bind("move_up", Key::W);
    map.bind("move_up", Key::Up);
    map.clear("move_up");
    EXPECT_TRUE(map.keys_for("move_up").empty());
}

TEST(InputMap, ClearAll) {
    InputMap map;
    map.bind("move_up", Key::W);
    map.bind("attack", MouseButton::Left);
    map.clear_all();
    EXPECT_TRUE(map.action_names().empty());
}

TEST(InputMap, ActionNames) {
    InputMap map;
    map.bind("move_up", Key::W);
    map.bind("attack", MouseButton::Left);
    map.bind("confirm", Key::Enter);
    auto names = map.action_names();
    ASSERT_EQ(names.size(), 3u);
    // action_names() returns sorted
    EXPECT_EQ(names[0], "attack");
    EXPECT_EQ(names[1], "confirm");
    EXPECT_EQ(names[2], "move_up");
}

TEST(InputMap, UnbindNonExistentActionIsNoop) {
    InputMap map;
    map.unbind("nonexistent", Key::W);            // no crash
    map.unbind("nonexistent", MouseButton::Left); // no crash
    map.clear("nonexistent");                     // no crash
}

// ---------------------------------------------------------------------------
// Per-frame state queries
// ---------------------------------------------------------------------------

// Helper: simulate a frame with given events through InputMap::update (via friend).
// We access update() indirectly by creating events and calling the private method.
// Since update() is private and InputMapSystem is a friend, we test through the system.
// But for unit tests, we use a simpler approach: just test via the public API
// after manually building an event vector and using a World+InputMapSystem.

namespace {
// A minimal helper that calls InputMap::update directly via InputMapSystem.
// We create a World with an EventQueue and InputMap, then tick the system.
} // namespace

// Since InputMap::update is private (friend of InputMapSystem), we test
// the state machine by constructing events and running InputMapSystem::update.
#include <xebble/ecs.hpp>
#include <xebble/world.hpp>

namespace {
void tick_input(World& world, std::vector<Event> events) {
    world.resource<EventQueue>().events = std::move(events);
    // Run InputMapSystem manually
    InputMapSystem sys;
    sys.update(world, 0.0f);
}

World make_world_with_input_map() {
    World world;
    world.add_resource<EventQueue>(EventQueue{});
    world.add_resource<InputMap>(InputMap{});
    return world;
}
} // namespace

TEST(InputMap, PressedOnFirstFrame) {
    auto world = make_world_with_input_map();
    auto& input = world.resource<InputMap>();
    input.bind("confirm", Key::Enter);

    tick_input(world, {Event::key_press(Key::Enter, {})});

    EXPECT_TRUE(input.is_pressed("confirm"));
    EXPECT_TRUE(input.is_held("confirm"));
    EXPECT_FALSE(input.is_released("confirm"));
}

TEST(InputMap, HeldOnSubsequentFrame) {
    auto world = make_world_with_input_map();
    auto& input = world.resource<InputMap>();
    input.bind("confirm", Key::Enter);

    // Frame 1: press
    tick_input(world, {Event::key_press(Key::Enter, {})});
    // Frame 2: no events (key still held physically)
    tick_input(world, {});

    EXPECT_FALSE(input.is_pressed("confirm")); // pressed is single-frame
    EXPECT_TRUE(input.is_held("confirm"));     // still held
    EXPECT_FALSE(input.is_released("confirm"));
}

TEST(InputMap, ReleasedOnReleaseFrame) {
    auto world = make_world_with_input_map();
    auto& input = world.resource<InputMap>();
    input.bind("confirm", Key::Enter);

    tick_input(world, {Event::key_press(Key::Enter, {})});
    tick_input(world, {Event::key_release(Key::Enter, {})});

    EXPECT_FALSE(input.is_pressed("confirm"));
    EXPECT_FALSE(input.is_held("confirm"));
    EXPECT_TRUE(input.is_released("confirm"));
}

TEST(InputMap, ReleasedClearedNextFrame) {
    auto world = make_world_with_input_map();
    auto& input = world.resource<InputMap>();
    input.bind("confirm", Key::Enter);

    tick_input(world, {Event::key_press(Key::Enter, {})});
    tick_input(world, {Event::key_release(Key::Enter, {})});
    tick_input(world, {}); // frame after release

    EXPECT_FALSE(input.is_pressed("confirm"));
    EXPECT_FALSE(input.is_held("confirm"));
    EXPECT_FALSE(input.is_released("confirm"));
}

TEST(InputMap, MultipleKeysOneAction) {
    auto world = make_world_with_input_map();
    auto& input = world.resource<InputMap>();
    input.bind("move_up", Key::W);
    input.bind("move_up", Key::Up);

    // Press W
    tick_input(world, {Event::key_press(Key::W, {})});
    EXPECT_TRUE(input.is_held("move_up"));

    // Also press Up (W still held)
    tick_input(world, {Event::key_press(Key::Up, {})});
    EXPECT_TRUE(input.is_held("move_up"));
    EXPECT_FALSE(input.is_pressed("move_up")); // action was already held

    // Release W (Up still held)
    tick_input(world, {Event::key_release(Key::W, {})});
    EXPECT_TRUE(input.is_held("move_up"));
    EXPECT_FALSE(input.is_released("move_up"));

    // Release Up (nothing held)
    tick_input(world, {Event::key_release(Key::Up, {})});
    EXPECT_FALSE(input.is_held("move_up"));
    EXPECT_TRUE(input.is_released("move_up"));
}

TEST(InputMap, MouseButtonPress) {
    auto world = make_world_with_input_map();
    auto& input = world.resource<InputMap>();
    input.bind("attack", MouseButton::Left);

    tick_input(world, {Event::mouse_press(MouseButton::Left, {}, {0, 0})});
    EXPECT_TRUE(input.is_pressed("attack"));
    EXPECT_TRUE(input.is_held("attack"));

    tick_input(world, {Event::mouse_release(MouseButton::Left, {}, {0, 0})});
    EXPECT_FALSE(input.is_held("attack"));
    EXPECT_TRUE(input.is_released("attack"));
}

TEST(InputMap, MixedKeyAndMouseBinding) {
    auto world = make_world_with_input_map();
    auto& input = world.resource<InputMap>();
    input.bind("attack", Key::X);
    input.bind("attack", MouseButton::Left);

    // Press X
    tick_input(world, {Event::key_press(Key::X, {})});
    EXPECT_TRUE(input.is_held("attack"));

    // Release X, press mouse left in same frame
    tick_input(world, {
                          Event::key_release(Key::X, {}),
                          Event::mouse_press(MouseButton::Left, {}, {0, 0}),
                      });
    // Action should still be held (mouse took over)
    EXPECT_TRUE(input.is_held("attack"));

    // Release mouse
    tick_input(world, {Event::mouse_release(MouseButton::Left, {}, {0, 0})});
    EXPECT_FALSE(input.is_held("attack"));
    EXPECT_TRUE(input.is_released("attack"));
}

TEST(InputMap, ConsumedEventsIgnored) {
    auto world = make_world_with_input_map();
    auto& input = world.resource<InputMap>();
    input.bind("confirm", Key::Enter);

    auto ev = Event::key_press(Key::Enter, {});
    ev.consumed = true;
    tick_input(world, {ev});

    EXPECT_FALSE(input.is_pressed("confirm"));
    EXPECT_FALSE(input.is_held("confirm"));
}

TEST(InputMap, UnboundActionQueryReturnsFalse) {
    auto world = make_world_with_input_map();
    auto& input = world.resource<InputMap>();
    // No bindings at all
    EXPECT_FALSE(input.is_pressed("nonexistent"));
    EXPECT_FALSE(input.is_held("nonexistent"));
    EXPECT_FALSE(input.is_released("nonexistent"));
}

TEST(InputMap, KeyRepeatIgnored) {
    auto world = make_world_with_input_map();
    auto& input = world.resource<InputMap>();
    input.bind("confirm", Key::Enter);

    // KeyRepeat events should not trigger pressed/held
    tick_input(world, {Event::key_repeat(Key::Enter, {})});
    EXPECT_FALSE(input.is_pressed("confirm"));
    EXPECT_FALSE(input.is_held("confirm"));
}

// ---------------------------------------------------------------------------
// TOML serialization
// ---------------------------------------------------------------------------

TEST(InputMap, TomlRoundTrip) {
    InputMap original;
    original.bind("move_up", Key::W);
    original.bind("move_up", Key::Up);
    original.bind("confirm", Key::Enter);
    original.bind("confirm", Key::Space);
    original.bind("attack", MouseButton::Left);

    auto toml_str = original.to_toml_string();

    InputMap loaded;
    ASSERT_TRUE(loaded.from_toml_string(toml_str));

    // Verify bindings match
    auto keys_up = loaded.keys_for("move_up");
    ASSERT_EQ(keys_up.size(), 2u);
    EXPECT_EQ(keys_up[0], Key::W);
    EXPECT_EQ(keys_up[1], Key::Up);

    auto keys_confirm = loaded.keys_for("confirm");
    ASSERT_EQ(keys_confirm.size(), 2u);
    EXPECT_EQ(keys_confirm[0], Key::Enter);
    EXPECT_EQ(keys_confirm[1], Key::Space);

    auto btns_attack = loaded.buttons_for("attack");
    ASSERT_EQ(btns_attack.size(), 1u);
    EXPECT_EQ(btns_attack[0], MouseButton::Left);
}

TEST(InputMap, FromTomlStringReplacesPreviousBindings) {
    InputMap map;
    map.bind("old_action", Key::A);

    auto toml_str = R"(
        [actions.new_action]
        keys = ["B"]
    )";
    ASSERT_TRUE(map.from_toml_string(toml_str));

    EXPECT_TRUE(map.keys_for("old_action").empty());
    EXPECT_EQ(map.keys_for("new_action").size(), 1u);
}

TEST(InputMap, FromTomlStringInvalidReturnsFalse) {
    InputMap map;
    EXPECT_FALSE(map.from_toml_string("this is not valid toml {{{{"));
}

TEST(InputMap, FromTomlStringNoActionsTableReturnsFalse) {
    InputMap map;
    EXPECT_FALSE(map.from_toml_string("[not_actions]\nfoo = 1\n"));
}

TEST(InputMap, TomlFileRoundTrip) {
    InputMap original;
    original.bind("move_up", Key::W);
    original.bind("attack", MouseButton::Right);

    std::string path = "/tmp/xebble_test_input_map.toml";
    original.save_toml(path);

    InputMap loaded;
    ASSERT_TRUE(loaded.load_toml(path));

    EXPECT_EQ(loaded.keys_for("move_up").size(), 1u);
    EXPECT_EQ(loaded.buttons_for("attack").size(), 1u);
    EXPECT_EQ(loaded.buttons_for("attack")[0], MouseButton::Right);

    // Clean up
    std::remove(path.c_str());
}

TEST(InputMap, LoadTomlNonExistentFileReturnsFalse) {
    InputMap map;
    EXPECT_FALSE(map.load_toml("/tmp/nonexistent_xebble_test_file.toml"));
}
