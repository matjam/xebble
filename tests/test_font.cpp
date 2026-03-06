#include <gtest/gtest.h>
#include <xebble/font.hpp>

using namespace xebble;

TEST(BitmapFontData, GlyphLookup) {
    BitmapFontData data(8, 8, " !\"#$%&'()*+,-./0123456789");
    auto region = data.glyph_index('A');
    EXPECT_EQ(region, std::nullopt); // 'A' not in charset

    auto space = data.glyph_index(' ');
    EXPECT_EQ(space, 0u);

    auto zero = data.glyph_index('0');
    EXPECT_EQ(zero, 16u); // 16th character in the charset
}

TEST(BitmapFontData, EmptyCharset) {
    BitmapFontData data(8, 8, "");
    EXPECT_EQ(data.glyph_index('A'), std::nullopt);
}

TEST(BitmapFontData, Dimensions) {
    BitmapFontData data(8, 16, "ABCD");
    EXPECT_EQ(data.glyph_width(), 8u);
    EXPECT_EQ(data.glyph_height(), 16u);
    EXPECT_EQ(data.charset(), "ABCD");
}
