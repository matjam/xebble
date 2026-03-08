/// @file spritesheet.hpp
/// @brief SpriteSheet — a texture divided into a uniform grid of tiles.
///
/// A `SpriteSheet` wraps a `Texture` and subdivides it into equal-sized tiles
/// arranged in a regular grid. You query it for UV rectangles by tile index or
/// by (column, row) coordinates, then pass those UVs to `SpriteInstance` when
/// constructing draw calls.
///
/// In most projects sprites sheets are loaded via `AssetManager` from a TOML
/// manifest rather than directly. Direct construction is used in tests or when
/// creating sheets from procedurally generated textures.
///
/// ## TOML manifest entry
///
/// @code{.toml}
/// [spritesheets.tiles]
/// path        = "tilesets/dungeon.png"
/// tile_width  = 16
/// tile_height = 16
///
/// [spritesheets.characters]
/// path        = "sprites/characters.png"
/// tile_width  = 16
/// tile_height = 24
/// @endcode
///
/// ## Loading from the asset manager
///
/// @code
/// // In your system's init():
/// const SpriteSheet& tiles = assets.get<SpriteSheet>("tiles");
/// const SpriteSheet& chars = assets.get<SpriteSheet>("characters");
/// @endcode
///
/// ## Using UV regions to build draw calls
///
/// @code
/// // Tile index 42 on the dungeon sheet.
/// Rect uv = tiles.region(42);
///
/// // Or by column and row (col 3, row 2 on a 16-tile-wide sheet = index 35).
/// Rect uv = tiles.region(3, 2);
///
/// // Build a SpriteInstance manually.
/// SpriteInstance inst;
/// inst.pos_x  = 100.0f; inst.pos_y  = 80.0f;
/// inst.uv_x   = uv.x;   inst.uv_y   = uv.y;
/// inst.uv_w   = uv.w;   inst.uv_h   = uv.h;
/// inst.quad_w = (float)tiles.tile_width();
/// inst.quad_h = (float)tiles.tile_height();
/// inst.r = inst.g = inst.b = inst.a = 1.0f;
///
/// renderer.submit_instances({&inst, 1}, tiles.texture(), 0.0f);
/// @endcode
#pragma once

#include <xebble/texture.hpp>
#include <xebble/types.hpp>

#include <expected>
#include <filesystem>
#include <memory>

namespace xebble {

class Renderer;

namespace vk {
class Context;
}

/// @brief A texture subdivided into a uniform grid of equal-sized tiles.
///
/// Tiles are indexed in **row-major order**: index 0 is the top-left tile,
/// index 1 is one step right, and so on. The index wraps to the next row after
/// reaching the last column.
///
/// @code
/// // On a sheet with 8 columns:
/// //   index  0 = (col 0, row 0)   — top-left
/// //   index  7 = (col 7, row 0)   — top-right
/// //   index  8 = (col 0, row 1)   — start of second row
/// //   index 15 = (col 7, row 1)
/// @endcode
class SpriteSheet {
public:
    /// @brief Load a spritesheet from an image file on disk.
    ///
    /// @param ctx          Vulkan context from `Renderer::context()`.
    /// @param image_path   Path to the atlas image (PNG, BMP, etc.).
    /// @param tile_width   Width of each tile in pixels.
    /// @param tile_height  Height of each tile in pixels.
    ///
    /// @code
    /// auto sheet = SpriteSheet::load(renderer.context(),
    ///                                 "assets/dungeon_tiles.png", 16, 16);
    /// if (!sheet) {
    ///     log(LogLevel::Error, sheet.error().message);
    ///     return 1;
    /// }
    /// @endcode
    [[nodiscard]] static std::expected<SpriteSheet, Error>
    load(vk::Context& ctx, const std::filesystem::path& image_path, uint32_t tile_width,
         uint32_t tile_height);

    /// @brief Create a spritesheet from an already-loaded `Texture`.
    ///
    /// Use this when the texture was loaded from a ZIP archive or created
    /// procedurally and you want to wrap it in a SpriteSheet for tile lookups.
    ///
    /// @param texture      The atlas texture (taken by move).
    /// @param tile_width   Width of each tile in pixels.
    /// @param tile_height  Height of each tile in pixels.
    [[nodiscard]] static std::expected<SpriteSheet, Error>
    from_texture(Texture texture, uint32_t tile_width, uint32_t tile_height);

    ~SpriteSheet();
    SpriteSheet(SpriteSheet&&) noexcept;
    SpriteSheet& operator=(SpriteSheet&&) noexcept;
    SpriteSheet(const SpriteSheet&) = delete;
    SpriteSheet& operator=(const SpriteSheet&) = delete;

    /// @brief Get the normalised UV rect for a tile by its linear index.
    ///
    /// The returned `Rect` has (x, y) at the top-left UV corner and (w, h) as
    /// the UV extent. Pass these directly into `SpriteInstance::uv_x` etc.
    ///
    /// @param index  Row-major tile index (0 = top-left).
    ///
    /// @code
    /// // Draw tile 42 of the dungeon sheet at virtual position (64, 48).
    /// Rect uv = sheet.region(42);
    /// SpriteInstance inst{64, 48, uv.x, uv.y, uv.w, uv.h,
    ///                     (float)sheet.tile_width(), (float)sheet.tile_height(),
    ///                     1, 1, 1, 1};
    /// renderer.submit_instances({&inst, 1}, sheet.texture());
    /// @endcode
    [[nodiscard]] Rect region(uint32_t index) const;

    /// @brief Get the normalised UV rect for a tile by column and row.
    ///
    /// Equivalent to `region(row * columns() + col)`.
    ///
    /// @param col  Column index (0 = leftmost).
    /// @param row  Row index    (0 = topmost).
    ///
    /// @code
    /// // The stone floor tile lives at column 2, row 1.
    /// Rect uv = sheet.region(2, 1);
    /// @endcode
    [[nodiscard]] Rect region(uint32_t col, uint32_t row) const;

    /// @brief Calculate a UV region without needing a SpriteSheet instance.
    ///
    /// Useful in tests or tools where GPU resources are not available.
    ///
    /// @param sheet_width   Total atlas width in pixels.
    /// @param sheet_height  Total atlas height in pixels.
    /// @param tile_width    Tile width in pixels.
    /// @param tile_height   Tile height in pixels.
    /// @param index         Linear tile index.
    [[nodiscard]] static Rect calculate_region(uint32_t sheet_width, uint32_t sheet_height,
                                               uint32_t tile_width, uint32_t tile_height,
                                               uint32_t index);

    /// @brief Calculate a UV region by (col, row) without a SpriteSheet instance.
    [[nodiscard]] static Rect calculate_region(uint32_t sheet_width, uint32_t sheet_height,
                                               uint32_t tile_width, uint32_t tile_height,
                                               uint32_t col, uint32_t row);

    [[nodiscard]] uint32_t columns() const; ///< Number of tile columns (sheet_width / tile_width).
    [[nodiscard]] uint32_t rows() const; ///< Number of tile rows    (sheet_height / tile_height).
    [[nodiscard]] uint32_t tile_width() const;  ///< Width of each tile in pixels.
    [[nodiscard]] uint32_t tile_height() const; ///< Height of each tile in pixels.

    /// @brief Access the underlying GPU texture.
    ///
    /// Pass `sheet.texture()` to `Renderer::submit_instances()`.
    [[nodiscard]] const Texture& texture() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SpriteSheet() = default;
};

} // namespace xebble
