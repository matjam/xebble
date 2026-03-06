/// @file buffer.hpp
/// @brief RAII wrapper for VkBuffer + VmaAllocation.
///
/// Provides a move-only Buffer class that owns a Vulkan buffer and its
/// VMA allocation. Supports host-visible uploads via mapped memory.
#pragma once

#include <xebble/types.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <expected>
#include <memory>

namespace xebble::vk {

/// @brief Owns a VkBuffer and its VMA allocation. Move-only.
class Buffer {
public:
    /// @brief Create a GPU buffer with the given usage and memory properties.
    /// @param allocator VMA allocator to allocate from.
    /// @param size Buffer size in bytes.
    /// @param usage Vulkan buffer usage flags (e.g. VK_BUFFER_USAGE_VERTEX_BUFFER_BIT).
    /// @param memory_usage VMA memory usage hint (e.g. VMA_MEMORY_USAGE_AUTO).
    /// @param flags VMA allocation create flags (e.g. VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT).
    static std::expected<Buffer, Error> create(
        VmaAllocator allocator,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VmaMemoryUsage memory_usage,
        VmaAllocationCreateFlags flags = 0);

    ~Buffer();
    Buffer(Buffer&&) noexcept;
    Buffer& operator=(Buffer&&) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    /// @brief Upload data to the buffer via persistent mapping.
    /// @param data Pointer to source data.
    /// @param size Number of bytes to copy.
    void upload(const void* data, VkDeviceSize size);

    VkBuffer handle() const;
    VkDeviceSize size() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Buffer() = default;
};

} // namespace xebble::vk
