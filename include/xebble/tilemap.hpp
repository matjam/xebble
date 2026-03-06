/// @file tilemap.hpp
/// @brief TileMap with arbitrary N-layer support.
///
/// TileMapData is the testable data layer storing tile indices in flat vectors.
/// TileMap wraps TileMapData with a SpriteSheet reference for rendering.
/// Layers are rendered back-to-front with alpha blending.
#pragma once

#include <xebble/types.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace xebble {

class SpriteSheet;

/// @brief Pure data layer for tilemap tile indices. Testable without GPU.
///
/// Tiles are stored as optional indices — std::nullopt means empty (transparent).
/// Each layer is a flat vector of size width * height.
class TileMapData {
public:
    /// @brief Create tilemap data with given dimensions and layer count.
    /// @param width Number of tile columns.
    /// @param height Number of tile rows.
    /// @param layer_count Number of layers.
    TileMapData(uint32_t width, uint32_t height, uint32_t layer_count);

    /// @brief Set a tile at the given position and layer.
    void set_tile(uint32_t layer, uint32_t x, uint32_t y, uint32_t tile_index);

    /// @brief Clear a tile (make it transparent).
    void clear_tile(uint32_t layer, uint32_t x, uint32_t y);

    /// @brief Get the tile index at the given position and layer.
    /// @return The tile index, or std::nullopt if empty.
    std::optional<uint32_t> tile_at(uint32_t layer, uint32_t x, uint32_t y) const;

    /// @brief Fill an entire layer with tile indices from a span.
    /// @param layer Layer index.
    /// @param tile_indices Span of width*height tile indices (0 = empty).
    void set_layer(uint32_t layer, std::span<const uint32_t> tile_indices);

    /// @brief Clear all tiles in a layer.
    void clear_layer(uint32_t layer);

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t layer_count() const { return layer_count_; }

private:
    uint32_t width_;
    uint32_t height_;
    uint32_t layer_count_;

    // Each layer: flat vector of optional tile indices.
    // We use a sentinel value (UINT32_MAX) for "empty" to avoid per-tile optional overhead.
    static constexpr uint32_t EMPTY_TILE = UINT32_MAX;
    std::vector<uint32_t> tiles_; // layer_count * width * height

    uint32_t index(uint32_t layer, uint32_t x, uint32_t y) const {
        return layer * (width_ * height_) + y * width_ + x;
    }
};

/// @brief A renderable tilemap referencing a SpriteSheet.
///
/// Wraps TileMapData and adds rendering-related state (offset, sheet reference).
class TileMap {
public:
    /// @brief Create a tilemap with the given dimensions.
    /// @param sheet The spritesheet to use for tile graphics.
    /// @param width Number of tile columns.
    /// @param height Number of tile rows.
    /// @param layer_count Number of layers.
    TileMap(const SpriteSheet& sheet, uint32_t width, uint32_t height, uint32_t layer_count);

    /// @brief Set a tile at the given position and layer.
    void set_tile(uint32_t layer, uint32_t x, uint32_t y, uint32_t tile_index);

    /// @brief Clear a tile (set to empty/transparent).
    void set_tile(uint32_t layer, uint32_t x, uint32_t y, std::nullopt_t);

    /// @brief Get the tile index at the given position and layer.
    std::optional<uint32_t> tile_at(uint32_t layer, uint32_t x, uint32_t y) const;

    /// @brief Fill an entire layer with tile indices.
    void set_layer(uint32_t layer, std::span<const uint32_t> tile_indices);

    /// @brief Clear all tiles in a layer.
    void clear_layer(uint32_t layer);

    /// @brief Set the scroll offset for the tilemap.
    void set_offset(Vec2 offset);

    Vec2 offset() const { return offset_; }
    uint32_t layer_count() const { return data_.layer_count(); }
    uint32_t width() const { return data_.width(); }
    uint32_t height() const { return data_.height(); }

    /// @brief Access the underlying tile data.
    const TileMapData& data() const { return data_; }

    /// @brief Access the referenced spritesheet.
    const SpriteSheet& sheet() const { return *sheet_; }

private:
    const SpriteSheet* sheet_;
    TileMapData data_;
    Vec2 offset_{0.0f, 0.0f};
};

} // namespace xebble
