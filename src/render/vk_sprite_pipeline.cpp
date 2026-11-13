// The sprite layer's pipelines (vk_sprite_state.hpp): the overlay's vertex layout, no cull or
// depth, premultiplied ONE / INV_SRC_ALPHA blending, one pipeline for the scene pass and one for
// the screen passes, the final target's loading pass, and the vertex buffer.
#include "render/vk_sprite_state.hpp"

#include <cstddef>

bool vk_sprite_pipelines_create(RendererState& vk, SpriteState& s) {
    const VkShaderModule vertex_shader = vk_shader(vk, "sprite.vert");
    const VkShaderModule fragment_shader = vk_shader(vk, "sprite.frag");
    if (!vertex_shader || !fragment_shader) {
        return false;
    }
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    // the renderer's four-stage texture sets; the sprite reads stage 0
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &vk.texture_layout;
    if (!vk_check(vkCreatePipelineLayout(vk.device, &layout, nullptr, &s.layout),
                  "sprite layout")) {
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_shader;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_shader;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binding{0, sizeof(SpriteVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[6] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SpriteVertex, position)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SpriteVertex, uv)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SpriteVertex, colour)},
        {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(SpriteVertex, k)},
        {4, 0, VK_FORMAT_R32_SFLOAT, offsetof(SpriteVertex, blur)},
        {5, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SpriteVertex, period)},
    };
    VkPipelineVertexInputStateCreateInfo input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 6;
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    // cull off, z off (pass_open_overlay)
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    // SRC = ONE, DEST = INV_SRC_ALPHA: the shader premultiplies
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo blending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blending;
    info.pDynamicState = &dynamic;
    info.layout = s.layout;
    // one for the scene pass (colour and depth-stencil), one for the screen passes (colour)
    info.renderPass = vk.scene_pass;
    if (!vk_check(vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &info, nullptr,
                                            &s.scene_pipeline),
                  "sprite pipeline")) {
        return false;
    }
    info.renderPass = vk.screen_pass;
    if (!vk_check(vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &info, nullptr,
                                            &s.final_pipeline),
                  "sprite pipeline")) {
        return false;
    }

    // the final target after the composite: kept, drawn over, read by the gamma pass
    VkAttachmentDescription attachment{};
    attachment.format = vk.final.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference colour{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colour;
    // the screen pass's dependencies, so its pipelines and the final target's framebuffer fit
    VkSubpassDependency dependencies[2];
    vk_pass_dependencies(dependencies);
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 1;
    pass.pAttachments = &attachment;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 2;
    pass.pDependencies = dependencies;
    if (!vk_check(vkCreateRenderPass(vk.device, &pass, nullptr, &s.load_pass),
                  "sprite pass")) {
        return false;
    }
    return vk_create_mapped_buffer(vk, sprite_max_quads * 6 * sizeof(SpriteVertex),
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, s.vertices);
}
