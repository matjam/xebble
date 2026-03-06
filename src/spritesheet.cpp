/// @file spritesheet.cpp
/// @brief SpriteSheet implementation.
#include <xebble/spritesheet.hpp>
#include <xebble/log.hpp>

namespace xebble {

struct SpriteSheet::Impl {
    Texture texture;
    uint32_t tile_w = 0;
    uint32_t tile_h = 0;
    uint32_t cols = 0;
    uint32_t rows = 0;

    Impl(Texture tex, uint32_t tw, uint32_t th)
        : texture(std::move(tex)), tile_w(tw), tile_h(th)
    {
        cols = texture.width() / tile_w;
        rows = texture.height() / tile_h;
    }
};

std::expected<SpriteSheet, Error> SpriteSheet::load(
    vk::Context& ctx,
    const std::filesystem::path& image_path,
    uint32_t tile_width, uint32_t tile_height)
{
    auto tex = Texture::load(ctx, image_path);
    if (!tex) return std::unexpected(tex.error());

    return from_texture(std::move(*tex), tile_width, tile_height);
}

std::expected<SpriteSheet, Error> SpriteSheet::from_texture(
    Texture texture, uint32_t tile_width, uint32_t tile_height)
{
    if (tile_width == 0 || tile_height == 0) {
        return std::unexpected(Error{"Tile dimensions must be non-zero"});
    }
    if (texture.width() < tile_width || texture.height() < tile_height) {
        return std::unexpected(Error{"Texture smaller than tile size"});
    }

    SpriteSheet sheet;
    sheet.impl_ = std::make_unique<Impl>(std::move(texture), tile_width, tile_height);
    return sheet;
}

SpriteSheet::~SpriteSheet() = default;
SpriteSheet::SpriteSheet(SpriteSheet&&) noexcept = default;
SpriteSheet& SpriteSheet::operator=(SpriteSheet&&) noexcept = default;

Rect SpriteSheet::region(uint32_t index) const {
    uint32_t col = index % impl_->cols;
    uint32_t row = index / impl_->cols;
    return region(col, row);
}

Rect SpriteSheet::region(uint32_t col, uint32_t row) const {
    return calculate_region(
        impl_->texture.width(), impl_->texture.height(),
        impl_->tile_w, impl_->tile_h,
        col, row);
}

Rect SpriteSheet::calculate_region(uint32_t sheet_width, uint32_t sheet_height,
                                    uint32_t tile_width, uint32_t tile_height,
                                    uint32_t index)
{
    uint32_t cols = sheet_width / tile_width;
    uint32_t col = index % cols;
    uint32_t row = index / cols;
    return calculate_region(sheet_width, sheet_height, tile_width, tile_height, col, row);
}

Rect SpriteSheet::calculate_region(uint32_t sheet_width, uint32_t sheet_height,
                                    uint32_t tile_width, uint32_t tile_height,
                                    uint32_t col, uint32_t row)
{
    float sw = static_cast<float>(sheet_width);
    float sh = static_cast<float>(sheet_height);
    float tw = static_cast<float>(tile_width);
    float th = static_cast<float>(tile_height);

    return Rect{
        .x = (col * tw) / sw,
        .y = (row * th) / sh,
        .w = tw / sw,
        .h = th / sh,
    };
}

uint32_t SpriteSheet::columns() const { return impl_->cols; }
uint32_t SpriteSheet::rows() const { return impl_->rows; }
uint32_t SpriteSheet::tile_width() const { return impl_->tile_w; }
uint32_t SpriteSheet::tile_height() const { return impl_->tile_h; }
const Texture& SpriteSheet::texture() const { return impl_->texture; }

} // namespace xebble
