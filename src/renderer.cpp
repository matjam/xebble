#include <xebble/renderer.hpp>
#include <xebble/window.hpp>
#include <xebble/log.hpp>

#include "vulkan/context.hpp"
#include "vulkan/swapchain.hpp"
#include "vulkan/buffer.hpp"
#include "vulkan/descriptor.hpp"
#include "vulkan/command.hpp"
#include "vulkan/pipeline.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace xebble {

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
static constexpr uint32_t MAX_INSTANCES_PER_BATCH = 65536;

/// @brief A batch of sprite instances sharing the same texture.
struct DrawBatch {
    const Texture* texture = nullptr;
    float z_order = 0.0f;
    std::vector<SpriteInstance> instances;
};

struct Renderer::Impl {
    Window* window = nullptr;
    RendererConfig config;

    // Vulkan core (optional because they need explicit init)
    std::optional<vk::Context> context;
    std::optional<vk::Swapchain> swapchain;

    // Offscreen framebuffer
    std::optional<Texture> offscreen_texture;
    VkRenderPass offscreen_render_pass = VK_NULL_HANDLE;
    VkFramebuffer offscreen_framebuffer = VK_NULL_HANDLE;

    // Swapchain framebuffers
    VkRenderPass swapchain_render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchain_framebuffers;

    // Pipelines
    std::optional<vk::Pipeline> sprite_pipeline;
    std::optional<vk::Pipeline> blit_pipeline;

    // Descriptors
    VkDescriptorSetLayout texture_layout = VK_NULL_HANDLE;
    std::optional<vk::DescriptorPool> descriptor_pool;

    // Offscreen descriptor set (for blit)
    VkDescriptorSet offscreen_descriptor = VK_NULL_HANDLE;

    // Per-frame sync
    std::vector<VkCommandBuffer> command_buffers;
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;

    // Per-frame instance buffers
    std::vector<vk::Buffer> instance_buffers;

    // Draw state
    uint32_t current_frame = 0;
    uint32_t current_image_index = 0;
    std::vector<DrawBatch> batches;

    // Texture descriptor cache
    std::unordered_map<VkImageView, VkDescriptorSet> texture_descriptors;

    // Timing
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_time;
    Clock::time_point last_frame_time;
    float delta_time_ = 0.0f;
    float elapsed_time_ = 0.0f;
    uint64_t frame_count_ = 0;

    // Border color
    Color border_color{0, 0, 0, 255};

    ~Impl() {
        if (!context || !context->device()) return;
        vkDeviceWaitIdle(context->device());

        for (auto fb : swapchain_framebuffers)
            vkDestroyFramebuffer(context->device(), fb, nullptr);
        if (offscreen_framebuffer)
            vkDestroyFramebuffer(context->device(), offscreen_framebuffer, nullptr);
        if (offscreen_render_pass)
            vkDestroyRenderPass(context->device(), offscreen_render_pass, nullptr);
        if (swapchain_render_pass)
            vkDestroyRenderPass(context->device(), swapchain_render_pass, nullptr);
        if (texture_layout)
            vkDestroyDescriptorSetLayout(context->device(), texture_layout, nullptr);

        for (auto sem : image_available_semaphores)
            vkDestroySemaphore(context->device(), sem, nullptr);
        for (auto sem : render_finished_semaphores)
            vkDestroySemaphore(context->device(), sem, nullptr);
        for (auto fence : in_flight_fences)
            vkDestroyFence(context->device(), fence, nullptr);
    }

    VkDescriptorSet get_or_create_descriptor(const Texture& tex) {
        auto it = texture_descriptors.find(tex.image_view());
        if (it != texture_descriptors.end()) return it->second;

        auto set = descriptor_pool->allocate(texture_layout);
        if (!set) {
            log(LogLevel::Error, "Failed to allocate texture descriptor: " + set.error().message);
            return VK_NULL_HANDLE;
        }
        vk::write_texture_descriptor(context->device(), *set, tex.image_view(), tex.sampler());
        texture_descriptors[tex.image_view()] = *set;
        return *set;
    }

    std::expected<void, Error> create_render_passes() {
        // Offscreen render pass (virtual resolution)
        {
            VkAttachmentDescription attach{};
            attach.format = VK_FORMAT_R8G8B8A8_SRGB;
            attach.samples = VK_SAMPLE_COUNT_1_BIT;
            attach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attach.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkAttachmentReference ref{};
            ref.attachment = 0;
            ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &ref;

            VkSubpassDependency dep{};
            dep.srcSubpass = VK_SUBPASS_EXTERNAL;
            dep.dstSubpass = 0;
            dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            ci.attachmentCount = 1;
            ci.pAttachments = &attach;
            ci.subpassCount = 1;
            ci.pSubpasses = &subpass;
            ci.dependencyCount = 1;
            ci.pDependencies = &dep;

            if (vkCreateRenderPass(context->device(), &ci, nullptr, &offscreen_render_pass) != VK_SUCCESS) {
                return std::unexpected(Error{"Failed to create offscreen render pass"});
            }
        }

        // Swapchain render pass
        {
            VkAttachmentDescription attach{};
            attach.format = swapchain->image_format();
            attach.samples = VK_SAMPLE_COUNT_1_BIT;
            attach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attach.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attach.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference ref{};
            ref.attachment = 0;
            ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &ref;

            VkSubpassDependency dep{};
            dep.srcSubpass = VK_SUBPASS_EXTERNAL;
            dep.dstSubpass = 0;
            dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep.srcAccessMask = 0;
            dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            ci.attachmentCount = 1;
            ci.pAttachments = &attach;
            ci.subpassCount = 1;
            ci.pSubpasses = &subpass;
            ci.dependencyCount = 1;
            ci.pDependencies = &dep;

            if (vkCreateRenderPass(context->device(), &ci, nullptr, &swapchain_render_pass) != VK_SUCCESS) {
                return std::unexpected(Error{"Failed to create swapchain render pass"});
            }
        }

        return {};
    }

    std::expected<void, Error> create_offscreen_framebuffer() {
        VkImageView view = offscreen_texture->image_view();
        VkFramebufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass = offscreen_render_pass;
        ci.attachmentCount = 1;
        ci.pAttachments = &view;
        ci.width = config.virtual_width;
        ci.height = config.virtual_height;
        ci.layers = 1;

        if (vkCreateFramebuffer(context->device(), &ci, nullptr, &offscreen_framebuffer) != VK_SUCCESS) {
            return std::unexpected(Error{"Failed to create offscreen framebuffer"});
        }
        return {};
    }

    std::expected<void, Error> create_swapchain_framebuffers() {
        for (auto fb : swapchain_framebuffers)
            vkDestroyFramebuffer(context->device(), fb, nullptr);

        swapchain_framebuffers.resize(swapchain->image_count());
        for (uint32_t i = 0; i < swapchain->image_count(); i++) {
            VkImageView view = swapchain->image_views()[i];
            VkFramebufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass = swapchain_render_pass;
            ci.attachmentCount = 1;
            ci.pAttachments = &view;
            ci.width = swapchain->extent().width;
            ci.height = swapchain->extent().height;
            ci.layers = 1;

            if (vkCreateFramebuffer(context->device(), &ci, nullptr, &swapchain_framebuffers[i]) != VK_SUCCESS) {
                return std::unexpected(Error{"Failed to create swapchain framebuffer"});
            }
        }
        return {};
    }

    std::expected<void, Error> create_sync_objects() {
        image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
        render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
        in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateSemaphore(context->device(), &sci, nullptr, &image_available_semaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(context->device(), &sci, nullptr, &render_finished_semaphores[i]) != VK_SUCCESS ||
                vkCreateFence(context->device(), &fci, nullptr, &in_flight_fences[i]) != VK_SUCCESS) {
                return std::unexpected(Error{"Failed to create sync objects"});
            }
        }
        return {};
    }
};

std::expected<Renderer, Error> Renderer::create(Window& window, const RendererConfig& config) {
    Renderer renderer;
    renderer.impl_ = std::make_unique<Impl>();
    auto& impl = *renderer.impl_;
    impl.window = &window;
    impl.config = config;

    // Create Vulkan context
    auto ctx_result = vk::Context::create(window.native_handle());
    if (!ctx_result) return std::unexpected(ctx_result.error());
    impl.context.emplace(std::move(*ctx_result));

    // Create swapchain
    auto sc = vk::Swapchain::create(*impl.context, window.native_handle(), config.vsync);
    if (!sc) return std::unexpected(sc.error());
    impl.swapchain.emplace(std::move(*sc));

    // Create offscreen texture (render target)
    auto offscreen = Texture::create_empty(*impl.context, config.virtual_width, config.virtual_height,
        VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    if (!offscreen) return std::unexpected(offscreen.error());
    impl.offscreen_texture.emplace(std::move(*offscreen));

    // Create render passes
    auto rp_result = impl.create_render_passes();
    if (!rp_result) return std::unexpected(rp_result.error());

    // Create framebuffers
    auto ofb_result = impl.create_offscreen_framebuffer();
    if (!ofb_result) return std::unexpected(ofb_result.error());
    auto sfb_result = impl.create_swapchain_framebuffers();
    if (!sfb_result) return std::unexpected(sfb_result.error());

    // Create descriptor set layout
    auto layout = vk::create_single_texture_layout(impl.context->device());
    if (!layout) return std::unexpected(layout.error());
    impl.texture_layout = *layout;

    // Create descriptor pool (enough for many textures + offscreen)
    auto pool = vk::DescriptorPool::create(impl.context->device(), 256,
        {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256}});
    if (!pool) return std::unexpected(pool.error());
    impl.descriptor_pool.emplace(std::move(*pool));

    // Create offscreen descriptor for blit
    impl.offscreen_descriptor = impl.get_or_create_descriptor(*impl.offscreen_texture);

    // Find shader directory: .app bundle > relative to exe > build paths
    auto shader_dir = std::filesystem::path();
    {
        std::vector<std::filesystem::path> search_paths;
#ifdef __APPLE__
        char exe_buf[4096];
        uint32_t exe_size = sizeof(exe_buf);
        if (_NSGetExecutablePath(exe_buf, &exe_size) == 0) {
            auto exe_dir = std::filesystem::path(exe_buf).parent_path();
            search_paths.push_back(exe_dir / "../Resources/shaders");  // .app bundle
            search_paths.push_back(exe_dir / "shaders");               // next to exe
        }
#endif
        search_paths.push_back("build/debug/shaders");
        search_paths.push_back("build/release/shaders");
        search_paths.push_back("shaders");

        for (auto& p : search_paths) {
            if (std::filesystem::exists(p / "sprite.vert.spv")) {
                shader_dir = p;
                break;
            }
        }
        if (shader_dir.empty()) {
            return std::unexpected(Error{"Could not find shader directory"});
        }
    }

    // Create pipelines
    auto sprite_pip = vk::Pipeline::create_sprite_pipeline(
        impl.context->device(), impl.offscreen_render_pass, impl.texture_layout,
        shader_dir / "sprite.vert.spv", shader_dir / "sprite.frag.spv");
    if (!sprite_pip) return std::unexpected(sprite_pip.error());
    impl.sprite_pipeline.emplace(std::move(*sprite_pip));

    auto blit_pip = vk::Pipeline::create_blit_pipeline(
        impl.context->device(), impl.swapchain_render_pass, impl.texture_layout,
        shader_dir / "blit.vert.spv", shader_dir / "blit.frag.spv");
    if (!blit_pip) return std::unexpected(blit_pip.error());
    impl.blit_pipeline.emplace(std::move(*blit_pip));

    // Allocate command buffers
    auto cmds = vk::allocate_command_buffers(
        impl.context->device(), impl.context->command_pool(), MAX_FRAMES_IN_FLIGHT);
    if (!cmds) return std::unexpected(cmds.error());
    impl.command_buffers = std::move(*cmds);

    // Create sync objects
    auto sync_result = impl.create_sync_objects();
    if (!sync_result) return std::unexpected(sync_result.error());

    // Create per-frame instance buffers
    impl.instance_buffers.reserve(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        auto buf = vk::Buffer::create(
            impl.context->allocator(),
            sizeof(SpriteInstance) * MAX_INSTANCES_PER_BATCH,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        if (!buf) return std::unexpected(buf.error());
        impl.instance_buffers.push_back(std::move(*buf));
    }

    // Init timing
    impl.start_time = Impl::Clock::now();
    impl.last_frame_time = impl.start_time;

    log(LogLevel::Info, "Renderer initialized (" +
        std::to_string(config.virtual_width) + "x" + std::to_string(config.virtual_height) + ")");

    return renderer;
}

Renderer::~Renderer() = default;
Renderer::Renderer(Renderer&&) noexcept = default;
Renderer& Renderer::operator=(Renderer&&) noexcept = default;

bool Renderer::begin_frame() {
    auto& impl = *impl_;

    // Update timing
    auto now = Impl::Clock::now();
    impl.delta_time_ = std::chrono::duration<float>(now - impl.last_frame_time).count();
    impl.elapsed_time_ = std::chrono::duration<float>(now - impl.start_time).count();
    impl.last_frame_time = now;
    impl.frame_count_++;

    // Wait for previous frame with this index
    vkWaitForFences(impl.context->device(), 1, &impl.in_flight_fences[impl.current_frame],
        VK_TRUE, UINT64_MAX);

    // Acquire swapchain image
    auto image_result = impl.swapchain->acquire_next_image(
        impl.image_available_semaphores[impl.current_frame]);
    if (!image_result) {
        // Swapchain out of date — recreate
        auto recreate = impl.swapchain->recreate(impl.window->native_handle(), impl.config.vsync);
        if (!recreate) return false;
        impl.create_swapchain_framebuffers();
        return false; // Skip this frame
    }
    impl.current_image_index = *image_result;

    vkResetFences(impl.context->device(), 1, &impl.in_flight_fences[impl.current_frame]);

    // Reset and begin command buffer
    auto cmd = impl.command_buffers[impl.current_frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    // Begin offscreen render pass
    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = impl.offscreen_render_pass;
    rpi.framebuffer = impl.offscreen_framebuffer;
    rpi.renderArea = {{0, 0}, {impl.config.virtual_width, impl.config.virtual_height}};
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    rpi.clearValueCount = 1;
    rpi.pClearValues = &clear;

    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor for virtual resolution
    VkViewport viewport{};
    viewport.width = static_cast<float>(impl.config.virtual_width);
    viewport.height = static_cast<float>(impl.config.virtual_height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, {impl.config.virtual_width, impl.config.virtual_height}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind sprite pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.sprite_pipeline->handle());

    // Push orthographic projection
    auto projection = glm::ortho(0.0f, static_cast<float>(impl.config.virtual_width),
                                  static_cast<float>(impl.config.virtual_height), 0.0f,
                                  -1.0f, 1.0f);
    vkCmdPushConstants(cmd, impl.sprite_pipeline->layout(),
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &projection);

    // Clear batches for this frame
    impl.batches.clear();

    return true;
}

void Renderer::submit_instances(std::span<const SpriteInstance> instances,
                                 const Texture& texture, float z_order)
{
    auto& impl = *impl_;
    DrawBatch batch;
    batch.texture = &texture;
    batch.z_order = z_order;
    batch.instances.assign(instances.begin(), instances.end());
    impl.batches.push_back(std::move(batch));
}

void Renderer::end_frame() {
    auto& impl = *impl_;
    auto cmd = impl.command_buffers[impl.current_frame];

    // Sort batches by z_order
    std::sort(impl.batches.begin(), impl.batches.end(),
        [](const DrawBatch& a, const DrawBatch& b) { return a.z_order < b.z_order; });

    // Collect all instances into the frame's instance buffer, issuing draw calls per batch
    auto& instance_buffer = impl.instance_buffers[impl.current_frame];
    uint32_t total_offset = 0;

    // First pass: upload all instance data
    std::vector<SpriteInstance> all_instances;
    for (auto& batch : impl.batches) {
        all_instances.insert(all_instances.end(), batch.instances.begin(), batch.instances.end());
    }

    if (!all_instances.empty()) {
        VkDeviceSize upload_size = all_instances.size() * sizeof(SpriteInstance);
        if (upload_size > instance_buffer.size()) {
            upload_size = instance_buffer.size(); // Clamp
        }
        instance_buffer.upload(all_instances.data(), upload_size);
    }

    // Second pass: draw calls
    VkBuffer vb = instance_buffer.handle();
    VkDeviceSize zero_offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero_offset);

    for (auto& batch : impl.batches) {
        if (batch.instances.empty()) continue;

        auto desc = impl.get_or_create_descriptor(*batch.texture);
        if (desc == VK_NULL_HANDLE) continue;

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            impl.sprite_pipeline->layout(), 0, 1, &desc, 0, nullptr);

        uint32_t instance_count = static_cast<uint32_t>(batch.instances.size());
        vkCmdDraw(cmd, 6, instance_count, 0, total_offset);
        total_offset += instance_count;
    }

    // End offscreen render pass
    vkCmdEndRenderPass(cmd);

    // --- Blit pass: render offscreen to swapchain ---
    VkRenderPassBeginInfo blit_rpi{};
    blit_rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    blit_rpi.renderPass = impl.swapchain_render_pass;
    blit_rpi.framebuffer = impl.swapchain_framebuffers[impl.current_image_index];
    blit_rpi.renderArea = {{0, 0}, impl.swapchain->extent()};

    VkClearValue blit_clear{};
    blit_clear.color = {{
        impl.border_color.r / 255.0f,
        impl.border_color.g / 255.0f,
        impl.border_color.b / 255.0f,
        1.0f}};
    blit_rpi.clearValueCount = 1;
    blit_rpi.pClearValues = &blit_clear;

    vkCmdBeginRenderPass(cmd, &blit_rpi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.blit_pipeline->handle());

    // Calculate letterboxed viewport
    float sw = static_cast<float>(impl.swapchain->extent().width);
    float sh = static_cast<float>(impl.swapchain->extent().height);
    float vw = static_cast<float>(impl.config.virtual_width);
    float vh = static_cast<float>(impl.config.virtual_height);

    float scale = std::min(sw / vw, sh / vh);
    if (impl.config.nearest_filter) {
        scale = std::floor(scale);
        if (scale < 1.0f) scale = 1.0f;
    }
    float scaled_w = vw * scale;
    float scaled_h = vh * scale;
    float offset_x = (sw - scaled_w) / 2.0f;
    float offset_y = (sh - scaled_h) / 2.0f;

    VkViewport blit_viewport{};
    blit_viewport.x = offset_x;
    blit_viewport.y = offset_y;
    blit_viewport.width = scaled_w;
    blit_viewport.height = scaled_h;
    blit_viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &blit_viewport);

    VkRect2D blit_scissor{{0, 0}, impl.swapchain->extent()};
    vkCmdSetScissor(cmd, 0, 1, &blit_scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        impl.blit_pipeline->layout(), 0, 1, &impl.offscreen_descriptor, 0, nullptr);

    vkCmdDraw(cmd, 3, 1, 0, 0); // Fullscreen triangle

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // Submit
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &impl.image_available_semaphores[impl.current_frame];
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &impl.render_finished_semaphores[impl.current_frame];

    vkQueueSubmit(impl.context->graphics_queue(), 1, &si, impl.in_flight_fences[impl.current_frame]);

    // Present
    auto present_result = impl.swapchain->present(
        impl.current_image_index, impl.render_finished_semaphores[impl.current_frame]);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
        impl.swapchain->recreate(impl.window->native_handle(), impl.config.vsync);
        impl.create_swapchain_framebuffers();
    }

    impl.current_frame = (impl.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::set_border_color(Color color) { impl_->border_color = color; }
float Renderer::delta_time() const { return impl_->delta_time_; }
float Renderer::elapsed_time() const { return impl_->elapsed_time_; }
uint64_t Renderer::frame_count() const { return impl_->frame_count_; }
uint32_t Renderer::virtual_width() const { return impl_->config.virtual_width; }
uint32_t Renderer::virtual_height() const { return impl_->config.virtual_height; }
vk::Context& Renderer::context() { return *impl_->context; }

} // namespace xebble
