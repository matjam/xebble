/// @file context.hpp
/// @brief Vulkan context owning instance, device, queues, surface, and VMA allocator.
///
/// The Context is the root Vulkan object. It creates a VkInstance (with
/// validation layers in debug builds), selects a physical device, creates a
/// logical device with graphics and present queues, initializes VMA, and
/// creates a command pool. All handles are destroyed in reverse order on
/// destruction. Move-only.
#pragma once

#include <xebble/types.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <expected>
#include <memory>

struct GLFWwindow;

namespace xebble::vk {

/// @brief Owns the core Vulkan state: instance, device, queues, allocator.
///
/// Created from a GLFWwindow (needed for surface creation). Prefers discrete
/// GPUs. On macOS, enables MoltenVK portability extensions automatically.
class Context {
public:
    /// @brief Create the Vulkan context from an existing GLFW window.
    /// @param window The GLFW window to create a Vulkan surface for.
    /// @return The context, or an Error if Vulkan initialization failed.
    static std::expected<Context, Error> create(GLFWwindow* window);

    ~Context();
    Context(Context&&) noexcept;
    Context& operator=(Context&&) noexcept;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    VkInstance instance() const;
    VkPhysicalDevice physical_device() const;
    VkDevice device() const;
    VkQueue graphics_queue() const;
    VkQueue present_queue() const;
    uint32_t graphics_queue_family() const;
    uint32_t present_queue_family() const;
    VkSurfaceKHR surface() const;
    VmaAllocator allocator() const;
    VkCommandPool command_pool() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Context() = default;
};

} // namespace xebble::vk
