/// @file swapchain.hpp
/// @brief Vulkan swapchain management with recreation support.
///
/// Wraps VkSwapchainKHR with automatic image view creation, format/present
/// mode selection, and transparent recreation on window resize or
/// VK_ERROR_OUT_OF_DATE_KHR. Move-only RAII type.
#pragma once

#include <xebble/types.hpp>

#include <vulkan/vulkan.h>

#include <expected>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace xebble::vk {

class Context;

/// @brief Owns a Vulkan swapchain, its image views, and handles recreation.
///
/// Prefers B8G8R8A8_SRGB format. Vsync uses FIFO present mode; non-vsync
/// prefers MAILBOX with FIFO fallback.
class Swapchain {
public:
    /// @brief Create a swapchain for the given context and window.
    /// @param ctx The Vulkan context (provides device, surface, queues).
    /// @param window The GLFW window (provides framebuffer extent).
    /// @param vsync Whether to use vsync (FIFO) or prefer mailbox.
    [[nodiscard]] static std::expected<Swapchain, Error> create(const Context& ctx,
                                                                GLFWwindow* window, bool vsync);

    ~Swapchain();
    Swapchain(Swapchain&&) noexcept;
    Swapchain& operator=(Swapchain&&) noexcept;
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    /// @brief Acquire the next swapchain image. Signals the given semaphore.
    /// @return The image index, or an Error if the swapchain is out of date.
    [[nodiscard]] std::expected<uint32_t, Error> acquire_next_image(VkSemaphore signal_semaphore);

    /// @brief Present the given image. Waits on the given semaphore.
    /// @return VK_SUCCESS, VK_SUBOPTIMAL_KHR, or VK_ERROR_OUT_OF_DATE_KHR.
    VkResult present(uint32_t image_index, VkSemaphore wait_semaphore);

    /// @brief Recreate the swapchain (e.g. after window resize).
    std::expected<void, Error> recreate(GLFWwindow* window, bool vsync);

    [[nodiscard]] VkSwapchainKHR handle() const;
    [[nodiscard]] VkFormat image_format() const;
    [[nodiscard]] VkExtent2D extent() const;
    [[nodiscard]] const std::vector<VkImageView>& image_views() const;
    [[nodiscard]] uint32_t image_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Swapchain() = default;
};

} // namespace xebble::vk
