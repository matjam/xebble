/// @file font.cpp
/// @brief BitmapFont and FreeType Font implementation.
#include <xebble/font.hpp>
#include <xebble/spritesheet.hpp>
#include <xebble/log.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace xebble {

// --- BitmapFontData ---

BitmapFontData::BitmapFontData(uint32_t glyph_width, uint32_t glyph_height, std::string_view charset)
    : glyph_width_(glyph_width), glyph_height_(glyph_height), charset_(charset)
{
}

std::optional<uint32_t> BitmapFontData::glyph_index(char ch) const {
    auto pos = charset_.find(ch);
    if (pos == std::string::npos) return std::nullopt;
    return static_cast<uint32_t>(pos);
}

// --- BitmapFont ---

struct BitmapFont::Impl {
    SpriteSheet sheet;
    BitmapFontData data;

    Impl(SpriteSheet s, BitmapFontData d)
        : sheet(std::move(s)), data(std::move(d)) {}
};

std::expected<BitmapFont, Error> BitmapFont::load(
    vk::Context& ctx,
    const std::filesystem::path& image_path,
    uint32_t glyph_width, uint32_t glyph_height,
    std::string_view charset)
{
    auto sheet = SpriteSheet::load(ctx, image_path, glyph_width, glyph_height);
    if (!sheet) return std::unexpected(sheet.error());

    BitmapFont font;
    font.impl_ = std::make_unique<Impl>(
        std::move(*sheet),
        BitmapFontData(glyph_width, glyph_height, charset));
    return font;
}

std::expected<BitmapFont, Error> BitmapFont::from_spritesheet(
    SpriteSheet sheet, std::string_view charset)
{
    uint32_t gw = sheet.tile_width();
    uint32_t gh = sheet.tile_height();
    BitmapFont font;
    font.impl_ = std::make_unique<Impl>(
        std::move(sheet),
        BitmapFontData(gw, gh, charset));
    return font;
}

BitmapFont::~BitmapFont() = default;
BitmapFont::BitmapFont(BitmapFont&&) noexcept = default;
BitmapFont& BitmapFont::operator=(BitmapFont&&) noexcept = default;

std::optional<uint32_t> BitmapFont::glyph_index(char ch) const {
    return impl_->data.glyph_index(ch);
}

const SpriteSheet& BitmapFont::sheet() const { return impl_->sheet; }
const BitmapFontData& BitmapFont::data() const { return impl_->data; }
uint32_t BitmapFont::glyph_width() const { return impl_->data.glyph_width(); }
uint32_t BitmapFont::glyph_height() const { return impl_->data.glyph_height(); }

// --- Font (FreeType) ---

struct Font::Impl {
    Texture atlas;
    std::unordered_map<char, GlyphMetrics> glyphs;
    uint32_t pixel_size = 0;
    float line_height = 0.0f;

    Impl(Texture tex) : atlas(std::move(tex)) {}
};

std::expected<Font, Error> Font::load(
    vk::Context& ctx,
    const std::filesystem::path& font_path,
    uint32_t pixel_size)
{
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        return std::unexpected(Error{"Failed to init FreeType"});
    }

    FT_Face face;
    if (FT_New_Face(ft, font_path.string().c_str(), 0, &face)) {
        FT_Done_FreeType(ft);
        return std::unexpected(Error{"Failed to load font: " + font_path.string()});
    }

    FT_Set_Pixel_Sizes(face, 0, pixel_size);

    // Determine atlas size — rasterize printable ASCII (32-126)
    constexpr int FIRST_CHAR = 32;
    constexpr int LAST_CHAR = 126;
    constexpr int CHAR_COUNT = LAST_CHAR - FIRST_CHAR + 1;

    // First pass: measure total area needed
    struct GlyphBitmap {
        std::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        GlyphMetrics metrics;
    };
    std::vector<GlyphBitmap> bitmaps(CHAR_COUNT);

    uint32_t max_height = 0;
    uint32_t total_width = 0;

    for (int i = 0; i < CHAR_COUNT; i++) {
        char ch = static_cast<char>(FIRST_CHAR + i);
        if (FT_Load_Char(face, ch, FT_LOAD_RENDER)) continue;

        auto& g = face->glyph;
        auto& bm = bitmaps[i];
        bm.width = g->bitmap.width;
        bm.height = g->bitmap.rows;
        bm.metrics.advance = static_cast<float>(g->advance.x >> 6);
        bm.metrics.bearing_x = static_cast<float>(g->bitmap_left);
        bm.metrics.bearing_y = static_cast<float>(g->bitmap_top);
        bm.metrics.width = static_cast<float>(bm.width);
        bm.metrics.height = static_cast<float>(bm.height);

        if (bm.width > 0 && bm.height > 0) {
            bm.pixels.resize(static_cast<size_t>(bm.width) * bm.height);
            std::copy_n(g->bitmap.buffer, bm.pixels.size(), bm.pixels.begin());
        }

        total_width += bm.width + 1; // 1px padding
        max_height = std::max(max_height, bm.height);
    }

    // Calculate atlas dimensions (power-of-2 friendly)
    uint32_t atlas_width = 1;
    while (atlas_width < total_width) atlas_width *= 2;
    atlas_width = std::min(atlas_width, 2048u);

    // Pack into rows
    uint32_t row_height = max_height + 1;
    uint32_t rows_needed = 1;
    uint32_t cursor_x = 0;
    for (int i = 0; i < CHAR_COUNT; i++) {
        if (cursor_x + bitmaps[i].width + 1 > atlas_width) {
            cursor_x = 0;
            rows_needed++;
        }
        cursor_x += bitmaps[i].width + 1;
    }

    uint32_t atlas_height = 1;
    while (atlas_height < rows_needed * row_height) atlas_height *= 2;

    // Second pass: blit into atlas
    std::vector<uint8_t> atlas_pixels(static_cast<size_t>(atlas_width) * atlas_height * 4, 0);
    cursor_x = 0;
    uint32_t cursor_y = 0;

    std::unordered_map<char, GlyphMetrics> glyph_map;

    for (int i = 0; i < CHAR_COUNT; i++) {
        auto& bm = bitmaps[i];
        if (cursor_x + bm.width + 1 > atlas_width) {
            cursor_x = 0;
            cursor_y += row_height;
        }

        // Blit glyph (grayscale → RGBA)
        for (uint32_t y = 0; y < bm.height; y++) {
            for (uint32_t x = 0; x < bm.width; x++) {
                uint8_t alpha = bm.pixels[y * bm.width + x];
                size_t dst = (static_cast<size_t>(cursor_y + y) * atlas_width + cursor_x + x) * 4;
                atlas_pixels[dst + 0] = 255;
                atlas_pixels[dst + 1] = 255;
                atlas_pixels[dst + 2] = 255;
                atlas_pixels[dst + 3] = alpha;
            }
        }

        // Store UV coordinates
        float aw = static_cast<float>(atlas_width);
        float ah = static_cast<float>(atlas_height);
        bm.metrics.uv = Rect{
            .x = static_cast<float>(cursor_x) / aw,
            .y = static_cast<float>(cursor_y) / ah,
            .w = static_cast<float>(bm.width) / aw,
            .h = static_cast<float>(bm.height) / ah,
        };

        char ch = static_cast<char>(FIRST_CHAR + i);
        glyph_map[ch] = bm.metrics;

        cursor_x += bm.width + 1;
    }

    float line_h = static_cast<float>(face->size->metrics.height >> 6);

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    // Create atlas texture
    auto tex = Texture::create_from_pixels(ctx, atlas_pixels.data(),
        atlas_width, atlas_height);
    if (!tex) return std::unexpected(tex.error());

    Font font;
    font.impl_ = std::make_unique<Impl>(std::move(*tex));
    font.impl_->glyphs = std::move(glyph_map);
    font.impl_->pixel_size = pixel_size;
    font.impl_->line_height = line_h;
    return font;
}

Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

std::optional<GlyphMetrics> Font::glyph(char ch) const {
    auto it = impl_->glyphs.find(ch);
    if (it == impl_->glyphs.end()) return std::nullopt;
    return it->second;
}

const Texture& Font::texture() const { return impl_->atlas; }
uint32_t Font::pixel_size() const { return impl_->pixel_size; }
float Font::line_height() const { return impl_->line_height; }

} // namespace xebble
