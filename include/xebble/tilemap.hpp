/// @file tilemap.hpp
/// @brief TileMap — a multi-layer grid of tile indices for map rendering.
///
/// A tilemap is the standard way to draw large static or semi-static
/// environments in Xebble. It is split into two classes:
///
/// - `TileMapData` — pure data (no GPU dependency). Stores tile indices in
///   flat vectors. Use this for game logic, serialization, and unit tests.
///
/// - `TileMap` — extends `TileMapData` with a `SpriteSheet` reference and a
///   scroll offset. This is what gets attached to an entity and rendered by
///   `TileMapRenderSystem`.
///
/// Layers are rendered back-to-front with alpha blending, so layer 0 is the
/// bottom (e.g. floor tiles) and higher layers are drawn on top (e.g. objects,
/// overlay effects).
///
/// ## Quick-start example
///
/// @code
/// // Create a 40×25 tile map with 3 layers (floor, objects, effects).
/// auto tilemap = std::make_shared<TileMap>(sheet, 40, 25, 3);
///
/// // Fill the floor layer (layer 0) with a stone tile (index 5).
/// for (uint32_t y = 0; y < 25; ++y)
///     for (uint32_t x = 0; x < 40; ++x)
///         tilemap->set_tile(0, x, y, 5);
///
/// // Place a wall tile on the object layer (layer 1) at (3, 7).
/// tilemap->set_tile(1, 3, 7, 12);
///
/// // Attach it to an ECS entity for rendering.
/// auto e = world.build_entity()
///     .with(TileMapLayer{tilemap, /*z_order=*/0.0f})
///     .build();
/// @endcode
///
/// ## Bulk layer fill
///
/// @code
/// // Pre-built tile index array (e.g. loaded from a map file).
/// std::vector<uint32_t> floor_tiles = load_layer_from_file("level1_floor.dat");
/// tilemap->set_layer(0, floor_tiles);
/// @endcode
///
/// ## Scrolling
///
/// @code
/// // Scroll the map so that the camera is centred on the player.
/// // (TileMapRenderSystem automatically applies the Camera resource offset,
/// //  but you can also set a per-tilemap offset for parallax effects.)
/// tilemap->set_offset({camera.x, camera.y});
/// @endcode
#pragma once

#include <xebble/types.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace xebble {

class SpriteSheet;

// ---------------------------------------------------------------------------
// TileMapData
// ---------------------------------------------------------------------------

/// @brief Pure data layer for tilemap tile indices. Has no GPU dependency.
///
/// Tiles are stored as optional indices — `std::nullopt` (internally
/// `UINT32_MAX`) means the cell is transparent. Each layer is a flat
/// `width × height` array in row-major order.
///
/// Use `TileMapData` directly in unit tests, procedural generation, and
/// save/load routines where you don't want a GPU context dependency.
///
/// @code
/// // Pure-logic map representation used during dungeon generation.
/// TileMapData data(40, 25, 3);
///
/// // Carve a room on the floor layer.
/// for (uint32_t y = 5; y < 15; ++y)
///     for (uint32_t x = 5; x < 20; ++x)
///         data.set_tile(0, x, y, TILE_FLOOR);
///
/// // Read back a tile (returns nullopt if empty).
/// auto t = data.tile_at(0, 10, 10);
/// if (t) { /* tile index *t is set */ }
///
/// // Clear an individual cell.
/// data.clear_tile(1, 7, 3);
///
/// // Clear an entire layer (e.g. to reset the effect layer between turns).
/// data.clear_layer(2);
/// @endcode
class TileMapData {
public:
    /// @brief Construct empty tilemap data (all cells transparent).
    ///
    /// @param width        Number of tile columns.
    /// @param height       Number of tile rows.
    /// @param layer_count  Number of layers (e.g. 3 for floor/objects/effects).
    TileMapData(uint32_t width, uint32_t height, uint32_t layer_count);

    /// @brief Set a tile at (x, y) on the given layer to @p tile_index.
    ///
    /// @param layer       Layer index (0 = bottom).
    /// @param x           Column (0 = left).
    /// @param y           Row    (0 = top).
    /// @param tile_index  Spritesheet tile index to display.
    void set_tile(uint32_t layer, uint32_t x, uint32_t y, uint32_t tile_index);

    /// @brief Make the cell at (x, y) on the given layer transparent.
    void clear_tile(uint32_t layer, uint32_t x, uint32_t y);

    /// @brief Get the tile index at (x, y) on the given layer.
    ///
    /// @return The tile index, or `std::nullopt` if the cell is empty.
    ///
    /// @code
    /// if (auto tile = data.tile_at(0, player_x, player_y)) {
    ///     if (*tile == TILE_LAVA) apply_lava_damage(player);
    /// }
    /// @endcode
    [[nodiscard]] std::optional<uint32_t> tile_at(uint32_t layer, uint32_t x, uint32_t y) const;

    /// @brief Replace an entire layer with tiles from a flat span.
    ///
    /// The span must have exactly `width * height` elements. A value of
    /// `UINT32_MAX` is treated as empty (transparent).
    ///
    /// @code
    /// // Fast bulk upload from a loaded map file.
    /// std::vector<uint32_t> indices = parse_layer(map_file, layer_id);
    /// data.set_layer(0, indices);
    /// @endcode
    void set_layer(uint32_t layer, std::span<const uint32_t> tile_indices);

    /// @brief Set all cells in the given layer to transparent.
    ///
    /// Useful for clearing the effects layer at the start of each turn.
    void clear_layer(uint32_t layer);

    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }
    [[nodiscard]] uint32_t layer_count() const { return layer_count_; }

private:
    uint32_t width_;
    uint32_t height_;
    uint32_t layer_count_;

    // Sentinel value representing an empty (transparent) cell.
    static constexpr uint32_t EMPTY_TILE = UINT32_MAX;
    std::vector<uint32_t> tiles_; // layer_count * width * height

    uint32_t index(uint32_t layer, uint32_t x, uint32_t y) const {
        return layer * (width_ * height_) + y * width_ + x;
    }
};

// ---------------------------------------------------------------------------
// TileMap
// ---------------------------------------------------------------------------

/// @brief A renderable tilemap that adds a SpriteSheet reference and scroll offset
///        to `TileMapData`.
///
/// Wrap this in a `std::shared_ptr<TileMap>` and attach it to an entity via the
/// `TileMapLayer` component. `TileMapRenderSystem` will draw it automatically
/// each frame, offset by the `Camera` resource.
///
/// @code
/// // Construct with a reference to the spritesheet.
/// auto tilemap = std::make_shared<TileMap>(dungeon_sheet, 80, 50, 2);
///
/// // Populate layers.
/// tilemap->set_layer(0, floor_tile_indices);
/// tilemap->set_layer(1, wall_tile_indices);
///
/// // Attach to the world.
/// world.build_entity()
///     .with(TileMapLayer{tilemap, 0.0f})
///     .build();
///
/// // Later, to scroll (e.g. follow the player):
/// tilemap->set_offset({camera_x, camera_y});
///
/// // Or clear a dynamic layer every turn and rebuild it.
/// tilemap->clear_layer(1);
/// place_objects(tilemap, world);
/// @endcode
class TileMap {
public:
    /// @brief Construct a tilemap with the given spritesheet and dimensions.
    ///
    /// @param sheet        Spritesheet whose tiles are used to render each cell.
    ///                     Must outlive this TileMap.
    /// @param width        Number of tile columns.
    /// @param height       Number of tile rows.
    /// @param layer_count  Number of layers.
    TileMap(const SpriteSheet& sheet, uint32_t width, uint32_t height, uint32_t layer_count);

    /// @brief Set a tile at (x, y) on the given layer.
    void set_tile(uint32_t layer, uint32_t x, uint32_t y, uint32_t tile_index);

    /// @brief Clear a tile at (x, y) on the given layer (make it transparent).
    void set_tile(uint32_t layer, uint32_t x, uint32_t y, std::nullopt_t);

    /// @brief Get the tile index at (x, y) on the given layer.
    [[nodiscard]] std::optional<uint32_t> tile_at(uint32_t layer, uint32_t x, uint32_t y) const;

    /// @brief Replace an entire layer from a flat span of tile indices.
    void set_layer(uint32_t layer, std::span<const uint32_t> tile_indices);

    /// @brief Clear all tiles in the given layer.
    void clear_layer(uint32_t layer);

    /// @brief Set the per-tilemap scroll offset in virtual pixels.
    ///
    /// This offset is *added* to the Camera resource offset by
    /// `TileMapRenderSystem`. Use it for parallax scrolling — set background
    /// layers to scroll at a fraction of the camera speed.
    ///
    /// @code
    /// // Background layer scrolls at half the camera speed.
    /// bg_tilemap->set_offset({camera.x * 0.5f, camera.y * 0.5f});
    /// @endcode
    void set_offset(Vec2 offset);

    [[nodiscard]] Vec2 offset() const { return offset_; }
    [[nodiscard]] uint32_t layer_count() const { return data_.layer_count(); }
    [[nodiscard]] uint32_t width() const { return data_.width(); }
    [[nodiscard]] uint32_t height() const { return data_.height(); }

    /// @brief Access the underlying pure-data layer for game-logic queries.
    [[nodiscard]] const TileMapData& data() const { return data_; }

    /// @brief Access the spritesheet used for rendering.
    [[nodiscard]] const SpriteSheet& sheet() const { return *sheet_; }

private:
    const SpriteSheet* sheet_;
    TileMapData data_;
    Vec2 offset_{0.0f, 0.0f};
};

} // namespace xebble
