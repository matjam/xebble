/// @file embedded_fonts_helpers.cpp
/// @brief World-level helpers that load an embedded font and wire it into UITheme.

#include <xebble/embedded_fonts.hpp>
#include <xebble/ui.hpp>
#include <xebble/world.hpp>

#include <memory>

namespace xebble::embedded_fonts {

namespace {

// Tag types used as World resource keys.
// std::any requires copy-constructibility, so we wrap in shared_ptr.
struct Berkelium64Resource {
    std::shared_ptr<Font> font;
};
struct PetMe64Resource {
    std::shared_ptr<BitmapFont> font;
};
struct PetMe642YResource {
    std::shared_ptr<BitmapFont> font;
};

vk::Context& ctx_from_world(World& world) {
    return world.resource<Renderer*>()->context();
}

UITheme& get_or_add_theme(World& world) {
    if (!world.has_resource<UITheme>())
        world.add_resource(UITheme{});
    return world.resource<UITheme>();
}

} // namespace

void use_berkelium64(World& world) {
    auto f = berkelium64::create(ctx_from_world(world));
    if (!f)
        return;
    world.add_resource(Berkelium64Resource{std::make_shared<Font>(std::move(*f))});
    get_or_add_theme(world).font = world.resource<Berkelium64Resource>().font.get();
}

void use_petme64(World& world) {
    auto f = petme64::create(ctx_from_world(world));
    if (!f)
        return;
    world.add_resource(PetMe64Resource{std::make_shared<BitmapFont>(std::move(*f))});
    get_or_add_theme(world).font = world.resource<PetMe64Resource>().font.get();
}

void use_petme642y(World& world) {
    auto f = petme642y::create(ctx_from_world(world));
    if (!f)
        return;
    world.add_resource(PetMe642YResource{std::make_shared<BitmapFont>(std::move(*f))});
    get_or_add_theme(world).font = world.resource<PetMe642YResource>().font.get();
}

} // namespace xebble::embedded_fonts
