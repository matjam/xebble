/// @file debug.cpp
/// @brief Debug overlay implementation.
#include <xebble/debug.hpp>
#include <xebble/renderer.hpp>
#include <xebble/ui.hpp>
#include <xebble/world.hpp>

#include <format>
#include <string>

namespace xebble {

void debug_overlay(World& world, Renderer& renderer) {
    float dt = renderer.delta_time();
    float fps = (dt > 0.0f) ? 1.0f / dt : 0.0f;
    float ms = dt * 1000.0f;

    auto& ui = world.resource<UIContext>();
    ui.panel("__debug_fps", {.anchor = Anchor::TopRight, .size = {130.0f, 30.0f}}, [&](auto& p) {
        auto s = std::format("{:.0f} fps  {:.1f} ms", fps, ms);
        p.text(std::u8string(s.begin(), s.end()), {.color = {180, 255, 180}});
    });
}

} // namespace xebble
