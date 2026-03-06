/// @file sprite.cpp
/// @brief Sprite implementation.
#include <xebble/sprite.hpp>
#include <cmath>

namespace xebble {

uint32_t Sprite::tile_index() const {
    if (auto* idx = std::get_if<uint32_t>(&source)) {
        return *idx;
    }
    return 0;
}

uint32_t Sprite::resolve_frame(float elapsed) const {
    if (auto* idx = std::get_if<uint32_t>(&source)) {
        return *idx;
    }

    auto& anim = std::get<AnimationDef>(source);
    if (anim.frames.empty()) return 0;
    if (anim.frame_duration <= 0.0f) return anim.frames[0];

    float total_duration = anim.frame_duration * static_cast<float>(anim.frames.size());
    float t = elapsed;

    if (anim.looping) {
        t = std::fmod(t, total_duration);
        if (t < 0.0f) t += total_duration;
    } else {
        t = std::min(t, total_duration - anim.frame_duration);
        t = std::max(t, 0.0f);
    }

    auto frame_idx = static_cast<uint32_t>(t / anim.frame_duration);
    frame_idx = std::min(frame_idx, static_cast<uint32_t>(anim.frames.size() - 1));
    return anim.frames[frame_idx];
}

} // namespace xebble
