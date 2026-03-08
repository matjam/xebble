/// @file asset_manager.cpp
/// @brief Asset manager implementation with TOML manifest and ZIP support.
#include "vulkan/context.hpp"

#include <xebble/asset_manager.hpp>
#include <xebble/font.hpp>
#include <xebble/log.hpp>
#include <xebble/spritesheet.hpp>
#include <xebble/texture.hpp>

#include <toml++/toml.hpp>

#include <fstream>
#include <miniz/miniz.h>

namespace xebble {

// --- Manifest parsing ---

Manifest parse_manifest(std::string_view toml_str) {
    Manifest manifest;
    auto tbl = toml::parse(toml_str);

    if (auto sheets = tbl["spritesheets"].as_table()) {
        for (auto& [name, val] : *sheets) {
            auto& entry = manifest.spritesheets[std::string(name.str())];
            auto& t = *val.as_table();
            entry.path = t["path"].value_or(std::string{});
            entry.tile_width = t["tile_width"].value_or(uint32_t{0});
            entry.tile_height = t["tile_height"].value_or(uint32_t{0});
        }
    }

    if (auto fonts = tbl["bitmap_fonts"].as_table()) {
        for (auto& [name, val] : *fonts) {
            auto& entry = manifest.bitmap_fonts[std::string(name.str())];
            auto& t = *val.as_table();
            entry.path = t["path"].value_or(std::string{});
            entry.glyph_width = t["glyph_width"].value_or(uint32_t{0});
            entry.glyph_height = t["glyph_height"].value_or(uint32_t{0});
            {
                auto cs = t["charset"].value_or(std::string{});
                entry.charset = std::u8string(cs.begin(), cs.end());
            }
        }
    }

    if (auto fonts = tbl["fonts"].as_table()) {
        for (auto& [name, val] : *fonts) {
            auto& entry = manifest.fonts[std::string(name.str())];
            auto& t = *val.as_table();
            entry.path = t["path"].value_or(std::string{});
            entry.pixel_size = t["pixel_size"].value_or(uint32_t{16});
        }
    }

    if (auto sounds = tbl["sounds"].as_table()) {
        for (auto& [name, val] : *sounds) {
            auto& entry = manifest.sounds[std::string(name.str())];
            auto& t = *val.as_table();
            entry.path = t["path"].value_or(std::string{});
        }
    }

    if (auto music = tbl["music"].as_table()) {
        for (auto& [name, val] : *music) {
            auto& entry = manifest.music[std::string(name.str())];
            auto& t = *val.as_table();
            entry.path = t["path"].value_or(std::string{});
        }
    }

    return manifest;
}

// --- Archive reader ---

struct AssetManager::Impl {
    std::filesystem::path directory;
    std::filesystem::path archive_path;

    std::expected<std::vector<uint8_t>, Error> read_from_directory(std::string_view path) const {
        auto full_path = directory / path;
        if (!std::filesystem::exists(full_path)) {
            return std::unexpected(Error{"File not found: " + full_path.string()});
        }

        std::ifstream file(full_path, std::ios::binary | std::ios::ate);
        if (!file) {
            return std::unexpected(Error{"Cannot open file: " + full_path.string()});
        }

        auto size = file.tellg();
        file.seekg(0);
        std::vector<uint8_t> data(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(data.data()), size);
        return data;
    }

    std::expected<std::vector<uint8_t>, Error> read_from_archive(std::string_view path) const {
        if (archive_path.empty() || !std::filesystem::exists(archive_path)) {
            return std::unexpected(Error{"No archive available"});
        }

        mz_zip_archive zip{};
        if (mz_zip_reader_init_file(&zip, archive_path.string().c_str(), 0) == MZ_FALSE) {
            return std::unexpected(Error{"Failed to open archive: " + archive_path.string()});
        }

        const std::string path_str(path);
        size_t out_size = 0;
        void* buf = mz_zip_reader_extract_file_to_heap(&zip, path_str.c_str(), &out_size, 0);

        mz_zip_reader_end(&zip);

        if (buf == nullptr) {
            return std::unexpected(Error{"Entry not found in archive: " + path_str});
        }

        std::vector<uint8_t> data(static_cast<uint8_t*>(buf),
                                  static_cast<uint8_t*>(buf) + out_size);
        mz_free(buf);
        return data;
    }

    std::expected<std::vector<uint8_t>, Error> resolve(std::string_view path) const {
        // Directory first, then archive
        auto full_path = directory / path;
        if (std::filesystem::exists(full_path)) {
            return read_from_directory(path);
        }
        return read_from_archive(path);
    }
};

// --- AssetManager ---

std::expected<AssetManager, Error> AssetManager::create(vk::Context& ctx,
                                                        const AssetConfig& config) {
    AssetManager mgr;
    mgr.impl_ = std::make_unique<Impl>();
    mgr.impl_->directory = config.directory;
    mgr.impl_->archive_path = config.archive;

    // Read and parse manifest
    if (!config.manifest.empty() && std::filesystem::exists(config.manifest)) {
        std::ifstream mf(config.manifest);
        if (!mf) {
            return std::unexpected(Error{"Cannot open manifest: " + config.manifest.string()});
        }
        std::string manifest_str((std::istreambuf_iterator<char>(mf)),
                                 std::istreambuf_iterator<char>());

        auto manifest = parse_manifest(manifest_str);

        // Load spritesheets
        for (auto& [name, entry] : manifest.spritesheets) {
            auto data = mgr.impl_->resolve(entry.path);
            if (!data) {
                log(LogLevel::Warn,
                    "Failed to load spritesheet '" + name + "': " + data.error().message);
                continue;
            }

            auto tex = Texture::load_from_memory(ctx, data->data(), data->size());
            if (!tex) {
                log(LogLevel::Warn,
                    "Failed to create texture for '" + name + "': " + tex.error().message);
                continue;
            }

            auto sheet =
                SpriteSheet::from_texture(std::move(*tex), entry.tile_width, entry.tile_height);
            if (!sheet) {
                log(LogLevel::Warn,
                    "Failed to create spritesheet '" + name + "': " + sheet.error().message);
                continue;
            }

            mgr.assets_[name] = std::make_shared<SpriteSheet>(std::move(*sheet));
            log(LogLevel::Info, "Loaded spritesheet: " + name);
        }

        // Load bitmap fonts
        for (auto& [name, entry] : manifest.bitmap_fonts) {
            auto data = mgr.impl_->resolve(entry.path);
            if (!data) {
                log(LogLevel::Warn,
                    "Failed to load bitmap font '" + name + "': " + data.error().message);
                continue;
            }

            auto tex = Texture::load_from_memory(ctx, data->data(), data->size());
            if (!tex) {
                log(LogLevel::Warn,
                    "Failed to create texture for font '" + name + "': " + tex.error().message);
                continue;
            }

            auto sheet =
                SpriteSheet::from_texture(std::move(*tex), entry.glyph_width, entry.glyph_height);
            if (!sheet) {
                log(LogLevel::Warn, "Failed to create spritesheet for font '" + name +
                                        "': " + sheet.error().message);
                continue;
            }

            auto bmfont = BitmapFont::from_spritesheet(std::move(*sheet), entry.charset);
            if (!bmfont) {
                log(LogLevel::Warn,
                    "Failed to create bitmap font '" + name + "': " + bmfont.error().message);
                continue;
            }

            mgr.assets_[name] = std::make_shared<BitmapFont>(std::move(*bmfont));
            log(LogLevel::Info, "Loaded bitmap font: " + name);
        }

        // Load TrueType fonts
        for (auto& [name, entry] : manifest.fonts) {
            // FreeType needs a file path, so resolve to directory
            auto full_path = config.directory / entry.path;
            if (!std::filesystem::exists(full_path)) {
                log(LogLevel::Warn, "Font file not found: " + full_path.string());
                continue;
            }

            auto font = Font::load(ctx, full_path, entry.pixel_size);
            if (!font) {
                log(LogLevel::Warn, "Failed to load font '" + name + "': " + font.error().message);
                continue;
            }

            mgr.assets_[name] = std::make_shared<Font>(std::move(*font));
            log(LogLevel::Info, "Loaded font: " + name);
        }

        // Load sound effects (raw bytes — decoded by AudioEngine on first play)
        for (auto& [name, entry] : manifest.sounds) {
            auto data = mgr.impl_->resolve(entry.path);
            if (!data) {
                log(LogLevel::Warn, "Failed to load sound '" + name + "': " + data.error().message);
                continue;
            }
            mgr.assets_[name] = std::make_shared<std::vector<uint8_t>>(std::move(*data));
            log(LogLevel::Info, "Loaded sound: " + name);
        }

        // Load music tracks (raw bytes — decoded by AudioEngine at playtime)
        for (auto& [name, entry] : manifest.music) {
            auto data = mgr.impl_->resolve(entry.path);
            if (!data) {
                log(LogLevel::Warn, "Failed to load music '" + name + "': " + data.error().message);
                continue;
            }
            mgr.assets_[name] = std::make_shared<std::vector<uint8_t>>(std::move(*data));
            log(LogLevel::Info, "Loaded music: " + name);
        }
    }

    return mgr;
}

AssetManager::~AssetManager() = default;
AssetManager::AssetManager(AssetManager&&) noexcept = default;
AssetManager& AssetManager::operator=(AssetManager&&) noexcept = default;

bool AssetManager::has(std::string_view name) const {
    return assets_.contains(std::string(name));
}

std::expected<std::vector<uint8_t>, Error> AssetManager::read_raw(std::string_view path) const {
    return impl_->resolve(path);
}

} // namespace xebble
