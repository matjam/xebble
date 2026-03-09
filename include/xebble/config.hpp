/// @file config.hpp
/// @brief TOML-based game configuration loader.
///
/// Loads engine and game settings from a single `game.toml` file with sections
/// for `[window]`, `[renderer]`, `[audio]`, and `[game]`.  Engine sections map
/// directly to the existing config structs (`WindowConfig`, `RendererConfig`);
/// the `[game]` section is exposed as a raw `toml::table` for arbitrary
/// game-specific key/value data.
///
/// ## game.toml example
///
/// @code{.toml}
/// [window]
/// title      = "Dungeon Delve"
/// width      = 1280
/// height     = 720
/// resizable  = true
/// fullscreen = false
///
/// [renderer]
/// virtual_width  = 640
/// virtual_height = 360
/// vsync          = true
/// scale_mode     = "fit"     # "fit" or "crop"
///
/// [audio]
/// master_volume = 0.8
/// music_volume  = 0.6
/// sfx_volume    = 1.0
///
/// [assets]
/// directory = "assets"
/// archive   = "assets.zip"
/// manifest  = "assets/manifest.toml"
///
/// [game]
/// seed       = 42
/// difficulty = "hard"
/// @endcode
///
/// ## Usage from main.cpp
///
/// @code
/// int main() {
///     auto cfg = xebble::Config::load("game.toml");
///     if (!cfg) {
///         xebble::log(LogLevel::Error, cfg.error().message);
///         return 1;
///     }
///
///     // Read game-specific values:
///     int seed = cfg->game_value<int>("seed").value_or(42);
///
///     World world;
///     world.add_system<MyGame>();
///     return xebble::run(std::move(world), cfg->to_game_config());
/// }
/// @endcode
#pragma once

#include <xebble/asset_manager.hpp>
#include <xebble/renderer.hpp>
#include <xebble/types.hpp>
#include <xebble/window.hpp>

#include <toml++/toml.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace xebble {

struct GameConfig; // Forward declaration; defined in game.hpp.

/// @brief Parsed game configuration from a TOML file.
///
/// Engine sections are parsed into their respective config structs.
/// The `[game]` section is available as a raw TOML table for game-specific
/// key/value data — the engine does not interpret it.
class Config {
public:
    /// @brief Load configuration from a TOML file.
    ///
    /// Missing sections or keys use defaults from the corresponding config
    /// structs.  The file itself is optional: if the path does not exist, a
    /// `Config` with all defaults is returned (no error).
    ///
    /// @param path  Path to the TOML configuration file.
    /// @return Parsed `Config`, or an error if the file exists but cannot be
    ///         parsed.
    [[nodiscard]] static std::expected<Config, Error> load(const std::filesystem::path& path);

    /// @brief Construct a Config with all defaults (no file).
    [[nodiscard]] static Config defaults();

    /// @brief Convert to a `GameConfig` suitable for `xebble::run()`.
    [[nodiscard]] GameConfig to_game_config() const;

    /// @brief Access the `[window]` configuration.
    [[nodiscard]] const WindowConfig& window() const { return window_; }

    /// @brief Access the `[renderer]` configuration.
    [[nodiscard]] const RendererConfig& renderer() const { return renderer_; }

    /// @brief Access the `[assets]` configuration.
    [[nodiscard]] const AssetConfig& assets() const { return assets_; }

    /// @brief Access the raw `[game]` table for game-specific values.
    [[nodiscard]] const toml::table& game() const { return game_; }

    /// @brief Read a typed value from the `[game]` section.
    ///
    /// @code
    /// int seed = config.game_value<int64_t>("seed").value_or(42);
    /// auto difficulty = config.game_value<std::string>("difficulty").value_or("normal");
    /// @endcode
    template<typename T>
    [[nodiscard]] std::optional<T> game_value(std::string_view key) const {
        if (auto node = game_[key]; node) {
            if (auto val = node.value<T>(); val) {
                return *val;
            }
        }
        return std::nullopt;
    }

    /// @brief Master audio volume [0.0, 1.0].  Applied to AudioEngine on startup.
    [[nodiscard]] float master_volume() const { return master_volume_; }

    /// @brief Music volume [0.0, 1.0].
    [[nodiscard]] float music_volume() const { return music_volume_; }

    /// @brief Sound effect volume [0.0, 1.0].
    [[nodiscard]] float sfx_volume() const { return sfx_volume_; }

private:
    WindowConfig window_;
    RendererConfig renderer_;
    AssetConfig assets_;
    toml::table game_;

    float master_volume_ = 1.0f;
    float music_volume_ = 1.0f;
    float sfx_volume_ = 1.0f;
};

} // namespace xebble
