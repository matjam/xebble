/// @file embedded_font.hpp
/// @brief Embedded 8x12 bitmap font for default UI rendering.
#pragma once

#include <xebble/types.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace xebble {

class BitmapFont;
class SpriteSheet;

namespace vk {
class Context;
}

namespace embedded_font {

constexpr uint32_t GLYPH_W = 8;
constexpr uint32_t GLYPH_H = 12;
constexpr uint32_t CHARS_PER_ROW = 16;
constexpr int FIRST_CHAR = 32;
constexpr int LAST_CHAR = 126;
constexpr int NUM_CHARS = LAST_CHAR - FIRST_CHAR + 1;
constexpr uint32_t ROWS = (NUM_CHARS + CHARS_PER_ROW - 1) / CHARS_PER_ROW;
constexpr uint32_t ATLAS_W = CHARS_PER_ROW * GLYPH_W;
constexpr uint32_t ATLAS_H = ROWS * GLYPH_H;

/// @brief Generate RGBA pixel data for the embedded font atlas.
std::vector<uint8_t> generate_pixels();

/// @brief Get the charset string (ASCII 32-126).
std::string charset();

/// @brief Create a BitmapFont from the embedded font data.
std::expected<std::unique_ptr<BitmapFont>, Error> create_font(vk::Context& ctx);

} // namespace embedded_font
} // namespace xebble
