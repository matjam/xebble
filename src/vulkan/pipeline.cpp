#include "pipeline.hpp"

#include <xebble/log.hpp>

#include <glm/glm.hpp>

#include <fstream>
#include <vector>

namespace xebble::vk {

struct Pipeline::Impl {
    VkDevice device = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    ~Impl() {
        if (pipeline)
            vkDestroyPipeline(device, pipeline, nullptr);
        if (layout)
            vkDestroyPipelineLayout(device, layout, nullptr);
    }
};

Pipeline::~Pipeline() = default;
Pipeline::Pipeline(Pipeline&&) noexcept = default;
Pipeline& Pipeline::operator=(Pipeline&&) noexcept = default;
VkPipeline Pipeline::handle() const {
    return impl_->pipeline;
}
VkPipelineLayout Pipeline::layout() const {
    return impl_->layout;
}

namespace {

std::expected<std::vector<char>, Error> read_spirv(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(Error{"Failed to open shader: " + path.string()});
    }
    auto size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

std::expected<VkShaderModule, Error> create_shader_module(VkDevice device,
                                                          const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create shader module"});
    }
    return module;
}

} // anonymous namespace

/// Per-instance vertex data for the sprite pipeline.
/// Must match the layout in sprite.vert exactly (field order, types, padding).
struct SpriteInstanceData {
    glm::vec2 position;  // location 0
    glm::vec2 uv_offset; // location 1
    glm::vec2 uv_size;   // location 2
    glm::vec2 quad_size; // location 3
    glm::vec4 color;     // location 4
    float scale;         // location 5
    float rotation;      // location 6
    glm::vec2 pivot;     // location 7
};

std::expected<Pipeline, Error> Pipeline::create_sprite_pipeline(
    VkDevice device, VkRenderPass render_pass, VkDescriptorSetLayout descriptor_set_layout,
    const std::filesystem::path& vert_path, const std::filesystem::path& frag_path) {
    // Load shaders
    auto vert_code = read_spirv(vert_path);
    if (!vert_code)
        return std::unexpected(vert_code.error());
    auto frag_code = read_spirv(frag_path);
    if (!frag_code)
        return std::unexpected(frag_code.error());

    auto vert_module = create_shader_module(device, *vert_code);
    if (!vert_module)
        return std::unexpected(vert_module.error());
    auto frag_module = create_shader_module(device, *frag_code);
    if (!frag_module) {
        vkDestroyShaderModule(device, *vert_module, nullptr);
        return std::unexpected(frag_module.error());
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = *vert_module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = *frag_module;
    stages[1].pName = "main";

    // Per-instance vertex input (no per-vertex attributes — vertices generated in shader)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(SpriteInstanceData);
    binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[8]{};
    // position
    attrs[0].binding = 0;
    attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset = offsetof(SpriteInstanceData, position);
    // uv_offset
    attrs[1].binding = 0;
    attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset = offsetof(SpriteInstanceData, uv_offset);
    // uv_size
    attrs[2].binding = 0;
    attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = offsetof(SpriteInstanceData, uv_size);
    // quad_size
    attrs[3].binding = 0;
    attrs[3].location = 3;
    attrs[3].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[3].offset = offsetof(SpriteInstanceData, quad_size);
    // color
    attrs[4].binding = 0;
    attrs[4].location = 4;
    attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[4].offset = offsetof(SpriteInstanceData, color);
    // scale
    attrs[5].binding = 0;
    attrs[5].location = 5;
    attrs[5].format = VK_FORMAT_R32_SFLOAT;
    attrs[5].offset = offsetof(SpriteInstanceData, scale);
    // rotation
    attrs[6].binding = 0;
    attrs[6].location = 6;
    attrs[6].format = VK_FORMAT_R32_SFLOAT;
    attrs[6].offset = offsetof(SpriteInstanceData, rotation);
    // pivot
    attrs[7].binding = 0;
    attrs[7].location = 7;
    attrs[7].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[7].offset = offsetof(SpriteInstanceData, pivot);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 8;
    vertex_input.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Dynamic viewport and scissor
    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Alpha blending
    VkPipelineColorBlendAttachmentState blend_attach{};
    blend_attach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_attach.blendEnable = VK_TRUE;
    blend_attach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attach.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_attach.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &blend_attach;

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    // Push constant for projection matrix
    VkPushConstantRange push_constant{};
    push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant.offset = 0;
    push_constant.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount = 1;
    layout_ci.pSetLayouts = &descriptor_set_layout;
    layout_ci.pushConstantRangeCount = 1;
    layout_ci.pPushConstantRanges = &push_constant;

    Pipeline pip;
    pip.impl_ = std::make_unique<Impl>();
    pip.impl_->device = device;

    if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &pip.impl_->layout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, *vert_module, nullptr);
        vkDestroyShaderModule(device, *frag_module, nullptr);
        return std::unexpected(Error{"Failed to create sprite pipeline layout"});
    }

    VkGraphicsPipelineCreateInfo pipeline_ci{};
    pipeline_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_ci.stageCount = 2;
    pipeline_ci.pStages = stages;
    pipeline_ci.pVertexInputState = &vertex_input;
    pipeline_ci.pInputAssemblyState = &input_assembly;
    pipeline_ci.pViewportState = &viewport_state;
    pipeline_ci.pRasterizationState = &rasterizer;
    pipeline_ci.pMultisampleState = &multisampling;
    pipeline_ci.pColorBlendState = &color_blending;
    pipeline_ci.pDynamicState = &dynamic_state;
    pipeline_ci.layout = pip.impl_->layout;
    pipeline_ci.renderPass = render_pass;
    pipeline_ci.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr,
                                                &pip.impl_->pipeline);

    vkDestroyShaderModule(device, *vert_module, nullptr);
    vkDestroyShaderModule(device, *frag_module, nullptr);

    if (result != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create sprite graphics pipeline"});
    }

    log(LogLevel::Info, "Sprite pipeline created");
    return pip;
}

std::expected<Pipeline, Error> Pipeline::create_blit_pipeline(
    VkDevice device, VkRenderPass render_pass, VkDescriptorSetLayout descriptor_set_layout,
    const std::filesystem::path& vert_path, const std::filesystem::path& frag_path) {
    auto vert_code = read_spirv(vert_path);
    if (!vert_code)
        return std::unexpected(vert_code.error());
    auto frag_code = read_spirv(frag_path);
    if (!frag_code)
        return std::unexpected(frag_code.error());

    auto vert_module = create_shader_module(device, *vert_code);
    if (!vert_module)
        return std::unexpected(vert_module.error());
    auto frag_module = create_shader_module(device, *frag_code);
    if (!frag_module) {
        vkDestroyShaderModule(device, *vert_module, nullptr);
        return std::unexpected(frag_module.error());
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = *vert_module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = *frag_module;
    stages[1].pName = "main";

    // No vertex input — fullscreen triangle generated in shader
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // No blending for blit
    VkPipelineColorBlendAttachmentState blend_attach{};
    blend_attach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &blend_attach;

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    // No push constants for blit
    VkPipelineLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount = 1;
    layout_ci.pSetLayouts = &descriptor_set_layout;

    Pipeline pip;
    pip.impl_ = std::make_unique<Impl>();
    pip.impl_->device = device;

    if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &pip.impl_->layout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, *vert_module, nullptr);
        vkDestroyShaderModule(device, *frag_module, nullptr);
        return std::unexpected(Error{"Failed to create blit pipeline layout"});
    }

    VkGraphicsPipelineCreateInfo pipeline_ci{};
    pipeline_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_ci.stageCount = 2;
    pipeline_ci.pStages = stages;
    pipeline_ci.pVertexInputState = &vertex_input;
    pipeline_ci.pInputAssemblyState = &input_assembly;
    pipeline_ci.pViewportState = &viewport_state;
    pipeline_ci.pRasterizationState = &rasterizer;
    pipeline_ci.pMultisampleState = &multisampling;
    pipeline_ci.pColorBlendState = &color_blending;
    pipeline_ci.pDynamicState = &dynamic_state;
    pipeline_ci.layout = pip.impl_->layout;
    pipeline_ci.renderPass = render_pass;
    pipeline_ci.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr,
                                                &pip.impl_->pipeline);

    vkDestroyShaderModule(device, *vert_module, nullptr);
    vkDestroyShaderModule(device, *frag_module, nullptr);

    if (result != VK_SUCCESS) {
        return std::unexpected(Error{"Failed to create blit graphics pipeline"});
    }

    log(LogLevel::Info, "Blit pipeline created");
    return pip;
}

} // namespace xebble::vk
