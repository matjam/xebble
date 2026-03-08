#include "swapchain.hpp"

#include "context.hpp"

#include <xebble/log.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <limits>

namespace xebble::vk {

struct Swapchain::Impl {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    uint32_t graphics_family = 0;
    uint32_t present_family = 0;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {0, 0};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;

    ~Impl() {
        destroy_views();
        if (swapchain)
            vkDestroySwapchainKHR(device, swapchain, nullptr);
    }

    void destroy_views() {
        for (auto view : views) {
            vkDestroyImageView(device, view, nullptr);
        }
        views.clear();
        images.clear();
    }

    std::expected<void, Error> create_swapchain(GLFWwindow* window, bool vsync) {
        // Query surface capabilities
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &caps);

        // Choose format
        uint32_t format_count;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count,
                                             formats.data());

        VkSurfaceFormatKHR chosen_format = formats[0];
        for (auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen_format = f;
                break;
            }
        }
        format = chosen_format.format;

        // Choose present mode
        uint32_t mode_count;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, nullptr);
        std::vector<VkPresentModeKHR> modes(mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count,
                                                  modes.data());

        VkPresentModeKHR chosen_mode = VK_PRESENT_MODE_FIFO_KHR;
        if (!vsync) {
            for (auto m : modes) {
                if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
                    chosen_mode = m;
                    break;
                }
            }
        }

        // Choose extent
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            extent = caps.currentExtent;
        } else {
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            extent.width = std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width,
                                      caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height,
                                       caps.maxImageExtent.height);
        }

        uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
            image_count = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = surface;
        ci.minImageCount = image_count;
        ci.imageFormat = format;
        ci.imageColorSpace = chosen_format.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t families[] = {graphics_family, present_family};
        if (graphics_family != present_family) {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = families;
        } else {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = chosen_mode;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = swapchain;

        VkSwapchainKHR new_swapchain;
        if (vkCreateSwapchainKHR(device, &ci, nullptr, &new_swapchain) != VK_SUCCESS) {
            return std::unexpected(Error{"Failed to create swapchain"});
        }

        // Destroy old
        if (swapchain) {
            destroy_views();
            vkDestroySwapchainKHR(device, swapchain, nullptr);
        }
        swapchain = new_swapchain;

        // Get images
        uint32_t count;
        vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
        images.resize(count);
        vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());

        // Create image views
        views.resize(count);
        for (uint32_t i = 0; i < count; i++) {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = images[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = format;
            vi.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.baseMipLevel = 0;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.baseArrayLayer = 0;
            vi.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &vi, nullptr, &views[i]) != VK_SUCCESS) {
                return std::unexpected(Error{"Failed to create swapchain image view"});
            }
        }

        // Log on creation and on any size change; suppress duplicates during
        // window animations where many same-size recreates fire in one frame.
        if (extent.width != this->extent.width || extent.height != this->extent.height) {
            log(LogLevel::Info, "Swapchain created: " + std::to_string(extent.width) + "x" +
                                    std::to_string(extent.height) + ", " + std::to_string(count) +
                                    " images");
            this->extent = extent;
        }

        return {};
    }
};

std::expected<Swapchain, Error> Swapchain::create(const Context& ctx, GLFWwindow* window,
                                                  bool vsync) {
    Swapchain sc;
    sc.impl_ = std::make_unique<Impl>();
    sc.impl_->device = ctx.device();
    sc.impl_->physical_device = ctx.physical_device();
    sc.impl_->surface = ctx.surface();
    sc.impl_->graphics_family = ctx.graphics_queue_family();
    sc.impl_->present_queue = ctx.present_queue();
    sc.impl_->present_family = ctx.present_queue_family();

    auto result = sc.impl_->create_swapchain(window, vsync);
    if (!result)
        return std::unexpected(result.error());

    return sc;
}

Swapchain::~Swapchain() = default;
Swapchain::Swapchain(Swapchain&&) noexcept = default;
Swapchain& Swapchain::operator=(Swapchain&&) noexcept = default;

std::expected<uint32_t, Error> Swapchain::acquire_next_image(VkSemaphore signal_semaphore) {
    uint32_t index;
    VkResult result = vkAcquireNextImageKHR(impl_->device, impl_->swapchain, UINT64_MAX,
                                            signal_semaphore, VK_NULL_HANDLE, &index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return std::unexpected(Error{"Swapchain out of date"});
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return std::unexpected(Error{"Failed to acquire swapchain image"});
    }
    return index;
}

VkResult Swapchain::present(uint32_t image_index, VkSemaphore wait_semaphore) {
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &wait_semaphore;
    pi.swapchainCount = 1;
    pi.pSwapchains = &impl_->swapchain;
    pi.pImageIndices = &image_index;
    return vkQueuePresentKHR(impl_->present_queue, &pi);
}

std::expected<void, Error> Swapchain::recreate(GLFWwindow* window, bool vsync) {
    // Wait for minimized windows
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(window, &w, &h);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(impl_->device);
    return impl_->create_swapchain(window, vsync);
}

VkSwapchainKHR Swapchain::handle() const {
    return impl_->swapchain;
}
VkFormat Swapchain::image_format() const {
    return impl_->format;
}
VkExtent2D Swapchain::extent() const {
    return impl_->extent;
}
const std::vector<VkImageView>& Swapchain::image_views() const {
    return impl_->views;
}
uint32_t Swapchain::image_count() const {
    return static_cast<uint32_t>(impl_->images.size());
}

} // namespace xebble::vk
