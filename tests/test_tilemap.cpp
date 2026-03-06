#include <gtest/gtest.h>
#include <xebble/tilemap.hpp>

using namespace xebble;

TEST(TileMapData, SetAndGetTile) {
    TileMapData data(40, 22, 3);
    data.set_tile(0, 5, 5, 42);
    EXPECT_EQ(data.tile_at(0, 5, 5), 42u);
    EXPECT_EQ(data.tile_at(0, 0, 0), std::nullopt);
}

TEST(TileMapData, ClearTile) {
    TileMapData data(10, 10, 1);
    data.set_tile(0, 3, 3, 10);
    data.clear_tile(0, 3, 3);
    EXPECT_EQ(data.tile_at(0, 3, 3), std::nullopt);
}

TEST(TileMapData, ClearLayer) {
    TileMapData data(10, 10, 2);
    data.set_tile(0, 0, 0, 1);
    data.set_tile(1, 0, 0, 2);
    data.clear_layer(0);
    EXPECT_EQ(data.tile_at(0, 0, 0), std::nullopt);
    EXPECT_EQ(data.tile_at(1, 0, 0), 2u);
}

TEST(TileMapData, SetLayer) {
    TileMapData data(3, 2, 1);
    std::vector<uint32_t> tiles = {1, 2, 3, 4, 5, 6};
    data.set_layer(0, tiles);
    EXPECT_EQ(data.tile_at(0, 0, 0), 1u);
    EXPECT_EQ(data.tile_at(0, 2, 0), 3u);
    EXPECT_EQ(data.tile_at(0, 0, 1), 4u);
    EXPECT_EQ(data.tile_at(0, 2, 1), 6u);
}

TEST(TileMapData, Dimensions) {
    TileMapData data(40, 22, 3);
    EXPECT_EQ(data.width(), 40u);
    EXPECT_EQ(data.height(), 22u);
    EXPECT_EQ(data.layer_count(), 3u);
}
