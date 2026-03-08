#include <xebble/event.hpp>

#include <gtest/gtest.h>

using namespace xebble;

TEST(Event, KeyPressEvent) {
    Event e = Event::key_press(Key::A, {.shift = true});
    EXPECT_EQ(e.type, EventType::KeyPress);
    EXPECT_EQ(e.key().key, Key::A);
    EXPECT_TRUE(e.key().mods.shift);
}

TEST(Event, MouseMoveEvent) {
    Event e = Event::mouse_move({100.0f, 200.0f});
    EXPECT_EQ(e.type, EventType::MouseMove);
    EXPECT_FLOAT_EQ(e.mouse_move().position.x, 100.0f);
    EXPECT_FLOAT_EQ(e.mouse_move().position.y, 200.0f);
}

TEST(Event, MouseButtonEvent) {
    Event e = Event::mouse_press(MouseButton::Left, {}, {50.0f, 60.0f});
    EXPECT_EQ(e.type, EventType::MousePress);
    EXPECT_EQ(e.mouse_button().button, MouseButton::Left);
    EXPECT_FLOAT_EQ(e.mouse_button().position.x, 50.0f);
}

TEST(Event, ScrollEvent) {
    Event e = Event::mouse_scroll(0.0f, -3.0f);
    EXPECT_EQ(e.type, EventType::MouseScroll);
    EXPECT_FLOAT_EQ(e.mouse_scroll().dy, -3.0f);
}

TEST(Event, ResizeEvent) {
    Event e = Event::window_resize(1920, 1080);
    EXPECT_EQ(e.type, EventType::WindowResize);
    EXPECT_EQ(e.resize().width, 1920u);
    EXPECT_EQ(e.resize().height, 1080u);
}

TEST(EventQueue, PushAndIterate) {
    std::vector<Event> queue;
    queue.push_back(Event::key_press(Key::W, {}));
    queue.push_back(Event::key_press(Key::A, {}));
    queue.push_back(Event::mouse_move({10.0f, 20.0f}));

    EXPECT_EQ(queue.size(), 3u);

    int key_count = 0;
    for (auto& e : queue) {
        switch (e.type) {
        case EventType::KeyPress:
            key_count++;
            break;
        default:
            break;
        }
    }
    EXPECT_EQ(key_count, 2);
}
