/// @file command.hpp
/// @brief Command buffer helpers for one-shot and per-frame operations.
///
/// Provides begin_one_shot / end_one_shot for immediate GPU work (e.g.
/// texture uploads via staging buffers), and allocate_command_buffers
/// for per-frame rendering.
#pragma once

#include <xebble/types.hpp>

#include <vulkan/vulkan.h>

#include <expected>
#include <vector>

namespace xebble::vk {

/// @brief Begin a one-shot command buffer for immediate GPU work.
/// @param device Vulkan logical device.
/// @param command_pool Pool to allocate from.
/// @return The begun command buffer, or an Error.
std::expected<VkCommandBuffer, Error> begin_one_shot(VkDevice device, VkCommandPool command_pool);

/// @brief End, submit, and wait for a one-shot command buffer, then free it.
/// @param device Vulkan logical device.
/// @param command_pool Pool the buffer was allocated from.
/// @param queue Queue to submit to.
/// @param cmd The command buffer (must have been begun with begin_one_shot).
std::expected<void, Error> end_one_shot(VkDevice device, VkCommandPool command_pool, VkQueue queue,
                                        VkCommandBuffer cmd);

/// @brief Allocate multiple command buffers from a pool.
/// @param device Vulkan logical device.
/// @param command_pool Pool to allocate from.
/// @param count Number of command buffers to allocate.
std::expected<std::vector<VkCommandBuffer>, Error>
allocate_command_buffers(VkDevice device, VkCommandPool command_pool, uint32_t count);

} // namespace xebble::vk
