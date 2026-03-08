#include <xebble/types.hpp>

#include <gtest/gtest.h>

using namespace xebble;

TEST(Vec2, DefaultConstruction) {
    Vec2 v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
}

TEST(Vec2, AggregateInit) {
    Vec2 v{3.0f, 4.0f};
    EXPECT_FLOAT_EQ(v.x, 3.0f);
    EXPECT_FLOAT_EQ(v.y, 4.0f);
}

TEST(Rect, AggregateInit) {
    Rect r{1.0f, 2.0f, 16.0f, 16.0f};
    EXPECT_FLOAT_EQ(r.x, 1.0f);
    EXPECT_FLOAT_EQ(r.w, 16.0f);
}

TEST(Color, DefaultWhite) {
    Color c{255, 255, 255, 255};
    EXPECT_EQ(c.r, 255);
    EXPECT_EQ(c.a, 255);
}

TEST(Error, Message) {
    Error e{"something went wrong"};
    EXPECT_EQ(e.message, "something went wrong");
}
