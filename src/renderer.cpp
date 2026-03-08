#include "vulkan/buffer.hpp"
#include "vulkan/command.hpp"
#include "vulkan/context.hpp"
#include "vulkan/descriptor.hpp"
#include "vulkan/pipeline.hpp"
#include "vulkan/swapchain.hpp"

#include <xebble/log.hpp>
#include <xebble/renderer.hpp>
#include <xebble/window.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace xebble {

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
static constexpr uint32_t INITIAL_INSTANCE_CAPACITY = 65536;

/// @brief Metadata for a batch of sprite instances sharing the same texture.
/// Instance data lives in Impl::staging_instances; this struct just records
/// the range [first_instance, first_instance + instance_count).
struct DrawBatch {
    const Texture* texture = nullptr;
    float z_order = 0.0f;
    uint32_t first_instance = 0;
    uint32_t instance_count = 0;
};

struct Renderer::Impl {
    Window* window = nullptr;
    RendererConfig config;

    // Vulkan core (optional because they need explicit init)
    std::optional<vk::Context> context;
    std::optional<vk::Swapchain> swapchain;

    // Offscreen framebuffers — one per frame in flight so that frame N's blit
    // (which samples the offscreen texture) cannot race with frame N+1's
    // offscreen render pass (which writes to it).
    std::vector<std::optional<Texture>> offscreen_textures;
    VkRenderPass offscreen_render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> offscreen_framebuffers;

    // Swapchain framebuffers
    VkRenderPass swapchain_render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchain_framebuffers;

    // Pipelines
    std::optional<vk::Pipeline> sprite_pipeline;
    std::optional<vk::Pipeline> blit_pipeline;

    // Descriptors
    VkDescriptorSetLayout texture_layout = VK_NULL_HANDLE;
    std::optional<vk::DescriptorPool> descriptor_pool;

    // Offscreen descriptor sets (for blit) — one per frame in flight.
    std::vector<VkDescriptorSet> offscreen_descriptors;

    // Per-frame sync
    std::vector<VkCommandBuffer> command_buffers;
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;

    // Per-swapchain-image: which frame-slot fence last targeted this image.
    // Prevents a second frame slot from writing to a swapchain image that is
    // still being rendered to by the other frame slot.
    std::vector<VkFence> images_in_flight;

    // Per-frame instance buffers (grow on demand)
    std::vector<vk::Buffer> instance_buffers;
    uint32_t instance_capacity = INITIAL_INSTANCE_CAPACITY; // current capacity per buffer

    // Draw state
    uint32_t current_frame = 0;
    uint32_t current_image_index = 0;
    std::vector<DrawBatch> batches;         // direct-write batches (absolute offsets)
    std::vector<DrawBatch> staging_batches; // submit_instances batches (staging-relative offsets)
    std::vector<SpriteInstance> staging_instances; // flat buffer for submit_instances() path
    uint32_t direct_write_count_ = 0; // non-zero when map_instance_buffer() was used this frame

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

    // Blit transform (for screen_to_virtual)
    float blit_scale = 1.0f;
    float blit_offset_x = 0.0f;
    float blit_offset_y = 0.0f;

    // Pending virtual resolution change (applied at next begin_frame)
    std::optional<std::pair<uint32_t, uint32_t>> pending_virtual_resolution;

    // ---------------------------------------------------------------------------
    // update_auto_filter()
    //
    // Called after every swapchain resize (and after virtual resolution changes)
    // when auto_filter is enabled.  Checks whether the blit from the virtual
    // framebuffer to the current swapchain extent is integer-exact on both axes
    // (i.e. sw % vw == 0 && sh % vh == 0 && sw/vw == sh/vh for Fit).  If so,
    // nearest-neighbour filtering is used; otherwise bilinear.
    //
    // The filter is baked into the offscreen texture's VkSampler, so a sampler
    // change requires an offscreen recreate.  We only queue one when the filter
    // actually changes to avoid unnecessary GPU stalls.
    // ---------------------------------------------------------------------------
    void update_auto_filter() {
        if (!config.auto_filter) {
            return;
        }
        if (!swapchain) {
            return;
        }

        const uint32_t sw = swapchain->extent().width;
        const uint32_t sh = swapchain->extent().height;
        const uint32_t vw = config.virtual_width;
        const uint32_t vh = config.virtual_height;

        bool want_nearest = false;
        if (sw > 0 && sh > 0 && vw > 0 && vh > 0 && sw >= vw && sh >= vh) {
            // Pixel-perfect (for Fit) when both axes divide evenly at the fit
            // scale.  sx == sy means no bars; sx != sy means letterbox/pillarbox
            // bars, which are still pixel-perfect — every virtual pixel maps to
            // exactly min(sx,sy) × min(sx,sy) physical pixels.
            want_nearest = (sw % vw == 0 && sh % vh == 0);
        }

        if (config.nearest_sample == want_nearest) {
            return; // Already correct — no GPU work needed.
        }

        log(LogLevel::Info, std::format("auto_filter: blit {}x{} -> {}x{}  =>  filter={}", vw, vh,
                                        sw, sh, want_nearest ? "nearest" : "bilinear"));

        config.nearest_sample = want_nearest;
        // Queue offscreen recreate to rebake the sampler.
        pending_virtual_resolution = {vw, vh};
    }

    ~Impl() {
        if (!context || !context->device())
            return;
        vkDeviceWaitIdle(context->device());

        for (auto fb : swapchain_framebuffers)
            vkDestroyFramebuffer(context->device(), fb, nullptr);
        for (auto fb : offscreen_framebuffers)
            if (fb)
                vkDestroyFramebuffer(context->device(), fb, nullptr);
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
        if (it != texture_descriptors.end())
            return it->second;

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

            // Two dependencies:
            //
            // dep[0] — entry: wait for the previous frame's blit pass fragment
            //   shader to finish reading the texture before we start writing to
            //   it again as a colour attachment.
            //
            // dep[1] — exit: ensure the colour attachment write and the
            //   implicit SHADER_READ_ONLY_OPTIMAL layout transition are both
            //   complete and visible to the blit pass fragment shader that
            //   samples this texture in the very next render pass.  Without
            //   this dependency the blit can start sampling before the
            //   transition is done, producing garbage rectangles.
            VkSubpassDependency deps[2]{};

            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass = 0;
            deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[0].dependencyFlags = 0;

            deps[1].srcSubpass = 0;
            deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[1].dependencyFlags = 0;

            VkRenderPassCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            ci.attachmentCount = 1;
            ci.pAttachments = &attach;
            ci.subpassCount = 1;
            ci.pSubpasses = &subpass;
            ci.dependencyCount = 2;
            ci.pDependencies = deps;

            if (vkCreateRenderPass(context->device(), &ci, nullptr, &offscreen_render_pass) !=
                VK_SUCCESS) {
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

            if (vkCreateRenderPass(context->device(), &ci, nullptr, &swapchain_render_pass) !=
                VK_SUCCESS) {
                return std::unexpected(Error{"Failed to create swapchain render pass"});
            }
        }

        return {};
    }

    std::expected<void, Error> create_offscreen_framebuffers() {
        offscreen_textures.resize(MAX_FRAMES_IN_FLIGHT);
        offscreen_framebuffers.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
        offscreen_descriptors.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            auto tex =
                Texture::create_empty(*context, config.virtual_width, config.virtual_height,
                                      VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                      config.nearest_sample ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
            if (!tex)
                return std::unexpected(tex.error());
            offscreen_textures[i].emplace(std::move(*tex));

            VkImageView view = offscreen_textures[i]->image_view();
            VkFramebufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass = offscreen_render_pass;
            ci.attachmentCount = 1;
            ci.pAttachments = &view;
            ci.width = config.virtual_width;
            ci.height = config.virtual_height;
            ci.layers = 1;

            if (vkCreateFramebuffer(context->device(), &ci, nullptr, &offscreen_framebuffers[i]) !=
                VK_SUCCESS) {
                return std::unexpected(Error{"Failed to create offscreen framebuffer"});
            }
        }
        return {};
    }

    std::expected<void, Error> create_swapchain_framebuffers() {
        for (auto fb : swapchain_framebuffers)
            vkDestroyFramebuffer(context->device(), fb, nullptr);

        swapchain_framebuffers.resize(swapchain->image_count());
        images_in_flight.assign(swapchain->image_count(), VK_NULL_HANDLE);
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

            if (vkCreateFramebuffer(context->device(), &ci, nullptr, &swapchain_framebuffers[i]) !=
                VK_SUCCESS) {
                return std::unexpected(Error{"Failed to create swapchain framebuffer"});
            }
        }
        return {};
    }

    std::expected<void, Error> recreate_offscreen(uint32_t new_w, uint32_t new_h) {
        vkDeviceWaitIdle(context->device());

        // Destroy old offscreen resources for all frame slots
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (i < offscreen_framebuffers.size() && offscreen_framebuffers[i]) {
                vkDestroyFramebuffer(context->device(), offscreen_framebuffers[i], nullptr);
                offscreen_framebuffers[i] = VK_NULL_HANDLE;
            }
            if (i < offscreen_textures.size() && offscreen_textures[i]) {
                auto it = texture_descriptors.find(offscreen_textures[i]->image_view());
                if (it != texture_descriptors.end())
                    texture_descriptors.erase(it);
                offscreen_textures[i].reset();
            }
        }

        // Update config
        config.virtual_width = new_w;
        config.virtual_height = new_h;

        // Recreate all per-frame offscreen resources
        auto result = create_offscreen_framebuffers();
        if (!result)
            return result;

        // Create blit descriptors for each frame's offscreen texture
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            offscreen_descriptors[i] = get_or_create_descriptor(*offscreen_textures[i]);
        }

        log(LogLevel::Info,
            "Virtual resolution changed to " + std::to_string(new_w) + "x" + std::to_string(new_h));
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
            if (vkCreateSemaphore(context->device(), &sci, nullptr,
                                  &image_available_semaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(context->device(), &sci, nullptr,
                                  &render_finished_semaphores[i]) != VK_SUCCESS ||
                vkCreateFence(context->device(), &fci, nullptr, &in_flight_fences[i]) !=
                    VK_SUCCESS) {
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
    if (!ctx_result)
        return std::unexpected(ctx_result.error());
    impl.context.emplace(std::move(*ctx_result));

    // Create swapchain
    auto sc = vk::Swapchain::create(*impl.context, window.native_handle(), config.vsync);
    if (!sc)
        return std::unexpected(sc.error());
    impl.swapchain.emplace(std::move(*sc));

    // Create render passes
    auto rp_result = impl.create_render_passes();
    if (!rp_result)
        return std::unexpected(rp_result.error());

    // Create per-frame offscreen textures + framebuffers
    auto ofb_result = impl.create_offscreen_framebuffers();
    if (!ofb_result)
        return std::unexpected(ofb_result.error());
    auto sfb_result = impl.create_swapchain_framebuffers();
    if (!sfb_result)
        return std::unexpected(sfb_result.error());

    // Create descriptor set layout
    auto layout = vk::create_single_texture_layout(impl.context->device());
    if (!layout)
        return std::unexpected(layout.error());
    impl.texture_layout = *layout;

    // Create descriptor pool (enough for many textures + offscreen)
    auto pool = vk::DescriptorPool::create(impl.context->device(), 256,
                                           {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256}});
    if (!pool)
        return std::unexpected(pool.error());
    impl.descriptor_pool.emplace(std::move(*pool));

    // Create per-frame offscreen descriptors for blit
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        impl.offscreen_descriptors[i] = impl.get_or_create_descriptor(*impl.offscreen_textures[i]);
    }

    // Find shader directory: .app bundle > relative to exe > build paths
    auto shader_dir = std::filesystem::path();
    {
        std::vector<std::filesystem::path> search_paths;
#ifdef __APPLE__
        char exe_buf[4096];
        uint32_t exe_size = sizeof(exe_buf);
        if (_NSGetExecutablePath(exe_buf, &exe_size) == 0) {
            auto exe_dir = std::filesystem::path(exe_buf).parent_path();
            search_paths.push_back(exe_dir / "../Resources/shaders"); // .app bundle
            search_paths.push_back(exe_dir / "shaders");              // next to exe
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
    if (!sprite_pip)
        return std::unexpected(sprite_pip.error());
    impl.sprite_pipeline.emplace(std::move(*sprite_pip));

    auto blit_pip = vk::Pipeline::create_blit_pipeline(
        impl.context->device(), impl.swapchain_render_pass, impl.texture_layout,
        shader_dir / "blit.vert.spv", shader_dir / "blit.frag.spv");
    if (!blit_pip)
        return std::unexpected(blit_pip.error());
    impl.blit_pipeline.emplace(std::move(*blit_pip));

    // Allocate command buffers
    auto cmds = vk::allocate_command_buffers(impl.context->device(), impl.context->command_pool(),
                                             MAX_FRAMES_IN_FLIGHT);
    if (!cmds)
        return std::unexpected(cmds.error());
    impl.command_buffers = std::move(*cmds);

    // Create sync objects
    auto sync_result = impl.create_sync_objects();
    if (!sync_result)
        return std::unexpected(sync_result.error());

    // Create per-frame instance buffers
    impl.instance_buffers.reserve(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        auto buf = vk::Buffer::create(impl.context->allocator(),
                                      sizeof(SpriteInstance) * INITIAL_INSTANCE_CAPACITY,
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO,
                                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        if (!buf)
            return std::unexpected(buf.error());
        impl.instance_buffers.push_back(std::move(*buf));
    }

    // Init timing
    impl.start_time = Impl::Clock::now();
    impl.last_frame_time = impl.start_time;

    log(LogLevel::Info, "Renderer initialized (" + std::to_string(config.virtual_width) + "x" +
                            std::to_string(config.virtual_height) + ")");

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

    // Apply any pending virtual resolution change before touching the GPU.
    if (impl.pending_virtual_resolution.has_value()) {
        auto [new_w, new_h] = *impl.pending_virtual_resolution;
        impl.pending_virtual_resolution.reset();
        if (auto r = impl.recreate_offscreen(new_w, new_h); !r) {
            log(LogLevel::Error, "Failed to resize virtual framebuffer: " + r.error().message);
            return false;
        }
    }

    // Proactively recreate the swapchain if the window framebuffer size has
    // changed since the last frame.  On Wayland compositors (e.g. Hyprland)
    // the compositor may resize the window tile without ever returning
    // VK_ERROR_OUT_OF_DATE_KHR — it simply scales the surface.  Detecting the
    // mismatch here and recreating before acquire prevents that scaling.
    {
        auto [fw, fh] = impl.window->framebuffer_size();
        auto ext = impl.swapchain->extent();
        if (fw > 0 && fh > 0 && (fw != ext.width || fh != ext.height)) {
            impl.swapchain->recreate(impl.window->native_handle(), impl.config.vsync);
            impl.create_swapchain_framebuffers();
            impl.update_auto_filter();
        }
    }

    // Wait for previous frame with this index
    vkWaitForFences(impl.context->device(), 1, &impl.in_flight_fences[impl.current_frame], VK_TRUE,
                    UINT64_MAX);

    // Acquire swapchain image
    auto image_result =
        impl.swapchain->acquire_next_image(impl.image_available_semaphores[impl.current_frame]);
    if (!image_result) {
        // Swapchain out of date — recreate
        auto recreate = impl.swapchain->recreate(impl.window->native_handle(), impl.config.vsync);
        if (!recreate)
            return false;
        impl.create_swapchain_framebuffers();
        impl.update_auto_filter();
        return false; // Skip this frame
    }
    impl.current_image_index = *image_result;

    // If another frame slot previously submitted work targeting this swapchain
    // image, wait for that work to complete before we start writing to the
    // image again.  This prevents two different frame slots from rendering to
    // the same swapchain image concurrently.
    if (impl.images_in_flight[impl.current_image_index] != VK_NULL_HANDLE) {
        vkWaitForFences(impl.context->device(), 1, &impl.images_in_flight[impl.current_image_index],
                        VK_TRUE, UINT64_MAX);
    }
    impl.images_in_flight[impl.current_image_index] = impl.in_flight_fences[impl.current_frame];

    // Reset and begin command buffer
    // NOTE: fence is NOT reset here — it is reset just before vkQueueSubmit in
    // end_frame(), which is the last safe moment.  Resetting it here (before the
    // buffer upload that happens between begin_frame and end_frame) would allow
    // a new submission to start before the previous GPU work on this frame slot
    // has finished, causing the instance buffer write to race with GPU reads.
    auto cmd = impl.command_buffers[impl.current_frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    // Begin offscreen render pass
    VkRenderPassBeginInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpi.renderPass = impl.offscreen_render_pass;
    rpi.framebuffer = impl.offscreen_framebuffers[impl.current_frame];
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
                                 static_cast<float>(impl.config.virtual_height), 0.0f, -1.0f, 1.0f);
    vkCmdPushConstants(cmd, impl.sprite_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(glm::mat4), &projection);

    // Clear batches and staging buffer for this frame
    impl.batches.clear();
    impl.staging_batches.clear();
    impl.staging_instances.clear();
    impl.direct_write_count_ = 0;

    return true;
}

void Renderer::submit_instances(std::span<const SpriteInstance> instances, const Texture& texture,
                                float z_order) {
    auto& impl = *impl_;
    uint32_t first = static_cast<uint32_t>(impl.staging_instances.size());
    impl.staging_instances.insert(impl.staging_instances.end(), instances.begin(), instances.end());
    impl.staging_batches.push_back(
        {&texture, z_order, first, static_cast<uint32_t>(instances.size())});
}

SpriteInstance* Renderer::map_instance_buffer(uint32_t count) {
    auto& impl = *impl_;

    // Grow instance buffers if needed.
    if (count > impl.instance_capacity) {
        vkDeviceWaitIdle(impl.context->device());

        uint32_t new_cap = impl.instance_capacity;
        while (new_cap < count)
            new_cap *= 2;

        log(LogLevel::Info,
            "Growing instance buffers (direct): " + std::to_string(impl.instance_capacity) +
                " -> " + std::to_string(new_cap));

        impl.instance_buffers.clear();
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            auto buf =
                vk::Buffer::create(impl.context->allocator(), sizeof(SpriteInstance) * new_cap,
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO,
                                   VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            if (!buf) {
                log(LogLevel::Error, "Failed to grow instance buffer");
                return nullptr;
            }
            impl.instance_buffers.push_back(std::move(*buf));
        }
        impl.instance_capacity = new_cap;
    }

    return static_cast<SpriteInstance*>(impl.instance_buffers[impl.current_frame].mapped_ptr());
}

void Renderer::record_batch(const Texture& texture, float z_order, uint32_t first_instance,
                            uint32_t instance_count) {
    impl_->batches.push_back({&texture, z_order, first_instance, instance_count});
}

void Renderer::flush_instance_buffer(uint32_t total_instances) {
    auto& impl = *impl_;
    if (total_instances > 0) {
        VkDeviceSize upload_size = total_instances * sizeof(SpriteInstance);
        impl.instance_buffers[impl.current_frame].flush(0, upload_size);
        // Mark staging_instances as effectively populated so end_frame()
        // sees there are instances to draw but doesn't re-upload.
        impl.direct_write_count_ = total_instances;
    }
}

void Renderer::end_frame() {
    auto& impl = *impl_;
    auto cmd = impl.command_buffers[impl.current_frame];

    // Merge staging batches into the main batch list.
    // Direct-write batches (from record_batch) have absolute buffer offsets.
    // Staging batches (from submit_instances) have offsets relative to
    // staging_instances[0] — shift them by direct_write_count_ so they
    // don't overlap with the direct-written region.
    uint32_t direct_count = impl.direct_write_count_;
    uint32_t staging_count = static_cast<uint32_t>(impl.staging_instances.size());

    for (auto& sb : impl.staging_batches) {
        sb.first_instance += direct_count;
        impl.batches.push_back(sb);
    }

    // Sort all batch metadata by z_order (tiny array — typically < 10 entries).
    std::sort(impl.batches.begin(), impl.batches.end(),
              [](const DrawBatch& a, const DrawBatch& b) { return a.z_order < b.z_order; });

    if (staging_count > 0) {
        uint32_t total_needed = direct_count + staging_count;

        // Grow instance buffers if needed (all frames must be the same size
        // so we can't just grow one — wait for idle and rebuild all of them).
        if (total_needed > impl.instance_capacity) {
            vkDeviceWaitIdle(impl.context->device());

            uint32_t new_cap = impl.instance_capacity;
            while (new_cap < total_needed)
                new_cap *= 2;

            log(LogLevel::Info,
                "Growing instance buffers: " + std::to_string(impl.instance_capacity) + " -> " +
                    std::to_string(new_cap));

            impl.instance_buffers.clear();
            for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                auto buf =
                    vk::Buffer::create(impl.context->allocator(), sizeof(SpriteInstance) * new_cap,
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO,
                                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
                if (!buf) {
                    log(LogLevel::Error, "Failed to grow instance buffer");
                    return;
                }
                impl.instance_buffers.push_back(std::move(*buf));
            }
            impl.instance_capacity = new_cap;
        }

        // Upload staging data after the direct-written region.
        auto* dst =
            static_cast<SpriteInstance*>(impl.instance_buffers[impl.current_frame].mapped_ptr());
        std::memcpy(dst + direct_count, impl.staging_instances.data(),
                    staging_count * sizeof(SpriteInstance));

        VkDeviceSize flush_offset = direct_count * sizeof(SpriteInstance);
        VkDeviceSize flush_size = staging_count * sizeof(SpriteInstance);
        impl.instance_buffers[impl.current_frame].flush(flush_offset, flush_size);
    } else if (direct_count == 0 && staging_count == 0) {
        // No instances at all — nothing to draw. Fall through to draw calls
        // which will all be skipped (empty batches list).
    }

    // Draw calls — each batch records its own first_instance offset into the
    // staging buffer, so draw order (z-sorted) is independent of buffer layout.
    VkBuffer vb = impl.instance_buffers[impl.current_frame].handle();
    VkDeviceSize zero_offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero_offset);

    for (auto& batch : impl.batches) {
        if (batch.instance_count == 0)
            continue;

        auto desc = impl.get_or_create_descriptor(*batch.texture);
        if (desc == VK_NULL_HANDLE)
            continue;

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                impl.sprite_pipeline->layout(), 0, 1, &desc, 0, nullptr);

        vkCmdDraw(cmd, 6, batch.instance_count, 0, batch.first_instance);
    }

    // End offscreen render pass
    vkCmdEndRenderPass(cmd);

    // Explicit barrier: ensure the offscreen colour-attachment writes and the
    // layout transition to SHADER_READ_ONLY_OPTIMAL are fully complete and
    // visible to the blit pass's fragment shader.  The subpass exit dependency
    // (dep[1]) should already handle this, but MoltenVK can sometimes elide
    // subpass dependency barriers when both render passes are in the same
    // command buffer.  This explicit barrier makes the synchronisation
    // unambiguous.
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = impl.offscreen_textures[impl.current_frame]->image();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);
    }

    // --- Blit pass: render offscreen to swapchain ---
    VkRenderPassBeginInfo blit_rpi{};
    blit_rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    blit_rpi.renderPass = impl.swapchain_render_pass;
    blit_rpi.framebuffer = impl.swapchain_framebuffers[impl.current_image_index];
    blit_rpi.renderArea = {{0, 0}, impl.swapchain->extent()};

    VkClearValue blit_clear{};
    blit_clear.color = {{impl.border_color.r / 255.0f, impl.border_color.g / 255.0f,
                         impl.border_color.b / 255.0f, 1.0f}};
    blit_rpi.clearValueCount = 1;
    blit_rpi.pClearValues = &blit_clear;

    vkCmdBeginRenderPass(cmd, &blit_rpi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.blit_pipeline->handle());

    // Calculate scaled viewport and scissor.
    //
    // Fit  (ScaleMode::Fit):  scale = min(sw/vw, sh/vh)
    //   The entire virtual canvas fits inside the window; empty bars appear on
    //   the shorter axis (letterbox / pillarbox).  Scissor covers the full
    //   window so the border clear colour fills the bars.
    //
    // Crop (ScaleMode::Crop): scale = max(sw/vw, sh/vh)
    //   The canvas fills the window; the excess on the longer axis is cropped.
    //   The viewport is offset so the crop is symmetric (centred).  The scissor
    //   is clamped to the window so Vulkan never writes outside the surface.
    float sw = static_cast<float>(impl.swapchain->extent().width);
    float sh = static_cast<float>(impl.swapchain->extent().height);
    float vw = static_cast<float>(impl.config.virtual_width);
    float vh = static_cast<float>(impl.config.virtual_height);

    const bool crop = (impl.config.scale_mode == ScaleMode::Crop);
    float scale = crop ? std::max(sw / vw, sh / vh) : std::min(sw / vw, sh / vh);
    float scaled_w = vw * scale;
    float scaled_h = vh * scale;
    float offset_x = (sw - scaled_w) / 2.0f;
    float offset_y = (sh - scaled_h) / 2.0f;

    impl.blit_scale = scale;
    impl.blit_offset_x = offset_x;
    impl.blit_offset_y = offset_y;

    VkViewport blit_viewport{};
    blit_viewport.x = offset_x;
    blit_viewport.y = offset_y;
    blit_viewport.width = scaled_w;
    blit_viewport.height = scaled_h;
    blit_viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &blit_viewport);

    // Scissor: in Fit mode cover the whole window (bars get the clear colour);
    // in Crop mode clamp to the window so the overflowing viewport is clipped.
    VkRect2D blit_scissor{{0, 0}, impl.swapchain->extent()};
    vkCmdSetScissor(cmd, 0, 1, &blit_scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.blit_pipeline->layout(), 0,
                            1, &impl.offscreen_descriptors[impl.current_frame], 0, nullptr);

    vkCmdDraw(cmd, 3, 1, 0, 0); // Fullscreen triangle

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // Reset the fence here — the latest safe point before reuse.  Doing it
    // earlier (e.g. right after vkAcquireNextImageKHR) would race with the
    // instance buffer upload that happens between begin_frame and end_frame.
    vkResetFences(impl.context->device(), 1, &impl.in_flight_fences[impl.current_frame]);

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

    vkQueueSubmit(impl.context->graphics_queue(), 1, &si,
                  impl.in_flight_fences[impl.current_frame]);

    // Present
    auto present_result = impl.swapchain->present(
        impl.current_image_index, impl.render_finished_semaphores[impl.current_frame]);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
        impl.swapchain->recreate(impl.window->native_handle(), impl.config.vsync);
        impl.create_swapchain_framebuffers();
    }

    impl.current_frame = (impl.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::set_virtual_resolution(uint32_t width, uint32_t height) {
    impl_->pending_virtual_resolution = {width, height};
}

void Renderer::set_display_mode(const DisplayMode& mode) {
    impl_->window->set_display_mode(mode);
    // The window resize triggers a swapchain recreation automatically.
    // No need to touch the virtual resolution here — that is a separate concern.
}

void Renderer::set_fullscreen(bool fullscreen) {
    impl_->window->set_fullscreen(fullscreen);
    // The resulting window resize event will trigger a proactive swapchain
    // recreate in begin_frame() — no explicit action needed here.
}

void Renderer::set_vsync(bool enabled) {
    auto& impl = *impl_;
    if (impl.config.vsync == enabled) {
        return;
    }
    impl.config.vsync = enabled;
    vkDeviceWaitIdle(impl.context->device());
    impl.swapchain->recreate(impl.window->native_handle(), impl.config.vsync);
    impl.create_swapchain_framebuffers();
    impl.update_auto_filter();
    log(LogLevel::Info, std::string("vsync: ") + (enabled ? "on" : "off"));
}

void Renderer::set_nearest_sample(bool nearest) {
    auto& impl = *impl_;
    // Manual override — disable auto-filter so the renderer doesn't
    // immediately undo the caller's choice on the next resize.
    impl.config.auto_filter = false;
    if (impl.config.nearest_sample == nearest) {
        return;
    }
    impl.config.nearest_sample = nearest;
    // Queue an offscreen recreate so the new sampler filter is baked in.
    impl.pending_virtual_resolution = {impl.config.virtual_width, impl.config.virtual_height};
}

void Renderer::set_auto_filter(bool enabled) {
    auto& impl = *impl_;
    impl.config.auto_filter = enabled;
    if (enabled) {
        // Re-evaluate immediately against the current swapchain extent.
        impl.update_auto_filter();
    }
}

void Renderer::handle_resize() {
    auto& impl = *impl_;
    vkDeviceWaitIdle(impl.context->device());
    impl.swapchain->recreate(impl.window->native_handle(), impl.config.vsync);
    impl.create_swapchain_framebuffers();
    impl.update_auto_filter();
}

void Renderer::set_border_color(Color color) {
    impl_->border_color = color;
}
float Renderer::delta_time() const {
    return impl_->delta_time_;
}
float Renderer::elapsed_time() const {
    return impl_->elapsed_time_;
}
uint64_t Renderer::frame_count() const {
    return impl_->frame_count_;
}
uint32_t Renderer::virtual_width() const {
    return impl_->config.virtual_width;
}
uint32_t Renderer::virtual_height() const {
    return impl_->config.virtual_height;
}
vk::Context& Renderer::context() {
    return *impl_->context;
}

uint32_t Renderer::current_frame_index() const {
    return impl_->current_frame;
}

Vec2 Renderer::screen_to_virtual(Vec2 screen_pos) const {
    float cs = impl_->window->content_scale();
    float fx = screen_pos.x * cs;
    float fy = screen_pos.y * cs;
    if (impl_->blit_scale == 0.0f)
        return {fx, fy};
    return {(fx - impl_->blit_offset_x) / impl_->blit_scale,
            (fy - impl_->blit_offset_y) / impl_->blit_scale};
}

} // namespace xebble
