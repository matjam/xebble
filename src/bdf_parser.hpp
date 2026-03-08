/// @file bdf_parser.hpp
/// @brief Internal BDF (Bitmap Distribution Format) text-format parser.
///
/// Parses Adobe BDF 2.1 font files into a simple in-memory representation
/// shared by BitmapFont::load_bdf and Font::load_bdf.
///
/// Not part of the public API. Include only from font.cpp.
#pragma once

#include <xebble/types.hpp>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace xebble::bdf {

// ---------------------------------------------------------------------------
// Per-glyph data
// ---------------------------------------------------------------------------

struct Glyph {
    uint32_t codepoint = 0;
    int32_t advance = 0;         ///< DWIDTH x value (pixel advance).
    int32_t bbx_w = 0;           ///< BBX width  (ink bounding box).
    int32_t bbx_h = 0;           ///< BBX height.
    int32_t bbx_x = 0;           ///< BBX x offset from origin.
    int32_t bbx_y = 0;           ///< BBX y offset from baseline (positive = above).
    std::vector<uint8_t> bitmap; ///< Row-major, MSBit first, one byte per 8px column.
};

// ---------------------------------------------------------------------------
// Whole-font data
// ---------------------------------------------------------------------------

struct Font {
    int32_t font_ascent = 0;  ///< Global FONT_ASCENT.
    int32_t font_descent = 0; ///< Global FONT_DESCENT.
    int32_t bbox_w = 0;       ///< Global FONTBOUNDINGBOX width.
    int32_t bbox_h = 0;       ///< Global FONTBOUNDINGBOX height.
    std::vector<Glyph> glyphs;
};

// ---------------------------------------------------------------------------
// Tiny line-oriented parser
// ---------------------------------------------------------------------------

inline std::expected<Font, xebble::Error> parse(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f)
        return std::unexpected(xebble::Error{"BDF: cannot open " + path.string()});

    Font font;
    std::string line;

    // Verify STARTFONT header.
    if (!std::getline(f, line) || line.rfind("STARTFONT", 0) != 0)
        return std::unexpected(xebble::Error{"BDF: missing STARTFONT header in " + path.string()});

    bool in_char = false;
    bool in_bitmap = false;
    Glyph cur;
    int bitmap_rows_expected = 0;

    auto next_int = [](std::istringstream& ss) -> int32_t {
        int32_t v = 0;
        ss >> v;
        return v;
    };

    while (std::getline(f, line)) {
        // Strip trailing CR (Windows line endings).
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (in_bitmap) {
            if (line.rfind("ENDCHAR", 0) == 0) {
                in_bitmap = false;
                in_char = false;
                font.glyphs.push_back(std::move(cur));
                cur = {};
                bitmap_rows_expected = 0;
            } else {
                // Each hex line is one row of the glyph bitmap.
                // Convert hex string → bytes (MSBit first, as per BDF spec).
                std::vector<uint8_t> row_bytes;
                // Parse pairs of hex chars.
                size_t i = 0;
                while (i + 1 < line.size()) {
                    auto hi_c = line[i], lo_c = line[i + 1];
                    // Skip non-hex chars (e.g. spaces).
                    auto is_hex = [](char c) {
                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                               (c >= 'A' && c <= 'F');
                    };
                    if (!is_hex(hi_c) || !is_hex(lo_c)) {
                        ++i;
                        continue;
                    }
                    auto hex = [](char c) -> uint8_t {
                        if (c >= '0' && c <= '9')
                            return static_cast<uint8_t>(c - '0');
                        if (c >= 'a' && c <= 'f')
                            return static_cast<uint8_t>(c - 'a' + 10);
                        return static_cast<uint8_t>(c - 'A' + 10);
                    };
                    row_bytes.push_back(static_cast<uint8_t>((hex(hi_c) << 4) | hex(lo_c)));
                    i += 2;
                }
                for (auto b : row_bytes)
                    cur.bitmap.push_back(b);
            }
            continue;
        }

        if (line.rfind("STARTCHAR", 0) == 0) {
            in_char = true;
            cur = {};
            continue;
        }

        if (!in_char) {
            // Global font properties.
            if (line.rfind("FONT_ASCENT", 0) == 0) {
                std::istringstream ss(line.substr(11));
                font.font_ascent = next_int(ss);
            } else if (line.rfind("FONT_DESCENT", 0) == 0) {
                std::istringstream ss(line.substr(12));
                font.font_descent = next_int(ss);
            } else if (line.rfind("FONTBOUNDINGBOX", 0) == 0) {
                std::istringstream ss(line.substr(15));
                font.bbox_w = next_int(ss);
                font.bbox_h = next_int(ss);
                // x_off and y_off follow but we don't need them globally.
            }
            continue;
        }

        // Per-glyph keywords.
        if (line.rfind("ENCODING", 0) == 0) {
            std::istringstream ss(line.substr(8));
            int32_t enc = next_int(ss);
            cur.codepoint = (enc >= 0) ? static_cast<uint32_t>(enc) : 0xFFFFFFFFu;
        } else if (line.rfind("DWIDTH", 0) == 0) {
            std::istringstream ss(line.substr(6));
            cur.advance = next_int(ss); // only x component.
        } else if (line.rfind("BBX", 0) == 0) {
            std::istringstream ss(line.substr(3));
            cur.bbx_w = next_int(ss);
            cur.bbx_h = next_int(ss);
            cur.bbx_x = next_int(ss);
            cur.bbx_y = next_int(ss);
            bitmap_rows_expected = cur.bbx_h;
        } else if (line.rfind("BITMAP", 0) == 0) {
            in_bitmap = true;
            cur.bitmap.reserve(static_cast<size_t>(bitmap_rows_expected) *
                               static_cast<size_t>((cur.bbx_w + 7) / 8));
        } else if (line.rfind("ENDCHAR", 0) == 0) {
            // Reached without BITMAP (e.g. empty glyph).
            in_char = false;
            font.glyphs.push_back(std::move(cur));
            cur = {};
        }
        // STARTFONT, FONT, SIZE, METRICSSET, SWIDTH, etc. — ignored.
    }

    if (font.glyphs.empty())
        return std::unexpected(xebble::Error{"BDF: no glyphs found in " + path.string()});

    // If global bounding box not given, derive from glyph metrics.
    if (font.bbox_w == 0 || font.bbox_h == 0) {
        for (const auto& g : font.glyphs) {
            font.bbox_w = std::max(font.bbox_w, g.bbx_w);
            font.bbox_h = std::max(font.bbox_h, g.bbx_h);
        }
    }
    if (font.font_ascent == 0 && font.font_descent == 0) {
        // Fallback: derive from max ascent/descent across glyphs.
        for (const auto& g : font.glyphs) {
            font.font_ascent = std::max(font.font_ascent, g.bbx_y + g.bbx_h);
            font.font_descent = std::max(font.font_descent, -g.bbx_y);
        }
    }

    return font;
}

// ---------------------------------------------------------------------------
// Helper: extract a single pixel (MSBit first) from a glyph bitmap row
// ---------------------------------------------------------------------------

/// @param glyph   Parsed BDF glyph.
/// @param col     0-based pixel column within the glyph's BBX.
/// @param row     0-based pixel row within the glyph's BBX (top=0).
/// @returns true if the pixel is set.
inline bool sample(const Glyph& glyph, int32_t col, int32_t row) noexcept {
    if (col < 0 || row < 0 || col >= glyph.bbx_w || row >= glyph.bbx_h)
        return false;
    int32_t stride = (glyph.bbx_w + 7) / 8;
    size_t byte_idx = static_cast<size_t>(row * stride + col / 8);
    if (byte_idx >= glyph.bitmap.size())
        return false;
    uint8_t byte = glyph.bitmap[byte_idx];
    return (byte >> (7 - (col % 8))) & 1u;
}

} // namespace xebble::bdf
