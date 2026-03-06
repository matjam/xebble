/// @file font.hpp
/// @brief Bitmap font, FreeType font, and TextBlock for text rendering.
///
/// BitmapFont uses a spritesheet where each tile is a glyph. The charset
/// string maps characters to tile indices.
///
/// Font loads a TTF via FreeType and rasterizes glyphs into a texture atlas.
///
/// TextBlock is a renderable text primitive used with Renderer::submit_instances().
#pragma once

#include <xebble/types.hpp>
#include <xebble/texture.hpp>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace xebble {

class SpriteSheet;

namespace vk {
class Context;
}

/// @brief Pure data layer for bitmap font glyph lookup. Testable without GPU.
class BitmapFontData {
public:
    /// @brief Create bitmap font data with glyph dimensions and charset.
    /// @param glyph_width Width of each glyph in pixels.
    /// @param glyph_height Height of each glyph in pixels.
    /// @param charset String mapping character position to tile index.
    BitmapFontData(uint32_t glyph_width, uint32_t glyph_height, std::string_view charset);

    /// @brief Look up the tile index for a character.
    /// @return The tile index, or std::nullopt if the character is not in the charset.
    std::optional<uint32_t> glyph_index(char ch) const;

    uint32_t glyph_width() const { return glyph_width_; }
    uint32_t glyph_height() const { return glyph_height_; }
    const std::string& charset() const { return charset_; }

private:
    uint32_t glyph_width_;
    uint32_t glyph_height_;
    std::string charset_;
};

/// @brief A bitmap font backed by a spritesheet of glyph tiles.
class BitmapFont {
public:
    /// @brief Load a bitmap font from an image file.
    /// @param ctx Vulkan context for texture creation.
    /// @param image_path Path to the glyph atlas image.
    /// @param glyph_width Width of each glyph in pixels.
    /// @param glyph_height Height of each glyph in pixels.
    /// @param charset String mapping character position to tile index.
    static std::expected<BitmapFont, Error> load(
        vk::Context& ctx,
        const std::filesystem::path& image_path,
        uint32_t glyph_width, uint32_t glyph_height,
        std::string_view charset);

    /// @brief Create a bitmap font from an existing spritesheet.
    /// @param sheet The glyph atlas spritesheet (takes ownership).
    /// @param charset String mapping character position to tile index.
    static std::expected<BitmapFont, Error> from_spritesheet(
        SpriteSheet sheet, std::string_view charset);

    ~BitmapFont();
    BitmapFont(BitmapFont&&) noexcept;
    BitmapFont& operator=(BitmapFont&&) noexcept;
    BitmapFont(const BitmapFont&) = delete;
    BitmapFont& operator=(const BitmapFont&) = delete;

    /// @brief Look up the tile index for a character.
    std::optional<uint32_t> glyph_index(char ch) const;

    /// @brief Access the underlying spritesheet.
    const SpriteSheet& sheet() const;

    /// @brief Access the font data.
    const BitmapFontData& data() const;

    uint32_t glyph_width() const;
    uint32_t glyph_height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    BitmapFont() = default;
};

/// @brief Glyph metrics for a FreeType-rendered glyph.
struct GlyphMetrics {
    float advance = 0.0f;      ///< Horizontal advance in pixels.
    float bearing_x = 0.0f;    ///< Horizontal bearing (left edge offset).
    float bearing_y = 0.0f;    ///< Vertical bearing (top edge offset from baseline).
    float width = 0.0f;        ///< Glyph bitmap width in pixels.
    float height = 0.0f;       ///< Glyph bitmap height in pixels.
    Rect uv;                   ///< UV region in the atlas texture.
};

/// @brief A TrueType font rendered via FreeType into a texture atlas.
class Font {
public:
    /// @brief Load a TTF font and rasterize glyphs into an atlas.
    /// @param ctx Vulkan context for texture creation.
    /// @param font_path Path to the .ttf font file.
    /// @param pixel_size Pixel size to rasterize at.
    static std::expected<Font, Error> load(
        vk::Context& ctx,
        const std::filesystem::path& font_path,
        uint32_t pixel_size);

    ~Font();
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    /// @brief Get metrics for a character.
    /// @return Glyph metrics, or std::nullopt if the character wasn't rasterized.
    std::optional<GlyphMetrics> glyph(char ch) const;

    /// @brief Access the atlas texture.
    const Texture& texture() const;

    /// @brief The pixel size this font was rasterized at.
    uint32_t pixel_size() const;

    /// @brief Line height in pixels.
    float line_height() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Font() = default;
};

/// @brief A renderable block of text.
struct TextBlock {
    std::string text;                                       ///< The text to render.
    Vec2 position;                                          ///< Position in virtual pixels.
    float z_order = 0.0f;                                   ///< Draw order (lower = behind).
    Color color = {255, 255, 255, 255};                     ///< Text color.
    std::variant<const BitmapFont*, const Font*> font;      ///< Font to render with.
};

} // namespace xebble
