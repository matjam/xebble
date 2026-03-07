#include <gtest/gtest.h>
#include <xebble/asset_manager.hpp>

#include <string_view>

using namespace xebble;

static std::string_view sv(const std::u8string& s) {
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

TEST(ManifestParser, ParseSpriteSheet) {
    auto toml_str = R"(
        [spritesheets.dungeon]
        path = "sprites/dungeon.png"
        tile_width = 16
        tile_height = 16
    )";
    auto manifest = parse_manifest(toml_str);
    ASSERT_TRUE(manifest.spritesheets.contains("dungeon"));
    EXPECT_EQ(manifest.spritesheets["dungeon"].path, "sprites/dungeon.png");
    EXPECT_EQ(manifest.spritesheets["dungeon"].tile_width, 16u);
    EXPECT_EQ(manifest.spritesheets["dungeon"].tile_height, 16u);
}

TEST(ManifestParser, ParseBitmapFont) {
    auto toml_str = R"(
        [bitmap_fonts.default]
        path = "fonts/cp437.png"
        glyph_width = 8
        glyph_height = 8
        charset = " !#$"
    )";
    auto manifest = parse_manifest(toml_str);
    ASSERT_TRUE(manifest.bitmap_fonts.contains("default"));
    EXPECT_EQ(manifest.bitmap_fonts["default"].glyph_width, 8u);
    EXPECT_EQ(manifest.bitmap_fonts["default"].glyph_height, 8u);
    EXPECT_EQ(sv(manifest.bitmap_fonts["default"].charset), " !#$");
}

TEST(ManifestParser, ParseFont) {
    auto toml_str = R"(
        [fonts.ui]
        path = "fonts/roboto.ttf"
        pixel_size = 24
    )";
    auto manifest = parse_manifest(toml_str);
    ASSERT_TRUE(manifest.fonts.contains("ui"));
    EXPECT_EQ(manifest.fonts["ui"].path, "fonts/roboto.ttf");
    EXPECT_EQ(manifest.fonts["ui"].pixel_size, 24u);
}

TEST(ManifestParser, MultipleSections) {
    auto toml_str = R"(
        [spritesheets.tiles]
        path = "tiles.png"
        tile_width = 16
        tile_height = 16

        [spritesheets.characters]
        path = "chars.png"
        tile_width = 32
        tile_height = 32

        [fonts.body]
        path = "body.ttf"
        pixel_size = 14
    )";
    auto manifest = parse_manifest(toml_str);
    EXPECT_EQ(manifest.spritesheets.size(), 2u);
    EXPECT_EQ(manifest.fonts.size(), 1u);
    EXPECT_EQ(manifest.spritesheets["characters"].tile_width, 32u);
}

TEST(ManifestParser, EmptyManifest) {
    auto manifest = parse_manifest("");
    EXPECT_TRUE(manifest.spritesheets.empty());
    EXPECT_TRUE(manifest.bitmap_fonts.empty());
    EXPECT_TRUE(manifest.fonts.empty());
}
