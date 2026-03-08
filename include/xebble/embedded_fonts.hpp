/// @file embedded_fonts.hpp
/// @brief Pre-rasterized embedded fonts generated from the PetMe and Berkelium TTFs.
///
/// These fonts are compiled directly into the library — no font files needed
/// at runtime. Re-generate the sources with:
///
///     python3 scripts/gen_embedded_fonts.py
///
/// ## Available fonts
///
/// | Symbol       | Type        | Cell     | Description                         |
/// |--------------|-------------|----------|-------------------------------------|
/// | `petme64`    | BitmapFont  | 8×8 px   | PetMe64 — C64 style, 3800+ glyphs   |
/// | `petme642y`  | BitmapFont  | 8×16 px  | PetMe642Y — C64 double-height       |
/// | `berkelium64`| Font        | ~8px high| Berkelium64 — proportional pixel    |
///
/// ## Usage — low-level (create and manage yourself)
///
/// @code
/// #include <xebble/embedded_fonts.hpp>
///
/// auto font = xebble::embedded_fonts::petme64::create(ctx);
/// auto font = xebble::embedded_fonts::petme642y::create(ctx);
/// auto font = xebble::embedded_fonts::berkelium64::create(ctx);
/// @endcode
///
/// ## Usage — high-level (load into World and set as UI font)
///
/// Call one of the `use_*` helpers from your system's `init()`. The font is
/// stored as a World resource (lifetime managed by the World) and wired into
/// the `UITheme` resource automatically:
///
/// @code
/// void init(xebble::World& world) override {
///     xebble::embedded_fonts::use_berkelium64(world);  // proportional
///     // or:
///     xebble::embedded_fonts::use_petme64(world);      // 8x8 fixed-cell
///     xebble::embedded_fonts::use_petme642y(world);    // 8x16 fixed-cell
/// }
/// @endcode
#pragma once

#include <xebble/font.hpp>
#include <xebble/types.hpp>

#include <cstdint>
#include <expected>

namespace xebble {
namespace vk {
class Context;
}
class World;
} // namespace xebble

namespace xebble::embedded_fonts {

// ---------------------------------------------------------------------------
// petme64  —  BitmapFont, 8×8 fixed cell
// ---------------------------------------------------------------------------
namespace petme64 {
/// @brief Create the PetMe64 embedded bitmap font (8×8 cells).
///
/// Covers 3800+ codepoints including full Latin, box-drawing (U+2500),
/// block elements, C64 Private Use Area graphics, and legacy symbols.
[[nodiscard]] std::expected<BitmapFont, Error> create(vk::Context& ctx);
} // namespace petme64

// ---------------------------------------------------------------------------
// petme642y  —  BitmapFont, 8×16 fixed cell
// ---------------------------------------------------------------------------
namespace petme642y {
/// @brief Create the PetMe642Y embedded bitmap font (8×16 cells).
///
/// Double-height version of PetMe64. Same codepoint coverage, taller glyphs.
[[nodiscard]] std::expected<BitmapFont, Error> create(vk::Context& ctx);
} // namespace petme642y

// ---------------------------------------------------------------------------
// berkelium64  —  Font (proportional), ~8px high
// ---------------------------------------------------------------------------
namespace berkelium64 {
/// @brief Create the Berkelium64 embedded proportional font (~8px high).
///
/// Variable-width pixel font with 288 codepoints covering Latin and
/// common symbols. Use as a `Font` with per-glyph advance/bearing metrics.
[[nodiscard]] std::expected<Font, Error> create(vk::Context& ctx);
} // namespace berkelium64

// ---------------------------------------------------------------------------
// World helpers — load a font and wire it into UITheme in one call
// ---------------------------------------------------------------------------

/// @brief Load Berkelium64 into the World and set it as the UI font.
///
/// Stores the font as a World resource (so its lifetime matches the World)
/// and sets it on the `UITheme` resource, creating one if absent.
/// Call from your system's `init()`.
void use_berkelium64(World& world);

/// @brief Load PetMe64 into the World and set it as the UI font.
void use_petme64(World& world);

/// @brief Load PetMe642Y into the World and set it as the UI font.
void use_petme642y(World& world);

} // namespace xebble::embedded_fonts
