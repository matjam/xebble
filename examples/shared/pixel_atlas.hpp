/// @file pixel_atlas.hpp
/// @brief Shared helper for all examples that need a procedural spritesheet.
///
/// Generates a 16-tile horizontal strip atlas in CPU memory (no external files)
/// and uploads it to the GPU as a SpriteSheet.
///
/// Palette (tile index → colour):
///   0  black (void)       8  orange
///   1  white (wall/fill)  9  dark grey (rock)
///   2  red               10  light grey (stone floor)
///   3  green             11  dark green (grass)
///   4  blue              12  brown (dirt)
///   5  yellow            13  dark blue (water)
///   6  cyan              14  light red (hero)
///   7  magenta           15  purple (goal/item)
///
/// Usage (in a System::init):
/// @code
///   sheet_ = pixel_atlas::create(world.resource<xebble::Renderer*>()->context());
/// @endcode
#pragma once

#include <xebble/texture.hpp>
#include <xebble/spritesheet.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace pixel_atlas {

constexpr uint32_t TILE_W    = 16;
constexpr uint32_t TILE_H    = 16;
constexpr uint32_t NUM_TILES = 16;
constexpr uint32_t ATLAS_W   = TILE_W * NUM_TILES;
constexpr uint32_t ATLAS_H   = TILE_H;

struct RGBA { uint8_t r, g, b, a; };
inline constexpr std::array<RGBA, NUM_TILES> PALETTE = {{
    {   0,   0,   0, 255 }, //  0 black  (void)
    { 255, 255, 255, 255 }, //  1 white  (wall/fill)
    { 210,  50,  50, 255 }, //  2 red
    {  50, 210,  50, 255 }, //  3 green
    {  50,  50, 210, 255 }, //  4 blue
    { 220, 220,  50, 255 }, //  5 yellow
    {  50, 210, 210, 255 }, //  6 cyan
    { 210,  50, 210, 255 }, //  7 magenta
    { 220, 130,  50, 255 }, //  8 orange
    {  60,  60,  60, 255 }, //  9 dark grey (rock/shadow)
    { 160, 160, 160, 255 }, // 10 light grey (stone floor)
    {  30, 100,  30, 255 }, // 11 dark green (grass)
    { 120,  80,  40, 255 }, // 12 brown (dirt/wood)
    {  20,  40, 120, 255 }, // 13 dark blue (water)
    { 255, 120, 120, 255 }, // 14 light red (hero)
    { 140,  60, 220, 255 }, // 15 purple (goal/item)
}};

enum : uint32_t {
    TILE_BLACK   =  0, TILE_WHITE  =  1,
    TILE_RED     =  2, TILE_GREEN  =  3,
    TILE_BLUE    =  4, TILE_YELLOW =  5,
    TILE_CYAN    =  6, TILE_MAGENTA=  7,
    TILE_ORANGE  =  8, TILE_ROCK   =  9,
    TILE_FLOOR   = 10, TILE_GRASS  = 11,
    TILE_DIRT    = 12, TILE_WATER  = 13,
    TILE_HERO    = 14, TILE_GOAL   = 15,
};

/// Build the raw RGBA pixel buffer.
inline std::vector<uint8_t> build_pixels() {
    std::vector<uint8_t> px(ATLAS_W * ATLAS_H * 4);
    for (uint32_t t = 0; t < NUM_TILES; ++t) {
        const RGBA& c = PALETTE[t];
        uint32_t x0 = t * TILE_W;
        for (uint32_t py = 0; py < TILE_H; ++py) {
            for (uint32_t px_off = 0; px_off < TILE_W; ++px_off) {
                uint32_t x   = x0 + px_off;
                uint32_t idx = (py * ATLAS_W + x) * 4;
                bool border  = (px_off == 0 || px_off == TILE_W - 1 ||
                                py    == 0 || py    == TILE_H - 1);
                uint8_t mul  = border ? 128 : 255;
                px[idx + 0]  = static_cast<uint8_t>((uint32_t)c.r * mul / 255);
                px[idx + 1]  = static_cast<uint8_t>((uint32_t)c.g * mul / 255);
                px[idx + 2]  = static_cast<uint8_t>((uint32_t)c.b * mul / 255);
                px[idx + 3]  = c.a;
            }
        }
    }
    return px;
}

/// Create the GPU spritesheet.  Returns nullopt on failure (won't happen in practice).
inline std::optional<xebble::SpriteSheet> create(xebble::vk::Context& ctx) {
    auto pixels = build_pixels();
    auto tex = xebble::Texture::create_from_pixels(ctx, pixels.data(), ATLAS_W, ATLAS_H);
    if (!tex) return std::nullopt;
    auto sheet = xebble::SpriteSheet::from_texture(std::move(*tex), TILE_W, TILE_H);
    if (!sheet) return std::nullopt;
    return std::move(*sheet);
}

} // namespace pixel_atlas
