/// @file tilemap.cpp
/// @brief TileMap and TileMapData implementation.
#include <xebble/tilemap.hpp>
#include <algorithm>

namespace xebble {

// --- TileMapData ---

TileMapData::TileMapData(uint32_t width, uint32_t height, uint32_t layer_count)
    : width_(width), height_(height), layer_count_(layer_count)
    , tiles_(static_cast<size_t>(layer_count) * width * height, EMPTY_TILE)
{
}

void TileMapData::set_tile(uint32_t layer, uint32_t x, uint32_t y, uint32_t tile_index) {
    tiles_[index(layer, x, y)] = tile_index;
}

void TileMapData::clear_tile(uint32_t layer, uint32_t x, uint32_t y) {
    tiles_[index(layer, x, y)] = EMPTY_TILE;
}

std::optional<uint32_t> TileMapData::tile_at(uint32_t layer, uint32_t x, uint32_t y) const {
    uint32_t val = tiles_[index(layer, x, y)];
    if (val == EMPTY_TILE) return std::nullopt;
    return val;
}

void TileMapData::set_layer(uint32_t layer, std::span<const uint32_t> tile_indices) {
    size_t layer_size = static_cast<size_t>(width_) * height_;
    size_t offset = static_cast<size_t>(layer) * layer_size;
    size_t count = std::min(tile_indices.size(), layer_size);
    std::copy_n(tile_indices.begin(), count, tiles_.begin() + offset);
}

void TileMapData::clear_layer(uint32_t layer) {
    size_t layer_size = static_cast<size_t>(width_) * height_;
    size_t offset = static_cast<size_t>(layer) * layer_size;
    std::fill_n(tiles_.begin() + offset, layer_size, EMPTY_TILE);
}

// --- TileMap ---

TileMap::TileMap(const SpriteSheet& sheet, uint32_t width, uint32_t height, uint32_t layer_count)
    : sheet_(&sheet), data_(width, height, layer_count)
{
}

void TileMap::set_tile(uint32_t layer, uint32_t x, uint32_t y, uint32_t tile_index) {
    data_.set_tile(layer, x, y, tile_index);
}

void TileMap::set_tile(uint32_t layer, uint32_t x, uint32_t y, std::nullopt_t) {
    data_.clear_tile(layer, x, y);
}

std::optional<uint32_t> TileMap::tile_at(uint32_t layer, uint32_t x, uint32_t y) const {
    return data_.tile_at(layer, x, y);
}

void TileMap::set_layer(uint32_t layer, std::span<const uint32_t> tile_indices) {
    data_.set_layer(layer, tile_indices);
}

void TileMap::clear_layer(uint32_t layer) {
    data_.clear_layer(layer);
}

void TileMap::set_offset(Vec2 offset) {
    offset_ = offset;
}

} // namespace xebble
