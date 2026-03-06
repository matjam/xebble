#include "command.hpp"

namespace xebble::vk {

std::expected<VkCommandBuffer, Error> begin_one_shot(
    VkDevice device, VkCommandPool command_pool)
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = command_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(device, &ai, &cmd) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to allocate one-shot command buffer"});
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, command_pool, 1, &cmd);
        return std::unexpected(Error{"Failed to begin one-shot command buffer"});
    }

    return cmd;
}

std::expected<void, Error> end_one_shot(
    VkDevice device, VkCommandPool command_pool,
    VkQueue queue, VkCommandBuffer cmd)
{
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, command_pool, 1, &cmd);
        return std::unexpected(Error{"Failed to end one-shot command buffer"});
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, command_pool, 1, &cmd);
    return {};
}

std::expected<std::vector<VkCommandBuffer>, Error> allocate_command_buffers(
    VkDevice device, VkCommandPool command_pool, uint32_t count)
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = command_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = count;

    std::vector<VkCommandBuffer> buffers(count);
    if (vkAllocateCommandBuffers(device, &ai, buffers.data()) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to allocate command buffers"});
    }
    return buffers;
}

} // namespace xebble::vk
