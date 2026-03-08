/// @file font.hpp
/// @brief Text rendering via bitmap fonts and TrueType (FreeType) fonts.
///
/// Xebble supports two font backends:
///
/// ### BitmapFont
///
/// A fixed-size grid of glyph tiles stored in a spritesheet — the classic
/// roguelike CP437 or ASCII terminal font. All glyphs are the same pixel
/// dimensions. Defined in the TOML manifest under `[bitmap_fonts]`.
///
/// @code{.toml}
/// [bitmap_fonts.terminal]
/// path         = "fonts/cp437_16x16.png"
/// glyph_width  = 16
/// glyph_height = 16
/// charset      = "
/// !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
/// @endcode
///
/// ### Font (TrueType)
///
/// A `.ttf` file rasterized via FreeType into a texture atlas at a given pixel
/// size. Supports variable glyph widths and proper typographic metrics.
/// Defined in the manifest under `[fonts]`.
///
/// @code{.toml}
/// [fonts.ui]
/// path       = "fonts/NotoSans-Regular.ttf"
/// pixel_size = 14
/// @endcode
///
/// ## Rendering text
///
/// In both cases, construct a `TextBlock` and pass it to
/// `Renderer::submit_instances()` via the built-in rendering systems, or use
/// the UI system's `PanelBuilder::text()` helper.
///
/// @code
/// // Using a bitmap font.
/// TextBlock msg;
/// msg.text     = "The goblin hits you for 3 damage!";
/// msg.position = {10.0f, 340.0f};
/// msg.color    = {255, 80, 80, 255};
/// msg.font     = &assets.get<BitmapFont>("terminal");
///
/// // Using a TrueType font.
/// TextBlock label;
/// label.text     = "Level 3 — Crystal Caverns";
/// label.position = {320.0f, 10.0f};
/// label.color    = {220, 220, 255, 255};
/// label.font     = &assets.get<Font>("ui");
/// @endcode
#pragma once

#include <xebble/texture.hpp>
#include <xebble/types.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace xebble {

class SpriteSheet;

namespace vk {
class Context;
}

// ---------------------------------------------------------------------------
// BitmapFontFormat
// ---------------------------------------------------------------------------

/// @brief Selects the file format used by `BitmapFont::load()`.
///
/// `Auto` detects the format from the file extension:
///   - `.pcf` or `.pcf.gz` → PCF
///   - `.bdf`              → BDF
///   - anything else       → PNG spritesheet
enum class BitmapFontFormat {
    Auto, ///< Detect from file extension.
    PNG,  ///< PNG spritesheet with uniform glyph grid.
    PCF,  ///< X11 Portable Compiled Format bitmap font.
    BDF,  ///< Adobe Bitmap Distribution Format (plain-text).
};

// ---------------------------------------------------------------------------
// BitmapFontData
// ---------------------------------------------------------------------------

/// @brief Pure data layer for bitmap font glyph lookup. No GPU dependency.
///
/// Maps Unicode codepoints to spritesheet tile indices. Supports both the
/// classic charset-string construction (PNG bitmap fonts) and direct
/// codepoint→tile-index map construction (PCF fonts).
///
/// @code
/// // PNG bitmap font: ordered charset, tile 0 = first char, etc.
/// BitmapFontData data(8, 12, " !\"#$%&...");
/// auto idx = data.glyph_index('A');
///
/// // PCF font: codepoint map built by the loader.
/// BitmapFontData data(8, 12);
/// data.add_glyph(0x41, 0);  // 'A' → tile 0
/// @endcode
class BitmapFontData {
public:
    /// @brief Construct from an ordered charset string (PNG fonts).
    ///
    /// The character at position N in the charset maps to tile index N.
    /// The charset must be a valid UTF-8 string; each decoded codepoint
    /// maps to one tile.
    BitmapFontData(uint32_t glyph_width, uint32_t glyph_height, std::u8string_view charset);

    /// @brief Construct an empty font data (for PCF / manual population).
    BitmapFontData(uint32_t glyph_width, uint32_t glyph_height);

    /// @brief Register a codepoint → tile index mapping (used by PCF loader).
    void add_glyph(uint32_t codepoint, uint32_t tile_index);

    /// @brief Return the tile index for a char (treats as Latin-1 codepoint).
    std::optional<uint32_t> glyph_index(char ch) const;

    /// @brief Return the tile index for a Unicode codepoint.
    std::optional<uint32_t> glyph_index(uint32_t codepoint) const;

    uint32_t glyph_width() const { return glyph_width_; }
    uint32_t glyph_height() const { return glyph_height_; }

    /// @brief The ordered charset string (empty for PCF fonts).
    const std::string& charset() const { return charset_; }

private:
    uint32_t glyph_width_;
    uint32_t glyph_height_;
    std::string charset_;                                  ///< Non-empty for PNG fonts.
    std::unordered_map<uint32_t, uint32_t> codepoint_map_; ///< codepoint → tile index.
};

// ---------------------------------------------------------------------------
// BitmapFont
// ---------------------------------------------------------------------------

/// @brief A bitmap (tile-based) font backed by a spritesheet of glyph tiles.
///
/// Each glyph is a fixed-size tile in the atlas. This is the standard font
/// type for roguelikes using an ASCII or CP437 tileset.
///
/// Loaded via the `AssetManager` or directly with `BitmapFont::load()`.
///
/// @code
/// // Usage in a rendering system:
/// const BitmapFont& font = assets.get<BitmapFont>("terminal");
///
/// // Iterate the string and build SpriteInstances.
/// float x = start_x;
/// for (char ch : text) {
///     if (auto idx = font.glyph_index(ch)) {
///         Rect uv = font.sheet().region(*idx);
///         SpriteInstance inst{x, y, uv.x, uv.y, uv.w, uv.h,
///                             (float)font.glyph_width(),
///                             (float)font.glyph_height(),
///                             r, g, b, a};
///         instances.push_back(inst);
///     }
///     x += font.glyph_width();
/// }
/// renderer.submit_instances(instances, font.sheet().texture(), z);
/// @endcode
class BitmapFont {
public:
    /// @brief Load a bitmap font from a file.
    ///
    /// The format is selected by `fmt`:
    /// - `BitmapFontFormat::Auto` — detected from the file extension
    ///   (`.pcf` / `.pcf.gz` → PCF; otherwise → PNG).
    /// - `BitmapFontFormat::PNG` — uniform glyph-grid spritesheet image.
    ///   Requires `glyph_width`, `glyph_height`, and `charset`.
    /// - `BitmapFontFormat::PCF` — X11 Portable Compiled Format.
    ///   `glyph_width`, `glyph_height`, and `charset` are ignored; all
    ///   metrics are read from the file. The cell size is set to the font's
    ///   maximum glyph bounding box.
    /// - `BitmapFontFormat::BDF` — Adobe Bitmap Distribution Format (plain-text).
    ///   `glyph_width`, `glyph_height`, and `charset` are ignored; metrics
    ///   are parsed directly from the `.bdf` text file. The cell size is set
    ///   to the font's FONTBOUNDINGBOX dimensions.
    ///
    /// @param ctx           Vulkan context.
    /// @param path          Path to the font file.
    /// @param fmt           File format (default: Auto).
    /// @param glyph_width   PNG only: width of each glyph cell in pixels.
    /// @param glyph_height  PNG only: height of each glyph cell in pixels.
    /// @param charset       PNG only: ordered charset as a UTF-8 string.
    static std::expected<BitmapFont, Error>
    load(vk::Context& ctx, const std::filesystem::path& path,
         BitmapFontFormat fmt = BitmapFontFormat::Auto, uint32_t glyph_width = 0,
         uint32_t glyph_height = 0, std::u8string_view charset = {});

    /// @brief Create a bitmap font from an existing spritesheet (takes ownership).
    ///
    /// @param sheet    The glyph atlas (moved in).
    /// @param charset  Ordered charset as a UTF-8 string; each decoded codepoint
    ///                 maps to one tile.
    static std::expected<BitmapFont, Error> from_spritesheet(SpriteSheet sheet,
                                                             std::u8string_view charset);

    /// @brief Create a bitmap font from a spritesheet and a pre-built data layer.
    ///
    /// Used by generated embedded font sources to avoid re-building the
    /// codepoint map at runtime.
    ///
    /// @param sheet  The glyph atlas (moved in).
    /// @param data   Pre-populated BitmapFontData (moved in).
    static std::expected<BitmapFont, Error> from_data(SpriteSheet sheet, BitmapFontData data);

    ~BitmapFont();
    BitmapFont(BitmapFont&&) noexcept;
    BitmapFont& operator=(BitmapFont&&) noexcept;
    BitmapFont(const BitmapFont&) = delete;
    BitmapFont& operator=(const BitmapFont&) = delete;

    /// @brief Return the spritesheet tile index for a char (Latin-1 codepoint).
    std::optional<uint32_t> glyph_index(char ch) const;

    /// @brief Return the spritesheet tile index for a Unicode codepoint.
    std::optional<uint32_t> glyph_index(uint32_t codepoint) const;

    /// @brief Access the underlying glyph atlas spritesheet.
    const SpriteSheet& sheet() const;

    /// @brief Access the pure-data font layer.
    const BitmapFontData& data() const;

    uint32_t glyph_width() const;  ///< Width of each glyph in pixels.
    uint32_t glyph_height() const; ///< Height of each glyph in pixels.

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BitmapFont() = default;

    static std::expected<BitmapFont, Error> load_pcf(vk::Context& ctx,
                                                     const std::filesystem::path& path);

    static std::expected<BitmapFont, Error> load_bdf(vk::Context& ctx,
                                                     const std::filesystem::path& path);
};

// ---------------------------------------------------------------------------
// GlyphMetrics
// ---------------------------------------------------------------------------

/// @brief Layout metrics for a single TrueType glyph.
///
/// These values are in virtual pixels and follow typographic conventions:
/// `bearing_x` is how far right to shift before drawing, `bearing_y` is how
/// far below the current baseline origin the glyph top sits (positive = above
/// baseline), and `advance` is how far right to move the pen after drawing.
///
/// @code
/// // Manual TrueType text layout.
/// float pen_x = start_x;
/// float baseline_y = start_y;
/// for (char ch : text) {
///     auto m = font.glyph(ch);
///     if (!m) { pen_x += font.pixel_size() * 0.5f; continue; }
///     float draw_x = pen_x + m->bearing_x;
///     float draw_y = baseline_y - m->bearing_y;
///     draw_glyph(draw_x, draw_y, m->uv, m->width, m->height);
///     pen_x += m->advance;
/// }
/// @endcode
struct GlyphMetrics {
    float advance = 0.0f;   ///< Horizontal pen advance in pixels.
    float bearing_x = 0.0f; ///< Offset from pen to left edge of glyph bitmap.
    float bearing_y = 0.0f; ///< Offset from baseline to top of glyph bitmap.
    float width = 0.0f;     ///< Rendered glyph bitmap width in pixels.
    float height = 0.0f;    ///< Rendered glyph bitmap height in pixels.
    Rect uv;                ///< Normalised UV rect in the font atlas texture.
};

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

/// @brief A TrueType font rasterized via FreeType into a GPU texture atlas.
///
/// Glyphs are rasterized at construction time at a fixed `pixel_size`. The
/// font owns its atlas texture and provides per-glyph metrics for layout.
/// Variable glyph widths, proper kerning baselines, and the full ASCII
/// printable range (0x20–0x7E) are supported.
///
/// Loaded via the `AssetManager` or directly with `Font::load()`.
///
/// @code
/// // Load at 16-pixel size.
/// auto font = Font::load(renderer.context(), "fonts/NotoSans.ttf", 16);
/// if (!font) { log(LogLevel::Error, font.error().message); return 1; }
///
/// // Measure the width of a string before drawing it.
/// float width = 0.0f;
/// for (char ch : text) {
///     if (auto m = font->glyph(ch)) width += m->advance;
/// }
///
/// // Centre the text horizontally.
/// float x = (virtual_width - width) * 0.5f;
/// @endcode
class Font {
public:
    /// @brief Load a TTF font and rasterize glyphs into an atlas texture.
    ///
    /// @param ctx         Vulkan context.
    /// @param font_path   Path to a `.ttf` font file.
    /// @param pixel_size  Rasterization size in pixels (e.g. 12, 14, 16, 24).
    static std::expected<Font, Error> load(vk::Context& ctx, const std::filesystem::path& font_path,
                                           uint32_t pixel_size);

    /// @brief Load a PCF bitmap font as a proportional `Font`.
    ///
    /// Reads per-glyph metrics (advance, bearings, ascent/descent) directly
    /// from the PCF METRICS table and packs glyph bitmaps into a texture atlas
    /// using the same row-packing as the TrueType path. The result is a `Font`
    /// with correct proportional layout — variable advances, proper baselines.
    ///
    /// Unlike `BitmapFont::load(..., BitmapFontFormat::PCF)` which snaps all
    /// glyphs to a uniform cell, this preserves each glyph's true ink bounds.
    ///
    /// @param ctx         Vulkan context.
    /// @param font_path   Path to a `.pcf` file.
    static std::expected<Font, Error> load_pcf(vk::Context& ctx,
                                               const std::filesystem::path& font_path);

    /// @brief Construct a `Font` from a pre-built atlas texture and glyph map.
    ///
    /// Used by generated embedded font sources that have already rasterized and
    /// packed glyphs offline.
    ///
    /// @param atlas        GPU texture atlas (moved in).
    /// @param glyphs       Codepoint → GlyphMetrics map (moved in).
    /// @param pixel_size   Nominal rasterization size in pixels.
    /// @param line_height  Recommended line height in pixels (ascender + |descender|).
    /// @param ascender     Distance from baseline to top of cell in pixels.
    ///                     Defaults to `line_height` if omitted (safe for fonts
    ///                     without descenders).
    static std::expected<Font, Error> from_atlas(Texture atlas,
                                                 std::unordered_map<uint32_t, GlyphMetrics> glyphs,
                                                 uint32_t pixel_size, float line_height,
                                                 float ascender = 0.0f);

    /// @brief Load a BDF bitmap font as a proportional `Font`.
    ///
    /// Parses the plain-text Adobe BDF file directly. Per-glyph metrics
    /// (advance, bearings, ascent/descent) are read from DWIDTH and BBX
    /// records. Glyph bitmaps are row-packed into a texture atlas identical to
    /// the PCF and TrueType paths, preserving each glyph's true ink bounds.
    ///
    /// @param ctx         Vulkan context.
    /// @param font_path   Path to a `.bdf` file.
    static std::expected<Font, Error> load_bdf(vk::Context& ctx,
                                               const std::filesystem::path& font_path);

    ~Font();
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    /// @brief Get layout metrics for a character (Latin-1 codepoint).
    ///
    /// @return Glyph metrics, or `std::nullopt` if not present.
    std::optional<GlyphMetrics> glyph(char ch) const;

    /// @brief Get layout metrics for a Unicode codepoint.
    ///
    /// @return Glyph metrics, or `std::nullopt` if not present.
    std::optional<GlyphMetrics> glyph(uint32_t codepoint) const;

    /// @brief The GPU texture atlas holding all rasterized glyphs.
    const Texture& texture() const;

    /// @brief The pixel size this font was rasterized at.
    uint32_t pixel_size() const;

    /// @brief Recommended line height in pixels (advance between baselines).
    float line_height() const;

    /// @brief Distance from the top of the cell to the baseline, in pixels.
    ///
    /// Equal to the font's ascender metric.  Use this as the vertical offset
    /// when placing glyphs: `glyph_y = cell_y + ascender() - bearing_y`.
    float ascender() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Font() = default;
};

// ---------------------------------------------------------------------------
// TextBlock
// ---------------------------------------------------------------------------

/// @brief A fully-specified block of text ready for the rendering system.
///
/// `TextBlock` bundles all the information needed to render a string: the text
/// itself, its position in virtual pixel space, draw order, colour, and which
/// font to use. It accepts either a `BitmapFont*` or a `Font*` so both font
/// types can be used uniformly.
///
/// Pass `TextBlock` objects to the built-in rendering system or queue them on
/// an entity for `SpriteRenderSystem` to process.
///
/// @code
/// // Damage number that floats upward.
/// TextBlock dmg;
/// dmg.text     = "-" + std::to_string(damage);
/// dmg.position = {entity_x, entity_y - 8.0f};
/// dmg.z_order  = 50.0f;
/// dmg.color    = {255, 60, 60, 255};
/// dmg.font     = &assets.get<BitmapFont>("terminal");
///
/// // HUD element: player HP in the top-left corner.
/// TextBlock hp;
/// hp.text     = "HP: " + std::to_string(player.hp) + "/" + std::to_string(player.max_hp);
/// hp.position = {8.0f, 8.0f};
/// hp.z_order  = 90.0f;   // drawn on top of the map
/// hp.color    = {100, 255, 100, 255};
/// hp.font     = &assets.get<Font>("ui");
/// @endcode
struct TextBlock {
    std::u8string text;                 ///< The text to render, encoded as UTF-8.
    Vec2 position;                      ///< Top-left corner in virtual pixel coordinates.
    float z_order = 0.0f;               ///< Draw order (lower = behind).
    Color color = {255, 255, 255, 255}; ///< Tint applied to each glyph.

    /// @brief The font to render with. Assign a `const BitmapFont*` or `const Font*`.
    std::variant<const BitmapFont*, const Font*> font;
};

} // namespace xebble
