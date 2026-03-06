/// @file asset_manager.hpp
/// @brief Asset manager with TOML manifest parsing and ZIP archive support.
///
/// Loads assets declared in a TOML manifest file. Files are resolved by
/// checking the directory first, then falling back to a ZIP archive.
/// All assets are loaded on create() and cached by name.
#pragma once

#include <xebble/types.hpp>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace xebble {

class Renderer;

namespace vk {
class Context;
}

/// @brief Configuration for asset loading.
struct AssetConfig {
    std::filesystem::path directory;    ///< Directory to search for loose files.
    std::filesystem::path archive;      ///< Optional ZIP archive path.
    std::filesystem::path manifest;     ///< Path to the TOML manifest file.
};

/// @brief Parsed spritesheet entry from the manifest.
struct SpriteSheetEntry {
    std::string path;
    uint32_t tile_width = 0;
    uint32_t tile_height = 0;
};

/// @brief Parsed bitmap font entry from the manifest.
struct BitmapFontEntry {
    std::string path;
    uint32_t glyph_width = 0;
    uint32_t glyph_height = 0;
    std::string charset;
};

/// @brief Parsed TrueType font entry from the manifest.
struct FontEntry {
    std::string path;
    uint32_t pixel_size = 16;
};

/// @brief Parsed manifest data.
struct Manifest {
    std::unordered_map<std::string, SpriteSheetEntry> spritesheets;
    std::unordered_map<std::string, BitmapFontEntry> bitmap_fonts;
    std::unordered_map<std::string, FontEntry> fonts;
};

/// @brief Parse a TOML manifest string into a Manifest struct.
Manifest parse_manifest(std::string_view toml_str);

/// @brief Manages loaded assets keyed by manifest name.
///
/// On create(), parses the manifest, resolves files from directory or ZIP,
/// loads all declared assets, and caches them. Use get<T>() to retrieve.
class AssetManager {
public:
    /// @brief Create the asset manager, loading all declared assets.
    /// @param ctx Vulkan context for GPU resource creation.
    /// @param config Asset configuration (paths).
    static std::expected<AssetManager, Error> create(
        vk::Context& ctx, const AssetConfig& config);

    ~AssetManager();
    AssetManager(AssetManager&&) noexcept;
    AssetManager& operator=(AssetManager&&) noexcept;
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    /// @brief Retrieve a loaded asset by name.
    /// @tparam T The asset type (SpriteSheet, BitmapFont, Font).
    /// @param name The manifest name for the asset.
    template<typename T>
    const T& get(std::string_view name) const {
        auto it = assets_.find(std::string(name));
        auto ptr = std::static_pointer_cast<T>(it->second);
        return *ptr;
    }

    /// @brief Check if an asset with the given name exists.
    bool has(std::string_view name) const;

    /// @brief Read raw file bytes from the asset sources.
    /// @param path Relative path to resolve (directory first, then archive).
    std::expected<std::vector<uint8_t>, Error> read_raw(std::string_view path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::unordered_map<std::string, std::shared_ptr<void>> assets_;
    AssetManager() = default;
};

} // namespace xebble
