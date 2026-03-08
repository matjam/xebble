#include <xebble/spritesheet.hpp>

#include <gtest/gtest.h>

using namespace xebble;

TEST(SpriteSheetRegion, LinearIndex) {
    // 128x128 sheet with 16x16 tiles = 8 columns, 8 rows
    auto region = SpriteSheet::calculate_region(128, 128, 16, 16, 0);
    EXPECT_FLOAT_EQ(region.x, 0.0f);
    EXPECT_FLOAT_EQ(region.y, 0.0f);
    EXPECT_FLOAT_EQ(region.w, 16.0f / 128.0f);
    EXPECT_FLOAT_EQ(region.h, 16.0f / 128.0f);
}

TEST(SpriteSheetRegion, SecondRow) {
    auto region = SpriteSheet::calculate_region(128, 128, 16, 16, 8); // First tile of second row
    EXPECT_FLOAT_EQ(region.x, 0.0f);
    EXPECT_FLOAT_EQ(region.y, 16.0f / 128.0f);
}

TEST(SpriteSheetRegion, ColRow) {
    auto region = SpriteSheet::calculate_region(128, 128, 16, 16, 3, 2); // col=3, row=2
    EXPECT_FLOAT_EQ(region.x, 48.0f / 128.0f);
    EXPECT_FLOAT_EQ(region.y, 32.0f / 128.0f);
}

TEST(SpriteSheetRegion, LastTile) {
    // 128x128 with 16x16 = 64 tiles, last is index 63 = col 7, row 7
    auto region = SpriteSheet::calculate_region(128, 128, 16, 16, 63);
    EXPECT_FLOAT_EQ(region.x, 112.0f / 128.0f);
    EXPECT_FLOAT_EQ(region.y, 112.0f / 128.0f);
    EXPECT_FLOAT_EQ(region.w, 16.0f / 128.0f);
    EXPECT_FLOAT_EQ(region.h, 16.0f / 128.0f);
}

TEST(SpriteSheetRegion, NonSquareSheet) {
    // 256x128 sheet with 32x32 tiles = 8 columns, 4 rows
    auto region = SpriteSheet::calculate_region(256, 128, 32, 32, 2, 3);
    EXPECT_FLOAT_EQ(region.x, 64.0f / 256.0f);
    EXPECT_FLOAT_EQ(region.y, 96.0f / 128.0f);
    EXPECT_FLOAT_EQ(region.w, 32.0f / 256.0f);
    EXPECT_FLOAT_EQ(region.h, 32.0f / 128.0f);
}
