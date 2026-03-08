/// @file debug.hpp
/// @brief Debug overlay utilities for development and profiling.
///
/// Provides a simple one-call debug panel showing FPS and frame time.
/// Call from any system's `draw()` method:
///
/// @code
/// void draw(xebble::World& world, xebble::Renderer& renderer) override {
///     xebble::debug_overlay(world, renderer);
/// }
/// @endcode
#pragma once

namespace xebble {

class World;
class Renderer;

/// @brief Draw a small debug panel showing FPS and frame time.
///
/// Renders a TopRight-anchored panel displaying the current frames per second
/// and per-frame time in milliseconds.  Call once per frame from a system's
/// `draw()` override.
///
/// @param world    The ECS world (used to access UIContext).
/// @param renderer The renderer (used for timing information).
void debug_overlay(World& world, Renderer& renderer);

} // namespace xebble
