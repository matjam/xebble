/// @file sprite.hpp
/// @brief Sprite and animation types for 2D rendering.
///
/// A Sprite references a SpriteSheet and defines position, z-order, and either
/// a static tile index or an AnimationDef for animated sprites. Animation
/// advancement is handled by the renderer using delta_time().
#pragma once

#include <xebble/types.hpp>
#include <cstdint>
#include <variant>
#include <vector>

namespace xebble {

class SpriteSheet;

/// @brief Defines a sprite animation as a sequence of tile indices.
struct AnimationDef {
    std::vector<uint32_t> frames;   ///< Tile indices in the spritesheet.
    float frame_duration = 0.1f;    ///< Seconds per frame.
    bool looping = true;            ///< Whether the animation loops.
};

/// @brief A drawable sprite referencing a spritesheet tile or animation.
struct Sprite {
    Vec2 position;                  ///< Position in virtual pixels.
    float z_order = 0.0f;           ///< Draw order (lower = behind).
    const SpriteSheet* sheet = nullptr; ///< The spritesheet to sample from.
    std::variant<uint32_t, AnimationDef> source = uint32_t{0}; ///< Static tile index or animation.

    /// @brief Get the current tile index for a static sprite.
    /// For animated sprites, use resolve_frame() with elapsed time.
    uint32_t tile_index() const;

    /// @brief Resolve the current frame index for an animated sprite.
    /// @param elapsed Total elapsed time in seconds since animation start.
    /// @return The tile index for the current frame.
    uint32_t resolve_frame(float elapsed) const;
};

} // namespace xebble
