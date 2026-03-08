#include <xebble/ui.hpp>

#include <gtest/gtest.h>

using namespace xebble;

TEST(UIPlacement, TopLeft) {
    PanelPlacement p{.anchor = Anchor::TopLeft, .size = {200, 100}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 0.0f);
    EXPECT_FLOAT_EQ(r.w, 200.0f);
    EXPECT_FLOAT_EQ(r.h, 100.0f);
}

TEST(UIPlacement, Center) {
    PanelPlacement p{.anchor = Anchor::Center, .size = {200, 100}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.x, 220.0f);
    EXPECT_FLOAT_EQ(r.y, 130.0f);
    EXPECT_FLOAT_EQ(r.w, 200.0f);
    EXPECT_FLOAT_EQ(r.h, 100.0f);
}

TEST(UIPlacement, BottomRight) {
    PanelPlacement p{.anchor = Anchor::BottomRight, .size = {200, 100}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.x, 440.0f);
    EXPECT_FLOAT_EQ(r.y, 260.0f);
}

TEST(UIPlacement, WithOffset) {
    PanelPlacement p{.anchor = Anchor::TopRight, .size = {128, 128}, .offset = {-8, 8}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.x, 640.0f - 128.0f - 8.0f);
    EXPECT_FLOAT_EQ(r.y, 8.0f);
}

TEST(UIPlacement, FractionalWidth) {
    PanelPlacement p{.anchor = Anchor::Bottom, .size = {1.0f, 40}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.w, 640.0f);
    EXPECT_FLOAT_EQ(r.h, 40.0f);
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 320.0f);
}

TEST(UIPlacement, FractionalBoth) {
    PanelPlacement p{.anchor = Anchor::Center, .size = {0.5f, 0.5f}};
    auto r = resolve_panel_placement(p, 640, 360);
    EXPECT_FLOAT_EQ(r.w, 320.0f);
    EXPECT_FLOAT_EQ(r.h, 180.0f);
    EXPECT_FLOAT_EQ(r.x, 160.0f);
    EXPECT_FLOAT_EQ(r.y, 90.0f);
}
