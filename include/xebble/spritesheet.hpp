/// @file spritesheet.hpp
/// @brief SpriteSheet for tile-based sprite atlases.
///
/// A SpriteSheet wraps a Texture and divides it into a uniform grid of tiles.
/// Regions can be queried by linear index or (col, row) coordinates, returning
/// normalized UV coordinates suitable for use with SpriteInstance.
#pragma once

#include <xebble/types.hpp>
#include <xebble/texture.hpp>
#include <expected>
#include <filesystem>
#include <memory>

namespace xebble {

class Renderer;

namespace vk {
class Context;
}

/// @brief A texture divided into a uniform grid of tiles.
///
/// Provides UV region lookup by linear index or (col, row).
/// Use `calculate_region()` for pure-math region calculation without GPU.
class SpriteSheet {
public:
    /// @brief Load a spritesheet from an image file.
    /// @param ctx Vulkan context for texture creation.
    /// @param image_path Path to the image file.
    /// @param tile_width Width of each tile in pixels.
    /// @param tile_height Height of each tile in pixels.
    static std::expected<SpriteSheet, Error> load(
        vk::Context& ctx,
        const std::filesystem::path& image_path,
        uint32_t tile_width, uint32_t tile_height);

    /// @brief Create a spritesheet from an existing texture.
    /// @param texture The texture to use as the atlas.
    /// @param tile_width Width of each tile in pixels.
    /// @param tile_height Height of each tile in pixels.
    static std::expected<SpriteSheet, Error> from_texture(
        Texture texture,
        uint32_t tile_width, uint32_t tile_height);

    ~SpriteSheet();
    SpriteSheet(SpriteSheet&&) noexcept;
    SpriteSheet& operator=(SpriteSheet&&) noexcept;
    SpriteSheet(const SpriteSheet&) = delete;
    SpriteSheet& operator=(const SpriteSheet&) = delete;

    /// @brief Get the UV region for a tile by linear index (row-major order).
    /// @param index Linear tile index (0 = top-left, columns-1 = top-right).
    Rect region(uint32_t index) const;

    /// @brief Get the UV region for a tile by column and row.
    /// @param col Column index (0 = leftmost).
    /// @param row Row index (0 = topmost).
    Rect region(uint32_t col, uint32_t row) const;

    /// @brief Calculate a UV region without needing a SpriteSheet instance.
    /// @param sheet_width Total sheet width in pixels.
    /// @param sheet_height Total sheet height in pixels.
    /// @param tile_width Tile width in pixels.
    /// @param tile_height Tile height in pixels.
    /// @param index Linear tile index.
    static Rect calculate_region(uint32_t sheet_width, uint32_t sheet_height,
                                  uint32_t tile_width, uint32_t tile_height,
                                  uint32_t index);

    /// @brief Calculate a UV region by column and row without a SpriteSheet instance.
    static Rect calculate_region(uint32_t sheet_width, uint32_t sheet_height,
                                  uint32_t tile_width, uint32_t tile_height,
                                  uint32_t col, uint32_t row);

    /// @brief Number of tile columns in the sheet.
    uint32_t columns() const;

    /// @brief Number of tile rows in the sheet.
    uint32_t rows() const;

    /// @brief Width of each tile in pixels.
    uint32_t tile_width() const;

    /// @brief Height of each tile in pixels.
    uint32_t tile_height() const;

    /// @brief Access the underlying texture.
    const Texture& texture() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SpriteSheet() = default;
};

} // namespace xebble
