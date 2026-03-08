/// @file font.cpp
/// @brief BitmapFont and FreeType/PCF Font implementation.
#include "bdf_parser.hpp"
#include "pcf_parser.hpp"

#include <xebble/font.hpp>
#include <xebble/log.hpp>
#include <xebble/spritesheet.hpp>
#include <xebble/texture.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "utf8.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// BitmapFontData
// ---------------------------------------------------------------------------

BitmapFontData::BitmapFontData(uint32_t glyph_width, uint32_t glyph_height,
                               std::u8string_view charset)
    : glyph_width_(glyph_width),
      glyph_height_(glyph_height),
      charset_(reinterpret_cast<const char*>(charset.data()), charset.size()) {
    uint32_t i = 0;
    for (uint32_t cp : utf8::codepoints(charset))
        codepoint_map_[cp] = i++;
}

BitmapFontData::BitmapFontData(uint32_t glyph_width, uint32_t glyph_height)
    : glyph_width_(glyph_width), glyph_height_(glyph_height) {}

void BitmapFontData::add_glyph(uint32_t codepoint, uint32_t tile_index) {
    codepoint_map_[codepoint] = tile_index;
}

std::optional<uint32_t> BitmapFontData::glyph_index(char ch) const {
    return glyph_index(static_cast<uint32_t>(static_cast<uint8_t>(ch)));
}

std::optional<uint32_t> BitmapFontData::glyph_index(uint32_t codepoint) const {
    auto it = codepoint_map_.find(codepoint);
    if (it == codepoint_map_.end())
        return std::nullopt;
    return it->second;
}

// ---------------------------------------------------------------------------
// BitmapFont::Impl
// ---------------------------------------------------------------------------

struct BitmapFont::Impl {
    SpriteSheet sheet;
    BitmapFontData data;
    Impl(SpriteSheet s, BitmapFontData d) : sheet(std::move(s)), data(std::move(d)) {}
};

// ---------------------------------------------------------------------------
// BitmapFont::load_pcf  — fixed-cell atlas from a PCF file
// ---------------------------------------------------------------------------

std::expected<BitmapFont, Error> BitmapFont::load_pcf(vk::Context& ctx,
                                                      const std::filesystem::path& path) {
    auto buf_result = pcf::read_file(path);
    if (!buf_result)
        return std::unexpected(buf_result.error());
    auto& buf = *buf_result;

    pcf::Reader r{buf.data(), buf.size()};
    auto toc = pcf::read_toc(r);

    // --- Metrics ---
    auto* mt = pcf::find_table(toc, pcf::PCF_METRICS);
    if (!mt)
        return std::unexpected(Error{"PCF: no METRICS table"});
    auto metrics_result = pcf::read_metrics_table(r, *mt);
    if (!metrics_result)
        return std::unexpected(metrics_result.error());
    auto& metrics = *metrics_result;

    // Compute uniform cell size from max bounding box.
    int16_t max_ascent = 0, max_descent = 0, max_rb = 0, min_lb = 0;
    for (const auto& m : metrics) {
        max_ascent = std::max(max_ascent, m.ascent);
        max_descent = std::max(max_descent, m.descent);
        max_rb = std::max(max_rb, m.right_bearing);
        min_lb = std::min(min_lb, m.left_bearing);
    }
    uint32_t cell_w = static_cast<uint32_t>(max_rb - min_lb);
    uint32_t cell_h = static_cast<uint32_t>(max_ascent + max_descent);
    if (cell_w == 0 || cell_h == 0)
        return std::unexpected(Error{"PCF: zero cell dimensions"});

    // --- Bitmap info ---
    auto* bt = pcf::find_table(toc, pcf::PCF_BITMAPS);
    if (!bt)
        return std::unexpected(Error{"PCF: no BITMAPS table"});
    auto binfo_result = pcf::read_bitmap_info(r, *bt, metrics.size());
    if (!binfo_result)
        return std::unexpected(binfo_result.error());
    auto& binfo = *binfo_result;

    // Square-ish atlas: each glyph occupies one cell_w × cell_h slot.
    size_t num_glyphs = metrics.size();
    uint32_t cols = 1;
    while (static_cast<size_t>(cols) * cols < num_glyphs)
        ++cols;
    uint32_t rows_atlas = (static_cast<uint32_t>(num_glyphs) + cols - 1) / cols;
    uint32_t atlas_w = cols * cell_w;
    uint32_t atlas_h = rows_atlas * cell_h;

    std::vector<uint8_t> atlas(static_cast<size_t>(atlas_w) * atlas_h * 4, 0);

    for (size_t gi = 0; gi < num_glyphs; ++gi) {
        const auto& m = metrics[gi];
        int16_t bm_w = static_cast<int16_t>(m.right_bearing - m.left_bearing);
        int16_t bm_h = static_cast<int16_t>(m.ascent + m.descent);
        if (bm_w <= 0 || bm_h <= 0)
            continue;

        uint32_t row_stride = (((static_cast<uint32_t>(bm_w) + 7) / 8) + binfo.pad_bytes - 1) /
                              binfo.pad_bytes * binfo.pad_bytes;

        int32_t cell_x = -static_cast<int32_t>(m.left_bearing);
        int32_t cell_y = static_cast<int32_t>(max_ascent - m.ascent);
        uint32_t atlas_col = static_cast<uint32_t>(gi) % cols;
        uint32_t atlas_row = static_cast<uint32_t>(gi) / cols;
        uint32_t ax = atlas_col * cell_w + static_cast<uint32_t>(cell_x);
        uint32_t ay = atlas_row * cell_h + static_cast<uint32_t>(cell_y);
        size_t glyph_off = binfo.data_offset + binfo.offsets[gi];

        for (int16_t row = 0; row < bm_h; ++row) {
            for (int16_t col = 0; col < bm_w; ++col) {
                size_t byte_idx = glyph_off + static_cast<size_t>(row) * row_stride +
                                  static_cast<size_t>(col) / 8;
                if (byte_idx >= buf.size())
                    continue;
                uint8_t byte = buf[byte_idx];
                if (!binfo.msbit_first)
                    byte = pcf::reverse_bits(byte);
                if (!((byte >> (7 - (col % 8))) & 1))
                    continue;
                uint32_t px = ax + static_cast<uint32_t>(col);
                uint32_t py = ay + static_cast<uint32_t>(row);
                if (px >= atlas_w || py >= atlas_h)
                    continue;
                size_t dst = (static_cast<size_t>(py) * atlas_w + px) * 4;
                atlas[dst + 0] = atlas[dst + 1] = atlas[dst + 2] = atlas[dst + 3] = 255;
            }
        }
    }

    // --- Encodings → codepoint→tile map ---
    auto* et = pcf::find_table(toc, pcf::PCF_BDF_ENCODINGS);
    if (!et)
        return std::unexpected(Error{"PCF: no BDF_ENCODINGS table"});
    auto enc_result = pcf::read_encodings(r, *et, num_glyphs);
    if (!enc_result)
        return std::unexpected(enc_result.error());

    BitmapFontData font_data(cell_w, cell_h);
    for (auto& [cp, gi] : *enc_result)
        font_data.add_glyph(cp, gi);

    auto tex = Texture::create_from_pixels(ctx, atlas.data(), atlas_w, atlas_h);
    if (!tex)
        return std::unexpected(tex.error());
    auto sheet = SpriteSheet::from_texture(std::move(*tex), cell_w, cell_h);
    if (!sheet)
        return std::unexpected(sheet.error());

    BitmapFont font;
    font.impl_ = std::make_unique<Impl>(std::move(*sheet), std::move(font_data));
    return font;
}

// ---------------------------------------------------------------------------
// BitmapFont public API
// ---------------------------------------------------------------------------

std::expected<BitmapFont, Error>
BitmapFont::load(vk::Context& ctx, const std::filesystem::path& path, BitmapFontFormat fmt,
                 uint32_t glyph_width, uint32_t glyph_height, std::u8string_view charset) {
    if (fmt == BitmapFontFormat::Auto) {
        auto ext = path.extension().string();
        if (ext == ".pcf" || ext == ".gz")
            fmt = BitmapFontFormat::PCF;
        else if (ext == ".bdf")
            fmt = BitmapFontFormat::BDF;
        else
            fmt = BitmapFontFormat::PNG;
    }
    if (fmt == BitmapFontFormat::PCF)
        return load_pcf(ctx, path);
    if (fmt == BitmapFontFormat::BDF)
        return load_bdf(ctx, path);

    auto sheet = SpriteSheet::load(ctx, path, glyph_width, glyph_height);
    if (!sheet)
        return std::unexpected(sheet.error());
    BitmapFont font;
    font.impl_ = std::make_unique<Impl>(std::move(*sheet),
                                        BitmapFontData(glyph_width, glyph_height, charset));
    return font;
}

std::expected<BitmapFont, Error> BitmapFont::from_spritesheet(SpriteSheet sheet,
                                                              std::u8string_view charset) {
    uint32_t gw = sheet.tile_width(), gh = sheet.tile_height();
    BitmapFont font;
    font.impl_ = std::make_unique<Impl>(std::move(sheet), BitmapFontData(gw, gh, charset));
    return font;
}

std::expected<BitmapFont, Error> BitmapFont::from_data(SpriteSheet sheet, BitmapFontData data) {
    BitmapFont font;
    font.impl_ = std::make_unique<Impl>(std::move(sheet), std::move(data));
    return font;
}

BitmapFont::~BitmapFont() = default;
BitmapFont::BitmapFont(BitmapFont&&) noexcept = default;
BitmapFont& BitmapFont::operator=(BitmapFont&&) noexcept = default;

std::optional<uint32_t> BitmapFont::glyph_index(char ch) const {
    return impl_->data.glyph_index(ch);
}
std::optional<uint32_t> BitmapFont::glyph_index(uint32_t codepoint) const {
    return impl_->data.glyph_index(codepoint);
}
const SpriteSheet& BitmapFont::sheet() const {
    return impl_->sheet;
}
const BitmapFontData& BitmapFont::data() const {
    return impl_->data;
}
uint32_t BitmapFont::glyph_width() const {
    return impl_->data.glyph_width();
}
uint32_t BitmapFont::glyph_height() const {
    return impl_->data.glyph_height();
}

// ---------------------------------------------------------------------------
// Font::Impl
// ---------------------------------------------------------------------------

struct Font::Impl {
    Texture atlas;
    std::unordered_map<uint32_t, GlyphMetrics> glyphs;
    uint32_t pixel_size = 0;
    float line_height = 0.0f;
    float ascender = 0.0f; ///< Pixels from cell-top to baseline.
    Impl(Texture tex) : atlas(std::move(tex)) {}
};

// ---------------------------------------------------------------------------
// Font::load  — TrueType via FreeType
// ---------------------------------------------------------------------------

std::expected<Font, Error> Font::load(vk::Context& ctx, const std::filesystem::path& font_path,
                                      uint32_t pixel_size) {
    FT_Library ft;
    if (FT_Init_FreeType(&ft))
        return std::unexpected(Error{"Failed to init FreeType"});

    FT_Face face;
    if (FT_New_Face(ft, font_path.string().c_str(), 0, &face)) {
        FT_Done_FreeType(ft);
        return std::unexpected(Error{"Failed to load font: " + font_path.string()});
    }
    FT_Set_Pixel_Sizes(face, 0, pixel_size);

    constexpr int FIRST_CHAR = 32;
    constexpr int LAST_CHAR = 126;
    constexpr int CHAR_COUNT = LAST_CHAR - FIRST_CHAR + 1;

    struct GlyphBitmap {
        std::vector<uint8_t> pixels;
        uint32_t width = 0, height = 0;
        GlyphMetrics metrics;
    };
    std::vector<GlyphBitmap> bitmaps(CHAR_COUNT);
    uint32_t total_width = 0, max_height = 0;

    for (int i = 0; i < CHAR_COUNT; ++i) {
        if (FT_Load_Char(face, static_cast<char>(FIRST_CHAR + i), FT_LOAD_RENDER))
            continue;
        auto& g = face->glyph;
        auto& bm = bitmaps[i];
        bm.width = g->bitmap.width;
        bm.height = g->bitmap.rows;
        bm.metrics = {
            .advance = static_cast<float>(g->advance.x >> 6),
            .bearing_x = static_cast<float>(g->bitmap_left),
            .bearing_y = static_cast<float>(g->bitmap_top),
            .width = static_cast<float>(bm.width),
            .height = static_cast<float>(bm.height),
        };
        if (bm.width > 0 && bm.height > 0) {
            bm.pixels.resize(static_cast<size_t>(bm.width) * bm.height);
            std::copy_n(g->bitmap.buffer, bm.pixels.size(), bm.pixels.begin());
        }
        total_width += bm.width + 1;
        max_height = std::max(max_height, bm.height);
    }

    uint32_t atlas_w = 1;
    while (atlas_w < total_width)
        atlas_w *= 2;
    atlas_w = std::min(atlas_w, 2048u);

    uint32_t row_h = max_height + 1, rows_needed = 1;
    {
        uint32_t cx = 0;
        for (int i = 0; i < CHAR_COUNT; ++i) {
            if (cx + bitmaps[i].width + 1 > atlas_w) {
                cx = 0;
                ++rows_needed;
            }
            cx += bitmaps[i].width + 1;
        }
    }
    uint32_t atlas_h = 1;
    while (atlas_h < rows_needed * row_h)
        atlas_h *= 2;

    std::vector<uint8_t> atlas_pixels(static_cast<size_t>(atlas_w) * atlas_h * 4, 0);
    uint32_t cx = 0, cy = 0;
    float aw = static_cast<float>(atlas_w), ah = static_cast<float>(atlas_h);
    std::unordered_map<uint32_t, GlyphMetrics> glyph_map;

    for (int i = 0; i < CHAR_COUNT; ++i) {
        auto& bm = bitmaps[i];
        if (cx + bm.width + 1 > atlas_w) {
            cx = 0;
            cy += row_h;
        }

        for (uint32_t y = 0; y < bm.height; ++y) {
            for (uint32_t x = 0; x < bm.width; ++x) {
                uint8_t a = bm.pixels[y * bm.width + x];
                size_t d = (static_cast<size_t>(cy + y) * atlas_w + cx + x) * 4;
                atlas_pixels[d + 0] = atlas_pixels[d + 1] = atlas_pixels[d + 2] = 255;
                atlas_pixels[d + 3] = a;
            }
        }
        bm.metrics.uv = Rect{
            .x = static_cast<float>(cx) / aw,
            .y = static_cast<float>(cy) / ah,
            .w = static_cast<float>(bm.width) / aw,
            .h = static_cast<float>(bm.height) / ah,
        };
        glyph_map[static_cast<uint32_t>(FIRST_CHAR + i)] = bm.metrics;
        cx += bm.width + 1;
    }

    float line_h = static_cast<float>(face->size->metrics.height >> 6);
    float asc_px = static_cast<float>(face->size->metrics.ascender >> 6);
    float dsc_px = static_cast<float>(-(face->size->metrics.descender >> 6)); // positive
    // Use true cell height instead of FreeType's line_height (which may omit
    // the descender row when lineGap=0 and ascender+|descender| > height).
    float cell_h = asc_px + dsc_px;
    if (cell_h > line_h)
        line_h = cell_h;
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    auto tex = Texture::create_from_pixels(ctx, atlas_pixels.data(), atlas_w, atlas_h);
    if (!tex)
        return std::unexpected(tex.error());

    Font font;
    font.impl_ = std::make_unique<Impl>(std::move(*tex));
    font.impl_->glyphs = std::move(glyph_map);
    font.impl_->pixel_size = pixel_size;
    font.impl_->line_height = line_h;
    font.impl_->ascender = asc_px;
    return font;
}

// ---------------------------------------------------------------------------
// BitmapFont::load_bdf  — fixed-cell atlas from a BDF file
// ---------------------------------------------------------------------------

std::expected<BitmapFont, Error> BitmapFont::load_bdf(vk::Context& ctx,
                                                      const std::filesystem::path& path) {
    auto parsed = bdf::parse(path);
    if (!parsed)
        return std::unexpected(parsed.error());
    const auto& bf = *parsed;

    // Cell size = FONTBOUNDINGBOX.
    uint32_t cell_w = static_cast<uint32_t>(bf.bbox_w);
    uint32_t cell_h = static_cast<uint32_t>(bf.bbox_h);
    if (cell_w == 0 || cell_h == 0)
        return std::unexpected(Error{"BDF: zero cell dimensions in " + path.string()});

    int32_t font_ascent = bf.font_ascent;

    size_t num_glyphs = bf.glyphs.size();
    uint32_t cols = 1;
    while (static_cast<size_t>(cols) * cols < num_glyphs)
        ++cols;
    uint32_t rows_atlas = (static_cast<uint32_t>(num_glyphs) + cols - 1) / cols;
    uint32_t atlas_w = cols * cell_w;
    uint32_t atlas_h = rows_atlas * cell_h;

    std::vector<uint8_t> atlas(static_cast<size_t>(atlas_w) * atlas_h * 4, 0);

    for (size_t gi = 0; gi < num_glyphs; ++gi) {
        const auto& g = bf.glyphs[gi];
        if (g.bbx_w <= 0 || g.bbx_h <= 0)
            continue;
        if (g.codepoint == 0xFFFFFFFFu)
            continue; // unmapped

        // BDF BBX origin: bbx_x pixels right of cell left, bbx_y pixels above baseline.
        // Cell baseline sits at (font_ascent) rows from top of cell.
        int32_t cell_col = g.bbx_x;
        int32_t cell_row = font_ascent - (g.bbx_y + g.bbx_h);

        uint32_t atlas_col = static_cast<uint32_t>(gi) % cols;
        uint32_t atlas_row = static_cast<uint32_t>(gi) / cols;

        for (int32_t row = 0; row < g.bbx_h; ++row) {
            for (int32_t col = 0; col < g.bbx_w; ++col) {
                if (!bdf::sample(g, col, row))
                    continue;
                int32_t px = static_cast<int32_t>(atlas_col * cell_w) + cell_col + col;
                int32_t py = static_cast<int32_t>(atlas_row * cell_h) + cell_row + row;
                if (px < 0 || py < 0)
                    continue;
                if (static_cast<uint32_t>(px) >= atlas_w || static_cast<uint32_t>(py) >= atlas_h)
                    continue;
                size_t dst = (static_cast<size_t>(py) * atlas_w + static_cast<size_t>(px)) * 4;
                atlas[dst + 0] = atlas[dst + 1] = atlas[dst + 2] = atlas[dst + 3] = 255;
            }
        }
    }

    BitmapFontData font_data(cell_w, cell_h);
    for (size_t gi = 0; gi < num_glyphs; ++gi) {
        const auto& g = bf.glyphs[gi];
        if (g.codepoint != 0xFFFFFFFFu)
            font_data.add_glyph(g.codepoint, static_cast<uint32_t>(gi));
    }

    auto tex = Texture::create_from_pixels(ctx, atlas.data(), atlas_w, atlas_h);
    if (!tex)
        return std::unexpected(tex.error());
    auto sheet = SpriteSheet::from_texture(std::move(*tex), cell_w, cell_h);
    if (!sheet)
        return std::unexpected(sheet.error());

    BitmapFont font;
    font.impl_ = std::make_unique<Impl>(std::move(*sheet), std::move(font_data));
    return font;
}

// ---------------------------------------------------------------------------
// Font::load_pcf  — proportional bitmap font from a PCF file
// ---------------------------------------------------------------------------

std::expected<Font, Error> Font::load_pcf(vk::Context& ctx, const std::filesystem::path& path) {
    auto buf_result = pcf::read_file(path);
    if (!buf_result)
        return std::unexpected(buf_result.error());
    auto& buf = *buf_result;

    pcf::Reader r{buf.data(), buf.size()};
    auto toc = pcf::read_toc(r);

    // --- Metrics ---
    auto* mt = pcf::find_table(toc, pcf::PCF_METRICS);
    if (!mt)
        return std::unexpected(Error{"PCF: no METRICS table"});
    auto metrics_result = pcf::read_metrics_table(r, *mt);
    if (!metrics_result)
        return std::unexpected(metrics_result.error());
    auto& metrics = *metrics_result;

    int16_t max_ascent = 0, max_descent = 0;
    for (const auto& m : metrics) {
        max_ascent = std::max(max_ascent, m.ascent);
        max_descent = std::max(max_descent, m.descent);
    }
    float line_h = static_cast<float>(max_ascent + max_descent);

    // --- Bitmap info ---
    auto* bt = pcf::find_table(toc, pcf::PCF_BITMAPS);
    if (!bt)
        return std::unexpected(Error{"PCF: no BITMAPS table"});
    auto binfo_result = pcf::read_bitmap_info(r, *bt, metrics.size());
    if (!binfo_result)
        return std::unexpected(binfo_result.error());
    auto& binfo = *binfo_result;

    // Decode each glyph into its ink bounding box.
    struct GlyphBitmap {
        std::vector<uint8_t> pixels; // RGBA, ink_w × ink_h
        uint32_t ink_w = 0, ink_h = 0;
        GlyphMetrics metrics;
    };

    size_t num_glyphs = metrics.size();
    std::vector<GlyphBitmap> bitmaps(num_glyphs);
    uint32_t total_width = 0, max_h = 0;

    for (size_t gi = 0; gi < num_glyphs; ++gi) {
        const auto& m = metrics[gi];
        int16_t ink_w = static_cast<int16_t>(m.right_bearing - m.left_bearing);
        int16_t ink_h = static_cast<int16_t>(m.ascent + m.descent);
        auto& gb = bitmaps[gi];

        gb.metrics = {
            .advance = static_cast<float>(m.width),
            .bearing_x = static_cast<float>(m.left_bearing),
            .bearing_y = static_cast<float>(m.ascent),
            .width = static_cast<float>(std::max(int16_t{0}, ink_w)),
            .height = static_cast<float>(std::max(int16_t{0}, ink_h)),
        };
        if (ink_w <= 0 || ink_h <= 0)
            continue;

        gb.ink_w = static_cast<uint32_t>(ink_w);
        gb.ink_h = static_cast<uint32_t>(ink_h);
        uint32_t row_stride =
            (((gb.ink_w + 7) / 8) + binfo.pad_bytes - 1) / binfo.pad_bytes * binfo.pad_bytes;

        gb.pixels.resize(static_cast<size_t>(gb.ink_w) * gb.ink_h * 4, 0);
        size_t glyph_off = binfo.data_offset + binfo.offsets[gi];

        for (uint32_t row = 0; row < gb.ink_h; ++row) {
            for (uint32_t col = 0; col < gb.ink_w; ++col) {
                size_t byte_idx = glyph_off + row * row_stride + col / 8;
                if (byte_idx >= buf.size())
                    continue;
                uint8_t byte = buf[byte_idx];
                if (!binfo.msbit_first)
                    byte = pcf::reverse_bits(byte);
                if (!((byte >> (7 - (col % 8))) & 1))
                    continue;
                size_t dst = (static_cast<size_t>(row) * gb.ink_w + col) * 4;
                bitmaps[gi].pixels[dst + 0] = bitmaps[gi].pixels[dst + 1] =
                    bitmaps[gi].pixels[dst + 2] = bitmaps[gi].pixels[dst + 3] = 255;
            }
        }
        total_width += gb.ink_w + 1;
        max_h = std::max(max_h, gb.ink_h);
    }

    // Row-pack into atlas.
    uint32_t atlas_w = 1;
    while (atlas_w < total_width)
        atlas_w *= 2;
    atlas_w = std::min(atlas_w, 2048u);

    uint32_t row_height = max_h + 1, rows_needed = 1;
    {
        uint32_t cxp = 0;
        for (size_t gi = 0; gi < num_glyphs; ++gi) {
            uint32_t w = bitmaps[gi].ink_w;
            if (w == 0)
                continue;
            if (cxp + w + 1 > atlas_w) {
                cxp = 0;
                ++rows_needed;
            }
            cxp += w + 1;
        }
    }
    uint32_t atlas_h = 1;
    while (atlas_h < rows_needed * row_height)
        atlas_h *= 2;

    std::vector<uint8_t> atlas_pixels(static_cast<size_t>(atlas_w) * atlas_h * 4, 0);
    uint32_t cx = 0, cy = 0;
    float aw = static_cast<float>(atlas_w), ah = static_cast<float>(atlas_h);

    for (size_t gi = 0; gi < num_glyphs; ++gi) {
        auto& gb = bitmaps[gi];
        if (gb.ink_w == 0 || gb.ink_h == 0)
            continue;
        if (cx + gb.ink_w + 1 > atlas_w) {
            cx = 0;
            cy += row_height;
        }

        for (uint32_t row = 0; row < gb.ink_h; ++row) {
            for (uint32_t col = 0; col < gb.ink_w; ++col) {
                size_t src = (static_cast<size_t>(row) * gb.ink_w + col) * 4;
                size_t dst = (static_cast<size_t>(cy + row) * atlas_w + cx + col) * 4;
                atlas_pixels[dst + 0] = gb.pixels[src + 0];
                atlas_pixels[dst + 1] = gb.pixels[src + 1];
                atlas_pixels[dst + 2] = gb.pixels[src + 2];
                atlas_pixels[dst + 3] = gb.pixels[src + 3];
            }
        }
        gb.metrics.uv = Rect{
            .x = static_cast<float>(cx) / aw,
            .y = static_cast<float>(cy) / ah,
            .w = static_cast<float>(gb.ink_w) / aw,
            .h = static_cast<float>(gb.ink_h) / ah,
        };
        cx += gb.ink_w + 1;
    }

    // --- Encodings → codepoint → GlyphMetrics ---
    auto* et = pcf::find_table(toc, pcf::PCF_BDF_ENCODINGS);
    if (!et)
        return std::unexpected(Error{"PCF: no BDF_ENCODINGS table"});
    auto enc_result = pcf::read_encodings(r, *et, num_glyphs);
    if (!enc_result)
        return std::unexpected(enc_result.error());

    std::unordered_map<uint32_t, GlyphMetrics> glyph_map;
    for (auto& [cp, gi] : *enc_result)
        glyph_map[cp] = bitmaps[gi].metrics;

    auto tex = Texture::create_from_pixels(ctx, atlas_pixels.data(), atlas_w, atlas_h);
    if (!tex)
        return std::unexpected(tex.error());

    Font font;
    font.impl_ = std::make_unique<Impl>(std::move(*tex));
    font.impl_->glyphs = std::move(glyph_map);
    font.impl_->pixel_size = static_cast<uint32_t>(max_ascent + max_descent);
    font.impl_->line_height = line_h;
    font.impl_->ascender = static_cast<float>(max_ascent);
    return font;
}

// ---------------------------------------------------------------------------
// Font::load_bdf  — proportional bitmap font from a BDF file
// ---------------------------------------------------------------------------

std::expected<Font, Error> Font::load_bdf(vk::Context& ctx, const std::filesystem::path& path) {
    auto parsed = bdf::parse(path);
    if (!parsed)
        return std::unexpected(parsed.error());
    const auto& bf = *parsed;

    int32_t font_ascent = bf.font_ascent;
    int32_t font_descent = bf.font_descent;
    float line_h = static_cast<float>(font_ascent + font_descent);

    // Decode each glyph into its ink bounding box (RGBA pixels).
    struct GlyphBitmap {
        std::vector<uint8_t> pixels;
        uint32_t ink_w = 0, ink_h = 0;
        GlyphMetrics metrics;
    };

    size_t num_glyphs = bf.glyphs.size();
    std::vector<GlyphBitmap> bitmaps(num_glyphs);
    uint32_t total_width = 0, max_h = 0;

    for (size_t gi = 0; gi < num_glyphs; ++gi) {
        const auto& g = bf.glyphs[gi];
        auto& gb = bitmaps[gi];

        gb.metrics = {
            .advance = static_cast<float>(g.advance),
            .bearing_x = static_cast<float>(g.bbx_x),
            .bearing_y = static_cast<float>(g.bbx_y + g.bbx_h), // top of glyph above baseline
            .width = static_cast<float>(std::max(0, g.bbx_w)),
            .height = static_cast<float>(std::max(0, g.bbx_h)),
        };
        if (g.bbx_w <= 0 || g.bbx_h <= 0 || g.codepoint == 0xFFFFFFFFu)
            continue;

        gb.ink_w = static_cast<uint32_t>(g.bbx_w);
        gb.ink_h = static_cast<uint32_t>(g.bbx_h);
        gb.pixels.resize(static_cast<size_t>(gb.ink_w) * gb.ink_h * 4, 0);

        for (int32_t row = 0; row < g.bbx_h; ++row) {
            for (int32_t col = 0; col < g.bbx_w; ++col) {
                if (!bdf::sample(g, col, row))
                    continue;
                size_t dst = (static_cast<size_t>(row) * gb.ink_w + static_cast<size_t>(col)) * 4;
                gb.pixels[dst + 0] = gb.pixels[dst + 1] = gb.pixels[dst + 2] = gb.pixels[dst + 3] =
                    255;
            }
        }
        total_width += gb.ink_w + 1;
        max_h = std::max(max_h, gb.ink_h);
    }

    // Row-pack into atlas (same algorithm as load_pcf / load TTF).
    uint32_t atlas_w = 1;
    while (atlas_w < total_width)
        atlas_w *= 2;
    atlas_w = std::min(atlas_w, 2048u);

    uint32_t row_height = max_h + 1, rows_needed = 1;
    {
        uint32_t cxp = 0;
        for (size_t gi = 0; gi < num_glyphs; ++gi) {
            uint32_t w = bitmaps[gi].ink_w;
            if (w == 0)
                continue;
            if (cxp + w + 1 > atlas_w) {
                cxp = 0;
                ++rows_needed;
            }
            cxp += w + 1;
        }
    }
    uint32_t atlas_h = 1;
    while (atlas_h < rows_needed * row_height)
        atlas_h *= 2;

    std::vector<uint8_t> atlas_pixels(static_cast<size_t>(atlas_w) * atlas_h * 4, 0);
    uint32_t cx = 0, cy = 0;
    float aw = static_cast<float>(atlas_w), ah = static_cast<float>(atlas_h);

    for (size_t gi = 0; gi < num_glyphs; ++gi) {
        auto& gb = bitmaps[gi];
        if (gb.ink_w == 0 || gb.ink_h == 0)
            continue;
        if (cx + gb.ink_w + 1 > atlas_w) {
            cx = 0;
            cy += row_height;
        }

        for (uint32_t row = 0; row < gb.ink_h; ++row) {
            for (uint32_t col = 0; col < gb.ink_w; ++col) {
                size_t src = (static_cast<size_t>(row) * gb.ink_w + col) * 4;
                size_t dst = (static_cast<size_t>(cy + row) * atlas_w + cx + col) * 4;
                atlas_pixels[dst + 0] = gb.pixels[src + 0];
                atlas_pixels[dst + 1] = gb.pixels[src + 1];
                atlas_pixels[dst + 2] = gb.pixels[src + 2];
                atlas_pixels[dst + 3] = gb.pixels[src + 3];
            }
        }
        gb.metrics.uv = Rect{
            .x = static_cast<float>(cx) / aw,
            .y = static_cast<float>(cy) / ah,
            .w = static_cast<float>(gb.ink_w) / aw,
            .h = static_cast<float>(gb.ink_h) / ah,
        };
        cx += gb.ink_w + 1;
    }

    // Build codepoint → GlyphMetrics map.
    std::unordered_map<uint32_t, GlyphMetrics> glyph_map;
    for (size_t gi = 0; gi < num_glyphs; ++gi) {
        const auto& g = bf.glyphs[gi];
        if (g.codepoint != 0xFFFFFFFFu)
            glyph_map[g.codepoint] = bitmaps[gi].metrics;
    }

    auto tex = Texture::create_from_pixels(ctx, atlas_pixels.data(), atlas_w, atlas_h);
    if (!tex)
        return std::unexpected(tex.error());

    Font font;
    font.impl_ = std::make_unique<Impl>(std::move(*tex));
    font.impl_->glyphs = std::move(glyph_map);
    font.impl_->pixel_size = static_cast<uint32_t>(font_ascent + font_descent);
    font.impl_->line_height = line_h;
    font.impl_->ascender = static_cast<float>(font_ascent);
    return font;
}

// ---------------------------------------------------------------------------
// Font::from_atlas  — construct from pre-built atlas (generated embedded fonts)
// ---------------------------------------------------------------------------

std::expected<Font, Error> Font::from_atlas(Texture atlas,
                                            std::unordered_map<uint32_t, GlyphMetrics> glyphs,
                                            uint32_t pixel_size, float line_height,
                                            float ascender) {
    Font font;
    font.impl_ = std::make_unique<Impl>(std::move(atlas));
    font.impl_->glyphs = std::move(glyphs);
    font.impl_->pixel_size = pixel_size;
    font.impl_->line_height = line_height;
    font.impl_->ascender = (ascender > 0.0f) ? ascender : line_height;
    return font;
}

// ---------------------------------------------------------------------------
// Font public API
// ---------------------------------------------------------------------------

Font::~Font() = default;
Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;

std::optional<GlyphMetrics> Font::glyph(char ch) const {
    return glyph(static_cast<uint32_t>(static_cast<uint8_t>(ch)));
}
std::optional<GlyphMetrics> Font::glyph(uint32_t codepoint) const {
    auto it = impl_->glyphs.find(codepoint);
    if (it == impl_->glyphs.end())
        return std::nullopt;
    return it->second;
}
const Texture& Font::texture() const {
    return impl_->atlas;
}
uint32_t Font::pixel_size() const {
    return impl_->pixel_size;
}
float Font::line_height() const {
    return impl_->line_height;
}
float Font::ascender() const {
    return impl_->ascender;
}

} // namespace xebble
