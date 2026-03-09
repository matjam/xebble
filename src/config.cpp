/// @file config.cpp
/// @brief TOML-based game configuration loader implementation.
#include <xebble/config.hpp>
#include <xebble/game.hpp>
#include <xebble/log.hpp>

#include <filesystem>

namespace xebble {

Config Config::defaults() {
    return Config{};
}

std::expected<Config, Error> Config::load(const std::filesystem::path& path) {
    // If the file doesn't exist, return defaults (not an error).
    if (!std::filesystem::exists(path)) {
        log(LogLevel::Info, "Config file not found: " + path.string() + " (using defaults)");
        return Config::defaults();
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        return std::unexpected(Error{std::string("Failed to parse config: ") + e.what()});
    }

    Config cfg;

    // -----------------------------------------------------------------------
    // [window]
    // -----------------------------------------------------------------------
    if (auto win = tbl["window"]; win.is_table()) {
        auto& w = cfg.window_;
        if (auto v = win["title"].value<std::string>())
            w.title = *v;
        if (auto v = win["width"].value<int64_t>())
            w.width = static_cast<uint32_t>(*v);
        if (auto v = win["height"].value<int64_t>())
            w.height = static_cast<uint32_t>(*v);
        if (auto v = win["resizable"].value<bool>())
            w.resizable = *v;
        if (auto v = win["fullscreen"].value<bool>())
            w.fullscreen = *v;
    }

    // -----------------------------------------------------------------------
    // [renderer]
    // -----------------------------------------------------------------------
    if (auto ren = tbl["renderer"]; ren.is_table()) {
        auto& r = cfg.renderer_;
        if (auto v = ren["virtual_width"].value<int64_t>())
            r.virtual_width = static_cast<uint32_t>(*v);
        if (auto v = ren["virtual_height"].value<int64_t>())
            r.virtual_height = static_cast<uint32_t>(*v);
        if (auto v = ren["vsync"].value<bool>())
            r.vsync = *v;
        if (auto v = ren["nearest_sample"].value<bool>())
            r.nearest_sample = *v;
        if (auto v = ren["auto_filter"].value<bool>())
            r.auto_filter = *v;
        if (auto v = ren["scale_mode"].value<std::string>()) {
            if (*v == "fit" || *v == "Fit")
                r.scale_mode = ScaleMode::Fit;
            else if (*v == "crop" || *v == "Crop")
                r.scale_mode = ScaleMode::Crop;
            else
                log(LogLevel::Warn, "Unknown scale_mode: " + *v + " (using 'fit')");
        }
    }

    // -----------------------------------------------------------------------
    // [audio]
    // -----------------------------------------------------------------------
    if (auto aud = tbl["audio"]; aud.is_table()) {
        if (auto v = aud["master_volume"].value<double>())
            cfg.master_volume_ = static_cast<float>(*v);
        if (auto v = aud["music_volume"].value<double>())
            cfg.music_volume_ = static_cast<float>(*v);
        if (auto v = aud["sfx_volume"].value<double>())
            cfg.sfx_volume_ = static_cast<float>(*v);
    }

    // -----------------------------------------------------------------------
    // [assets]
    // -----------------------------------------------------------------------
    if (auto ast = tbl["assets"]; ast.is_table()) {
        auto& a = cfg.assets_;
        if (auto v = ast["directory"].value<std::string>())
            a.directory = *v;
        if (auto v = ast["archive"].value<std::string>())
            a.archive = *v;
        if (auto v = ast["manifest"].value<std::string>())
            a.manifest = *v;
    }

    // -----------------------------------------------------------------------
    // [game] -- raw table, not interpreted by the engine
    // -----------------------------------------------------------------------
    if (auto game = tbl["game"]; game.is_table()) {
        cfg.game_ = *game.as_table();
    }

    log(LogLevel::Info, "Config loaded from: " + path.string());
    return cfg;
}

GameConfig Config::to_game_config() const {
    return GameConfig{
        .window = window_,
        .renderer = renderer_,
        .assets = assets_,
    };
}

} // namespace xebble
