#include "vulkan/buffer.hpp"
#include "vulkan/command.hpp"
#include "vulkan/context.hpp"

#include <xebble/log.hpp>
#include <xebble/texture.hpp>

#include <stb_image.h>

namespace xebble {

struct Texture::Impl {
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;

    ~Impl() {
        if (sampler)
            vkDestroySampler(device, sampler, nullptr);
        if (view)
            vkDestroyImageView(device, view, nullptr);
        if (image && allocator)
            vmaDestroyImage(allocator, image, allocation);
    }
};

namespace {

std::expected<void, Error> transition_image_layout(VkDevice device, VkCommandPool pool,
                                                   VkQueue queue, VkImage image,
                                                   VkImageLayout old_layout,
                                                   VkImageLayout new_layout) {
    auto cmd = vk::begin_one_shot(device, pool);
    if (!cmd)
        return std::unexpected(cmd.error());

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage, dst_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        return std::unexpected(Error{"Unsupported image layout transition"});
    }

    vkCmdPipelineBarrier(*cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    return vk::end_one_shot(device, pool, queue, *cmd);
}

} // anonymous namespace

std::expected<Texture, Error> Texture::create_from_pixels(vk::Context& ctx, const uint8_t* pixels,
                                                          uint32_t width, uint32_t height) {
    VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * height * 4;

    // Create staging buffer
    auto staging = vk::Buffer::create(ctx.allocator(), image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      VMA_MEMORY_USAGE_AUTO,
                                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    if (!staging)
        return std::unexpected(staging.error());
    staging->upload(pixels, image_size);

    // Create image
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_SRGB;
    ici.extent = {width, height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;

    Texture tex;
    tex.impl_ = std::make_unique<Texture::Impl>();
    tex.impl_->device = ctx.device();
    tex.impl_->allocator = ctx.allocator();
    tex.impl_->width = width;
    tex.impl_->height = height;

    if (vmaCreateImage(ctx.allocator(), &ici, &ai, &tex.impl_->image, &tex.impl_->allocation,
                       nullptr) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create texture image"});
    }

    // Transition to transfer dst
    auto result = transition_image_layout(ctx.device(), ctx.command_pool(), ctx.graphics_queue(),
                                          tex.impl_->image, VK_IMAGE_LAYOUT_UNDEFINED,
                                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (!result)
        return std::unexpected(result.error());

    // Copy staging buffer to image
    auto cmd = vk::begin_one_shot(ctx.device(), ctx.command_pool());
    if (!cmd)
        return std::unexpected(cmd.error());

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(*cmd, staging->handle(), tex.impl_->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    auto end_result =
        vk::end_one_shot(ctx.device(), ctx.command_pool(), ctx.graphics_queue(), *cmd);
    if (!end_result)
        return std::unexpected(end_result.error());

    // Transition to shader read
    result = transition_image_layout(ctx.device(), ctx.command_pool(), ctx.graphics_queue(),
                                     tex.impl_->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!result)
        return std::unexpected(result.error());

    // Create image view
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = tex.impl_->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_SRGB;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;

    if (vkCreateImageView(ctx.device(), &vi, nullptr, &tex.impl_->view) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create texture image view"});
    }

    // Create nearest-neighbor sampler
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(ctx.device(), &si, nullptr, &tex.impl_->sampler) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create texture sampler"});
    }

    return tex;
}

std::expected<Texture, Error> Texture::load(vk::Context& ctx, const std::filesystem::path& path) {
    int w, h, channels;
    auto* pixels = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        return std::unexpected(Error{"Failed to load image: " + path.string()});
    }

    auto result =
        create_from_pixels(ctx, pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    stbi_image_free(pixels);

    if (result) {
        log(LogLevel::Info, "Texture loaded: " + path.string() + " (" + std::to_string(w) + "x" +
                                std::to_string(h) + ")");
    }
    return result;
}

std::expected<Texture, Error> Texture::load_from_memory(vk::Context& ctx, const uint8_t* data,
                                                        size_t size) {
    int w, h, channels;
    auto* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
    if (!pixels) {
        return std::unexpected(Error{"Failed to load image from memory"});
    }

    auto result =
        create_from_pixels(ctx, pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    stbi_image_free(pixels);
    return result;
}

std::expected<Texture, Error> Texture::create_empty(vk::Context& ctx, uint32_t width,
                                                    uint32_t height, VkFormat format,
                                                    VkImageUsageFlags usage) {
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {width, height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | usage;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;

    Texture tex;
    tex.impl_ = std::make_unique<Impl>();
    tex.impl_->device = ctx.device();
    tex.impl_->allocator = ctx.allocator();
    tex.impl_->width = width;
    tex.impl_->height = height;

    if (vmaCreateImage(ctx.allocator(), &ici, &ai, &tex.impl_->image, &tex.impl_->allocation,
                       nullptr) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create empty texture"});
    }

    // Create image view
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = tex.impl_->image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;

    if (vkCreateImageView(ctx.device(), &vi, nullptr, &tex.impl_->view) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create empty texture image view"});
    }

    // Nearest-neighbor sampler
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(ctx.device(), &si, nullptr, &tex.impl_->sampler) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create empty texture sampler"});
    }

    log(LogLevel::Info,
        "Empty texture created: " + std::to_string(width) + "x" + std::to_string(height));
    return tex;
}

Texture::~Texture() = default;
Texture::Texture(Texture&&) noexcept = default;
Texture& Texture::operator=(Texture&&) noexcept = default;

uint32_t Texture::width() const {
    return impl_->width;
}
uint32_t Texture::height() const {
    return impl_->height;
}
VkImage Texture::image() const {
    return impl_->image;
}
VkImageView Texture::image_view() const {
    return impl_->view;
}
VkSampler Texture::sampler() const {
    return impl_->sampler;
}

} // namespace xebble
