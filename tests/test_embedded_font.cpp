#include <xebble/embedded_font.hpp>

#include <gtest/gtest.h>

using namespace xebble::embedded_font;

TEST(EmbeddedFont, AtlasDimensions) {
    EXPECT_EQ(ATLAS_W, 128u);
    EXPECT_EQ(ATLAS_H, 72u);
}

TEST(EmbeddedFont, PixelDataSize) {
    auto pixels = generate_pixels();
    EXPECT_EQ(pixels.size(), ATLAS_W * ATLAS_H * 4);
}

TEST(EmbeddedFont, PixelDataNotEmpty) {
    auto pixels = generate_pixels();
    bool has_content = false;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 0) {
            has_content = true;
            break;
        }
    }
    EXPECT_TRUE(has_content);
}

TEST(EmbeddedFont, SpaceIsEmpty) {
    auto pixels = generate_pixels();
    for (uint32_t y = 0; y < GLYPH_H; y++) {
        for (uint32_t x = 0; x < GLYPH_W; x++) {
            int idx = (y * ATLAS_W + x) * 4;
            EXPECT_EQ(pixels[idx + 3], 0) << "at (" << x << "," << y << ")";
        }
    }
}

TEST(EmbeddedFont, Charset) {
    auto cs = charset();
    EXPECT_EQ(cs.size(), 95u);
    EXPECT_EQ(cs[0], ' ');
    EXPECT_EQ(cs[cs.size() - 1], '~');
}
