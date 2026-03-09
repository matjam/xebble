/// @file test_config.cpp
/// @brief Unit tests for the TOML config loader.
#include <xebble/config.hpp>
#include <xebble/game.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

// Helper: write a string to a temp file and return the path.
std::filesystem::path write_tmp(const std::string& name, const std::string& content) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream ofs(p);
    ofs << content;
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

TEST(Config, DefaultsReturnsValidConfig) {
    auto cfg = xebble::Config::defaults();
    EXPECT_EQ(cfg.window().title, "Xebble");
    EXPECT_EQ(cfg.window().width, 1280u);
    EXPECT_EQ(cfg.window().height, 720u);
    EXPECT_TRUE(cfg.window().resizable);
    EXPECT_FALSE(cfg.window().fullscreen);
    EXPECT_EQ(cfg.renderer().virtual_width, 960u);
    EXPECT_EQ(cfg.renderer().virtual_height, 540u);
    EXPECT_TRUE(cfg.renderer().vsync);
    EXPECT_FLOAT_EQ(cfg.master_volume(), 1.0f);
    EXPECT_FLOAT_EQ(cfg.music_volume(), 1.0f);
    EXPECT_FLOAT_EQ(cfg.sfx_volume(), 1.0f);
}

// ---------------------------------------------------------------------------
// Missing file returns defaults (not an error)
// ---------------------------------------------------------------------------

TEST(Config, MissingFileReturnsDefaults) {
    auto cfg = xebble::Config::load("/tmp/xebble_test_nonexistent_config.toml");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->window().title, "Xebble");
}

// ---------------------------------------------------------------------------
// Full parse
// ---------------------------------------------------------------------------

TEST(Config, ParsesAllSections) {
    auto path = write_tmp("xebble_test_full.toml", R"(
[window]
title      = "TestGame"
width      = 800
height     = 600
resizable  = false
fullscreen = true

[renderer]
virtual_width  = 320
virtual_height = 180
vsync          = false
nearest_sample = true
auto_filter    = false
scale_mode     = "crop"

[audio]
master_volume = 0.5
music_volume  = 0.3
sfx_volume    = 0.9

[assets]
directory = "data"
archive   = "data.zip"
manifest  = "data/manifest.toml"

[game]
seed       = 42
difficulty = "hard"
)");

    auto cfg = xebble::Config::load(path);
    ASSERT_TRUE(cfg.has_value());

    // Window
    EXPECT_EQ(cfg->window().title, "TestGame");
    EXPECT_EQ(cfg->window().width, 800u);
    EXPECT_EQ(cfg->window().height, 600u);
    EXPECT_FALSE(cfg->window().resizable);
    EXPECT_TRUE(cfg->window().fullscreen);

    // Renderer
    EXPECT_EQ(cfg->renderer().virtual_width, 320u);
    EXPECT_EQ(cfg->renderer().virtual_height, 180u);
    EXPECT_FALSE(cfg->renderer().vsync);
    EXPECT_TRUE(cfg->renderer().nearest_sample);
    EXPECT_FALSE(cfg->renderer().auto_filter);
    EXPECT_EQ(cfg->renderer().scale_mode, xebble::ScaleMode::Crop);

    // Audio
    EXPECT_FLOAT_EQ(cfg->master_volume(), 0.5f);
    EXPECT_FLOAT_EQ(cfg->music_volume(), 0.3f);
    EXPECT_FLOAT_EQ(cfg->sfx_volume(), 0.9f);

    // Assets
    EXPECT_EQ(cfg->assets().directory, "data");
    EXPECT_EQ(cfg->assets().archive, "data.zip");
    EXPECT_EQ(cfg->assets().manifest, "data/manifest.toml");

    // Game
    auto seed = cfg->game_value<int64_t>("seed");
    ASSERT_TRUE(seed.has_value());
    EXPECT_EQ(*seed, 42);

    auto diff = cfg->game_value<std::string>("difficulty");
    ASSERT_TRUE(diff.has_value());
    EXPECT_EQ(*diff, "hard");

    // Missing game key
    auto missing = cfg->game_value<int64_t>("nonexistent");
    EXPECT_FALSE(missing.has_value());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Partial config (only some sections present)
// ---------------------------------------------------------------------------

TEST(Config, PartialConfigUsesDefaults) {
    auto path = write_tmp("xebble_test_partial.toml", R"(
[window]
title = "Partial"
)");

    auto cfg = xebble::Config::load(path);
    ASSERT_TRUE(cfg.has_value());

    EXPECT_EQ(cfg->window().title, "Partial");
    EXPECT_EQ(cfg->window().width, 1280u);          // default
    EXPECT_EQ(cfg->renderer().virtual_width, 960u); // default
    EXPECT_FLOAT_EQ(cfg->master_volume(), 1.0f);    // default

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Invalid TOML returns error
// ---------------------------------------------------------------------------

TEST(Config, InvalidTomlReturnsError) {
    auto path = write_tmp("xebble_test_bad.toml", R"(
[window
title = "broken"
)");

    auto cfg = xebble::Config::load(path);
    EXPECT_FALSE(cfg.has_value());
    EXPECT_FALSE(cfg.error().message.empty());

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// to_game_config()
// ---------------------------------------------------------------------------

TEST(Config, ToGameConfigPreservesValues) {
    auto path = write_tmp("xebble_test_convert.toml", R"(
[window]
title = "Convert"
width = 640

[renderer]
virtual_width = 320
)");

    auto cfg = xebble::Config::load(path);
    ASSERT_TRUE(cfg.has_value());

    auto gc = cfg->to_game_config();
    EXPECT_EQ(gc.window.title, "Convert");
    EXPECT_EQ(gc.window.width, 640u);
    EXPECT_EQ(gc.renderer.virtual_width, 320u);

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Scale mode parsing
// ---------------------------------------------------------------------------

TEST(Config, ScaleModeFit) {
    auto path = write_tmp("xebble_test_fit.toml", R"(
[renderer]
scale_mode = "fit"
)");

    auto cfg = xebble::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->renderer().scale_mode, xebble::ScaleMode::Fit);

    std::filesystem::remove(path);
}

TEST(Config, ScaleModeCrop) {
    auto path = write_tmp("xebble_test_crop.toml", R"(
[renderer]
scale_mode = "Crop"
)");

    auto cfg = xebble::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->renderer().scale_mode, xebble::ScaleMode::Crop);

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Empty [game] section
// ---------------------------------------------------------------------------

TEST(Config, EmptyGameSection) {
    auto path = write_tmp("xebble_test_empty_game.toml", R"(
[game]
)");

    auto cfg = xebble::Config::load(path);
    ASSERT_TRUE(cfg.has_value());
    EXPECT_TRUE(cfg->game().empty());

    std::filesystem::remove(path);
}
