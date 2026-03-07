/// @file asset_manager.hpp
/// @brief Asset manager with TOML manifest parsing and ZIP archive support.
///
/// `AssetManager` is the central hub for loading and accessing all game
/// resources (spritesheets, bitmap fonts, TrueType fonts). Assets are declared
/// in a TOML manifest file; the manager resolves each path by checking a loose
/// file directory first, then falling back to a ZIP archive. All assets are
/// loaded eagerly on `AssetManager::create()` and cached so subsequent `get<T>`
/// calls are zero-cost lookups.
///
/// ## Asset manifest format
///
/// The TOML manifest uses three top-level tables:
///
/// ```toml
/// # assets/manifest.toml
///
/// [spritesheets]
/// [spritesheets.tiles]
/// path         = "tilesets/dungeon.png"
/// tile_width   = 16
/// tile_height  = 16
///
/// [bitmap_fonts]
/// [bitmap_fonts.main]
/// path         = "fonts/cp437.png"
/// glyph_width  = 8
/// glyph_height = 16
/// charset      = " !\"#$%&'()*+,-./" # first 32 printable ASCII chars …
///
/// [fonts]
/// [fonts.ui]
/// path         = "fonts/inter.ttf"
/// pixel_size   = 18
/// ```
///
/// ## Quick-start
///
/// @code
/// #include <xebble/asset_manager.hpp>
/// #include <xebble/spritesheet.hpp>
/// using namespace xebble;
///
/// AssetConfig cfg{
///     .directory = "assets",
///     .archive   = "assets.zip",   // optional; used as fallback
///     .manifest  = "assets/manifest.toml",
/// };
///
/// auto result = AssetManager::create(vk_ctx, cfg);
/// if (!result) { log_error(result.error().message); return; }
///
/// AssetManager assets = std::move(*result);
///
/// // Retrieve cached resources by manifest name:
/// const SpriteSheet& tiles = assets.get<SpriteSheet>("tiles");
/// const BitmapFont&  font  = assets.get<BitmapFont>("main");
/// @endcode
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
///
/// Specifies where the manager should look for asset files and the manifest
/// that describes which assets to load. The directory and archive are
/// complementary: the directory is checked first (fast iteration during
/// development), then the archive (distribution builds where all assets are
/// packed).
///
/// @code
/// AssetConfig cfg{
///     .directory = "assets",        // loose files during development
///     .archive   = "game.zip",      // packed archive for release
///     .manifest  = "assets/manifest.toml",
/// };
/// @endcode
struct AssetConfig {
    std::filesystem::path directory;    ///< Directory to search for loose files.
    std::filesystem::path archive;      ///< Optional ZIP archive path.
    std::filesystem::path manifest;     ///< Path to the TOML manifest file.
};

/// @brief Parsed spritesheet entry from the manifest.
///
/// Describes a single spritesheet: the image file and the uniform tile size.
/// Every tile in the sheet is assumed to have the same pixel dimensions.
///
/// Manifest example:
/// ```toml
/// [spritesheets.dungeon]
/// path        = "tilesets/dungeon.png"
/// tile_width  = 16
/// tile_height = 16
/// ```
struct SpriteSheetEntry {
    std::string path;           ///< Relative path to the image file.
    uint32_t tile_width  = 0;  ///< Width of each tile in pixels.
    uint32_t tile_height = 0;  ///< Height of each tile in pixels.
};

/// @brief Parsed bitmap font entry from the manifest.
///
/// Describes a fixed-size bitmap font: the sheet image, glyph dimensions,
/// and the character set encoded in the sheet (left-to-right, top-to-bottom).
///
/// Manifest example:
/// ```toml
/// [bitmap_fonts.cp437]
/// path         = "fonts/cp437_8x16.png"
/// glyph_width  = 8
/// glyph_height = 16
/// charset      = " !\"#$%&'()*+,-./0123456789:;<=>?"
/// ```
struct BitmapFontEntry {
    std::string path;            ///< Relative path to the font sheet image.
    uint32_t glyph_width  = 0;  ///< Width of one glyph cell in pixels.
    uint32_t glyph_height = 0;  ///< Height of one glyph cell in pixels.
    std::u8string charset;       ///< Characters encoded in the sheet, in order.
};

/// @brief Parsed TrueType font entry from the manifest.
///
/// References a `.ttf` or `.otf` file that will be rasterised at the given
/// pixel size when loaded.
///
/// Manifest example:
/// ```toml
/// [fonts.ui]
/// path       = "fonts/inter.ttf"
/// pixel_size = 18
/// ```
struct FontEntry {
    std::string path;            ///< Relative path to the font file.
    uint32_t pixel_size = 16;   ///< Render size in pixels (height).
};

/// @brief Parsed manifest data.
///
/// The in-memory representation of the TOML manifest. Normally you do not
/// need to interact with this directly — `AssetManager::create()` parses and
/// consumes it internally. It is exposed for testing and tooling.
struct Manifest {
    std::unordered_map<std::string, SpriteSheetEntry> spritesheets; ///< All declared spritesheets.
    std::unordered_map<std::string, BitmapFontEntry>  bitmap_fonts; ///< All declared bitmap fonts.
    std::unordered_map<std::string, FontEntry>         fonts;        ///< All declared TrueType fonts.
};

/// @brief Parse a TOML manifest string into a Manifest struct.
///
/// This is called internally by `AssetManager::create()` but is also useful
/// for unit-testing manifests or building custom asset pipelines.
///
/// @param toml_str  Contents of the TOML manifest file.
/// @return          Populated Manifest.
///
/// @code
/// std::string src = R"(
///     [spritesheets.tiles]
///     path        = "dungeon.png"
///     tile_width  = 16
///     tile_height = 16
/// )";
/// Manifest m = parse_manifest(src);
/// assert(m.spritesheets.count("tiles") == 1);
/// @endcode
Manifest parse_manifest(std::string_view toml_str);

/// @brief Manages loaded assets keyed by manifest name.
///
/// On `create()`, `AssetManager` parses the manifest, resolves each declared
/// file from the directory or ZIP archive, loads it into a GPU resource, and
/// stores a type-erased shared pointer. Use `get<T>()` to retrieve the asset
/// as its concrete type.
///
/// `AssetManager` is move-only because it owns GPU resources.
///
/// ## Usage
///
/// @code
/// auto mgr = AssetManager::create(ctx, cfg).value();
///
/// // Spritesheets
/// const SpriteSheet& dungeon = mgr.get<SpriteSheet>("dungeon");
/// renderer.draw_sprite(dungeon.sprite_at(3, 1), pos);
///
/// // Bitmap fonts (for tile-aligned text rendering)
/// const BitmapFont& cp437 = mgr.get<BitmapFont>("cp437");
/// renderer.draw_text(cp437, "Hello, dungeon!", {10, 2});
///
/// // TrueType fonts (for scalable UI text)
/// const Font& ui_font = mgr.get<Font>("ui");
/// renderer.draw_string(ui_font, "Inventory", {20.0f, 40.0f});
/// @endcode
///
/// ## Checking existence
///
/// @code
/// if (mgr.has("optional_music_sheet")) {
///     const SpriteSheet& music = mgr.get<SpriteSheet>("optional_music_sheet");
/// }
/// @endcode
///
/// ## Reading raw bytes
///
/// @code
/// // Load a custom binary data file via the same search path.
/// auto bytes = mgr.read_raw("data/level1.bin");
/// if (bytes) {
///     parse_level(*bytes);
/// }
/// @endcode
class AssetManager {
public:
    /// @brief Create the asset manager, loading all declared assets.
    ///
    /// Parses the TOML manifest, resolves each file (directory then archive),
    /// and uploads GPU resources. Returns an error if any required file is
    /// missing or the manifest is malformed.
    ///
    /// @param ctx     Vulkan context used to create GPU textures.
    /// @param config  Paths for the directory, archive, and manifest.
    /// @return        A ready AssetManager, or an Error on failure.
    ///
    /// @code
    /// AssetConfig cfg{
    ///     .directory = "assets",
    ///     .archive   = "assets.zip",
    ///     .manifest  = "assets/manifest.toml",
    /// };
    /// auto result = AssetManager::create(vk_ctx, cfg);
    /// if (!result) {
    ///     log_error("Failed to load assets: {}", result.error().message);
    ///     return EXIT_FAILURE;
    /// }
    /// AssetManager assets = std::move(*result);
    /// @endcode
    static std::expected<AssetManager, Error> create(
        vk::Context& ctx, const AssetConfig& config);

    ~AssetManager();
    AssetManager(AssetManager&&) noexcept;
    AssetManager& operator=(AssetManager&&) noexcept;
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    /// @brief Retrieve a loaded asset by manifest name.
    ///
    /// The asset must have been declared in the manifest and successfully
    /// loaded. The type parameter `T` must match the actual asset type
    /// (SpriteSheet, BitmapFont, or Font). Passing the wrong type is
    /// undefined behaviour.
    ///
    /// @tparam T    Asset type: `SpriteSheet`, `BitmapFont`, or `Font`.
    /// @param name  The manifest key used to declare the asset.
    /// @return      A const reference valid for the lifetime of this manager.
    ///
    /// @code
    /// // Retrieve a spritesheet by its manifest name.
    /// const SpriteSheet& dungeon = assets.get<SpriteSheet>("dungeon");
    ///
    /// // Retrieve a bitmap font for roguelike tile-grid text.
    /// const BitmapFont& cp437 = assets.get<BitmapFont>("cp437");
    ///
    /// // Retrieve a scalable TrueType font for HUD text.
    /// const Font& hud = assets.get<Font>("hud");
    /// @endcode
    template<typename T>
    const T& get(std::string_view name) const {
        auto it = assets_.find(std::string(name));
        auto ptr = std::static_pointer_cast<T>(it->second);
        return *ptr;
    }

    /// @brief Check if an asset with the given name was successfully loaded.
    ///
    /// Useful for optional assets that may or may not be present depending on
    /// the game configuration or DLC.
    ///
    /// @code
    /// if (assets.has("dlc_tileset")) {
    ///     const SpriteSheet& dlc = assets.get<SpriteSheet>("dlc_tileset");
    ///     // Use DLC tiles...
    /// }
    /// @endcode
    bool has(std::string_view name) const;

    /// @brief Read raw file bytes from the asset sources.
    ///
    /// Searches the directory first, then the ZIP archive, returning the raw
    /// byte content. Useful for non-standard asset types (level data, scripts,
    /// config blobs) that the manager does not otherwise process.
    ///
    /// @param path  Relative path to the file (e.g. "data/level1.bin").
    /// @return      Byte vector on success, or an Error if not found.
    ///
    /// @code
    /// auto bytes = assets.read_raw("maps/overworld.bin");
    /// if (!bytes) {
    ///     log_error("Map not found: {}", bytes.error().message);
    ///     return;
    /// }
    /// Map map = Map::deserialize(*bytes);
    /// @endcode
    std::expected<std::vector<uint8_t>, Error> read_raw(std::string_view path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::unordered_map<std::string, std::shared_ptr<void>> assets_;
    AssetManager() = default;
};

} // namespace xebble
