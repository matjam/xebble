#include "buffer.hpp"
#include <cstring>

namespace xebble::vk {

struct Buffer::Impl {
    VmaAllocator  allocator  = VK_NULL_HANDLE;
    VkBuffer      buffer     = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize  size       = 0;
    void*         mapped     = nullptr; ///< Persistently mapped pointer (non-null for host-visible buffers).

    ~Impl() {
        if (buffer && allocator) {
            if (mapped) {
                vmaUnmapMemory(allocator, allocation);
                mapped = nullptr;
            }
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
    ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size        = size;
    ci.usage       = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = memory_usage;
    ai.flags = flags;

    Buffer buf;
    buf.impl_            = std::make_unique<Impl>();
    buf.impl_->allocator = allocator;
    buf.impl_->size      = size;

    if (vmaCreateBuffer(allocator, &ci, &ai,
            &buf.impl_->buffer, &buf.impl_->allocation, nullptr) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create buffer"});
    }

    // Persistently map host-visible allocations so upload() never needs to
    // map/unmap per call.  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    // guarantees the allocation is in host-visible memory.
    if (flags & VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT) {
        if (vmaMapMemory(allocator, buf.impl_->allocation, &buf.impl_->mapped) != VK_SUCCESS) {
            return std::unexpected(Error{"Failed to map buffer memory"});
        }
    }

    return buf;
}

Buffer::~Buffer() = default;
Buffer::Buffer(Buffer&&) noexcept = default;
Buffer& Buffer::operator=(Buffer&&) noexcept = default;

void Buffer::upload(const void* data, VkDeviceSize size) {
    std::memcpy(impl_->mapped, data, static_cast<size_t>(size));
    // Flush to make the write visible to the GPU on non-coherent heaps.
    // On coherent memory (e.g. Apple Silicon unified memory) this is a no-op.
    vmaFlushAllocation(impl_->allocator, impl_->allocation, 0, size);
}

void* Buffer::mapped_ptr() const { return impl_->mapped; }

void Buffer::flush(VkDeviceSize offset, VkDeviceSize size) {
    vmaFlushAllocation(impl_->allocator, impl_->allocation, offset, size);
}

VkBuffer     Buffer::handle() const { return impl_->buffer; }
VkDeviceSize Buffer::size()   const { return impl_->size; }

} // namespace xebble::vk
