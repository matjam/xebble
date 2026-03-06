#include "buffer.hpp"
#include <cstring>

namespace xebble::vk {

struct Buffer::Impl {
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;

    ~Impl() {
        if (buffer && allocator) {
            vmaDestroyBuffer(allocator, buffer, allocation);
        }
    }
};

std::expected<Buffer, Error> Buffer::create(
    VmaAllocator allocator,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memory_usage,
    VmaAllocationCreateFlags flags)
{
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = memory_usage;
    ai.flags = flags;

    Buffer buf;
    buf.impl_ = std::make_unique<Impl>();
    buf.impl_->allocator = allocator;
    buf.impl_->size = size;

    if (vmaCreateBuffer(allocator, &ci, &ai,
            &buf.impl_->buffer, &buf.impl_->allocation, nullptr) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create buffer"});
    }

    return buf;
}

Buffer::~Buffer() = default;
Buffer::Buffer(Buffer&&) noexcept = default;
Buffer& Buffer::operator=(Buffer&&) noexcept = default;

void Buffer::upload(const void* data, VkDeviceSize size) {
    void* mapped;
    vmaMapMemory(impl_->allocator, impl_->allocation, &mapped);
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vmaUnmapMemory(impl_->allocator, impl_->allocation);
}

VkBuffer Buffer::handle() const { return impl_->buffer; }
VkDeviceSize Buffer::size() const { return impl_->size; }

} // namespace xebble::vk
