#include "render/vk_internal.hpp"

#include "game_paths.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>

namespace {

// the per-draw ring and the largest block a draw binds (MultiMatrixPlDraw is 1552 bytes)
constexpr VkDeviceSize draw_ring_size = 8u << 20;
constexpr VkDeviceSize draw_block_range = 2048;
// set 1 binding 1: a frame's billboards (0x988 of 48 bytes a view, with room to spare)
constexpr VkDeviceSize storage_size = 1u << 20;

// how a program's vertices are laid out
enum class Layout : std::uint8_t { mesh, fighter, screen_quad, screen_ff, none };

struct ProgramInfo {
    const char* vertex;
    const char* fragment; // nullptr: no fragment stage
    Layout layout;
    bool spec0; // constant_id 0: UNLIT, DOF_XVU, SHADOW_QUAD
    bool spec1; // constant_id 1: ALPHA_FROM_VERTEX, DEPTH_FROM_DEFAULT_TEXCOORD
};

// by Program, then by ScreenProgram from screen_program_base (docs/shaders.md)
const ProgramInfo* program_info(int program) {
    static const ProgramInfo programs[] = {
        {"ff_mesh.vert", "ff_mesh.frag", Layout::mesh, false, false},
        {"fog.vert", "ff_mesh.frag", Layout::mesh, false, false},
        {"point.vert", "point.frag", Layout::mesh, false, false},
        {"point.vert", "point.frag", Layout::mesh, false, true},
        {"point.vert", "point.frag", Layout::mesh, true, false},
        {"point.vert", "point.frag", Layout::mesh, true, true},
        {"bump.vert", "bump.frag", Layout::mesh, false, false},
        {"reflect.vert", "reflect.frag", Layout::mesh, false, false},
        {"seabump.vert", "seabump.frag", Layout::mesh, false, false},
        {"multi_matrix_pl.vert", "multi_matrix_pl.frag", Layout::fighter, false, false},
        {"multi_shadow.vert", nullptr, Layout::fighter, false, false},
        {"ff_mesh.vert", nullptr, Layout::mesh, false, false},
        {"screen_ff.vert", "screen_ff.frag", Layout::screen_ff, true, false},
        {"particle.vert", "premultiplied.frag", Layout::none, false, false},
        {"multi_optical.vert", "blend.frag", Layout::fighter, false, false},
        {"rainbow.vert", "rainbow.frag", Layout::mesh, false, false},
        {"effopt.vert", "effopt.frag", Layout::mesh, false, false},
        {"cockpit_uv.vert", "ff_mesh.frag", Layout::mesh, false, false},
    };
    static const ProgramInfo screens[] = {
        {"screen_quad.vert", "blur.frag", Layout::screen_quad, false, false},
        {"screen_quad.vert", "dof.frag", Layout::screen_quad, true, false},
        {"screen_quad.vert", "dofblur.frag", Layout::screen_quad, true, true},
        {"screen_ff.vert", "screen_ff.frag", Layout::screen_ff, false, false},
        {"screen_ff.vert", "gamma.frag", Layout::screen_ff, false, false},
        {"screen_ff.vert", "post.frag", Layout::screen_ff, false, false},
    };
    static_assert(std::size(programs) == static_cast<std::size_t>(Program::count));
    static_assert(std::size(screens) == static_cast<std::size_t>(ScreenProgram::count));
    if (program >= 0 && program < static_cast<int>(Program::count)) {
        return &programs[program];
    }
    const int screen = program - screen_program_base;
    return screen >= 0 && screen < static_cast<int>(ScreenProgram::count) ? &screens[screen]
                                                                          : nullptr;
}

std::uint64_t key_bits(const PipelineKey& key) {
    return static_cast<std::uint64_t>(key.program) | static_cast<std::uint64_t>(key.blend) << 8 |
           static_cast<std::uint64_t>(key.depth_test) << 12 |
           static_cast<std::uint64_t>(key.depth_write) << 13 |
           static_cast<std::uint64_t>(key.cull) << 16 |
           static_cast<std::uint64_t>(key.stencil) << 20 |
           static_cast<std::uint64_t>(key.pass) << 24;
}

VkDescriptorSetLayout make_layout(RendererState& vk, const VkDescriptorSetLayoutBinding* bindings,
                                  std::uint32_t count) {
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = count;
    layout.pBindings = bindings;
    VkDescriptorSetLayout out = VK_NULL_HANDLE;
    vk_check(vkCreateDescriptorSetLayout(vk.device, &layout, nullptr, &out), "set layout");
    return out;
}

// stencil faces for a setup: front and back
void stencil_faces(Stencil stencil, VkStencilOpState& front, VkStencilOpState& back) {
    VkStencilOpState op{};
    op.failOp = op.passOp = op.depthFailOp = VK_STENCIL_OP_KEEP;
    op.compareOp = VK_COMPARE_OP_ALWAYS;
    op.compareMask = op.writeMask = 0xFF;
    op.reference = 0;
    front = back = op;
    switch (stencil) {
    case Stencil::zero_on_pass:
        front.passOp = back.passOp = VK_STENCIL_OP_ZERO;
        break;
    case Stencil::volume:
        front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
        back.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
        break;
    case Stencil::volume_inside:
        front.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
        back.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
        break;
    case Stencil::quad:
        // D3D LEQUAL against ref 1: 1 <= the count; pass REPLACE
        front.compareOp = back.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        front.reference = back.reference = 1;
        front.passOp = back.passOp = VK_STENCIL_OP_REPLACE;
        break;
    case Stencil::off:
        break;
    }
}

} // namespace

VkShaderModule vk_shader(RendererState& vk, const std::string& name) {
    const auto found = vk.shaders.find(name);
    if (found != vk.shaders.end()) {
        return found->second;
    }
    const std::string path = game_path(PHANTOMCRASH_SHADER_DIR) + "/" + name + ".spv";
    std::ifstream in(path, std::ios::binary);
    const std::vector<char> code{std::istreambuf_iterator<char>(in),
                                 std::istreambuf_iterator<char>()};
    VkShaderModule module = VK_NULL_HANDLE;
    if (code.empty() || code.size() % 4) {
        SDL_SetError("can't read the shader %s", path.c_str());
    } else {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
        vk_check(vkCreateShaderModule(vk.device, &info, nullptr, &module), "shader module");
    }
    vk.shaders[name] = module;
    return module;
}

bool vk_create_shading(RendererState& vk) {
    // set 0: Frame; set 1: the per-draw block (dynamic offset) and the storage buffer; set 2: the
    // four texture stages (docs/shaders.md, "Binding layout proposal")
    const VkShaderStageFlags both = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkDescriptorSetLayoutBinding frame_binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, both,
                                                     nullptr};
    const VkDescriptorSetLayoutBinding draw_bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, both, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}};
    VkDescriptorSetLayoutBinding texture_bindings[4];
    for (std::uint32_t s = 0; s < 4; ++s) {
        texture_bindings[s] = {s, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                               VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    }
    vk.frame_layout = make_layout(vk, &frame_binding, 1);
    vk.draw_layout = make_layout(vk, draw_bindings, 2);
    vk.texture_layout = make_layout(vk, texture_bindings, 4);
    if (!vk.frame_layout || !vk.draw_layout || !vk.texture_layout) {
        return false;
    }
    VkDescriptorSetLayout sets[3] = {vk.frame_layout, vk.draw_layout, vk.texture_layout};
    VkPushConstantRange push{both, 0, sizeof(DrawState)};
    VkPipelineLayoutCreateInfo pipeline_layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout.setLayoutCount = 3;
    pipeline_layout.pSetLayouts = sets;
    pipeline_layout.pushConstantRangeCount = 1;
    pipeline_layout.pPushConstantRanges = &push;
    if (!vk_check(vkCreatePipelineLayout(vk.device, &pipeline_layout, nullptr, &vk.pipeline_layout),
                  "pipeline layout")) {
        return false;
    }

    VkDescriptorPoolSize sizes[4] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
                                     {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4},
                                     {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
                                     {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * 8192}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 8192 + 8;
    pool.poolSizeCount = 4;
    pool.pPoolSizes = sizes;
    return vk_check(vkCreateDescriptorPool(vk.device, &pool, nullptr, &vk.descriptor_pool),
                    "descriptor pool") &&
           vk_create_mapped_buffer(vk, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, vk.frame_uniform) &&
           vk_create_mapped_buffer(vk, draw_ring_size + draw_block_range,
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, vk.draw_uniforms) &&
           vk_create_mapped_buffer(vk, storage_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vk.storage) &&
           vk_create_mapped_buffer(vk, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vk.bone_zero);
}

void vk_reset_descriptors(RendererState& vk) {
    vkResetDescriptorPool(vk.device, vk.descriptor_pool, 0);
    vk.texture_sets.clear();
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = vk.descriptor_pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &vk.frame_layout;
    vk_check(vkAllocateDescriptorSets(vk.device, &allocate, &vk.frame_set), "frame set");
    allocate.pSetLayouts = &vk.draw_layout;
    vk_check(vkAllocateDescriptorSets(vk.device, &allocate, &vk.draw_set), "draw set");
    VkDescriptorBufferInfo buffers[3] = {{vk.frame_uniform.buffer, 0, 64},
                                         {vk.draw_uniforms.buffer, 0, draw_block_range},
                                         {vk.storage.buffer, 0, storage_size}};
    VkWriteDescriptorSet writes[3]{};
    for (VkWriteDescriptorSet& write : writes) {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
    }
    writes[0].dstSet = vk.frame_set;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &buffers[0];
    writes[1].dstSet = vk.draw_set;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[1].pBufferInfo = &buffers[1];
    writes[2].dstSet = vk.draw_set;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo = &buffers[2];
    vkUpdateDescriptorSets(vk.device, 3, writes, 0, nullptr);
}

void vk_destroy_shading(RendererState& vk) {
    for (auto& [key, pipeline] : vk.pipelines) {
        vkDestroyPipeline(vk.device, pipeline, nullptr);
    }
    vk.pipelines.clear();
    for (auto& [key, sampler] : vk.samplers) {
        vkDestroySampler(vk.device, sampler, nullptr);
    }
    vk.samplers.clear();
    for (auto& [name, module] : vk.shaders) {
        if (module) {
            vkDestroyShaderModule(vk.device, module, nullptr);
        }
    }
    vk.shaders.clear();
    for (GpuBuffer* buffer : {&vk.frame_uniform, &vk.draw_uniforms, &vk.storage, &vk.bone_zero,
                              &vk.screen_quads, &vk.staging, &vk.probe}) {
        vk_destroy_buffer(vk, *buffer);
    }
    vk_destroy_image(vk, vk.wave);
    vk_destroy_image(vk, vk.ramp);
    if (vk.descriptor_pool) {
        vkDestroyDescriptorPool(vk.device, vk.descriptor_pool, nullptr);
    }
    if (vk.pipeline_layout) {
        vkDestroyPipelineLayout(vk.device, vk.pipeline_layout, nullptr);
    }
    for (VkDescriptorSetLayout layout : {vk.frame_layout, vk.draw_layout, vk.texture_layout}) {
        if (layout) {
            vkDestroyDescriptorSetLayout(vk.device, layout, nullptr);
        }
    }
    for (VkRenderPass pass : {vk.scene_pass, vk.screen_pass, vk.late_pass, vk.resume_pass}) {
        if (pass) {
            vkDestroyRenderPass(vk.device, pass, nullptr);
        }
    }
}

VkPipeline vk_pipeline_for(RendererState& vk, const PipelineKey& key) {
    const std::uint64_t bits = key_bits(key);
    const auto found = vk.pipelines.find(bits);
    if (found != vk.pipelines.end()) {
        return found->second;
    }
    const ProgramInfo* info = program_info(key.program);
    if (!info) {
        return VK_NULL_HANDLE;
    }

    // stages, with both specialization constants offered to both (a shader ignores ids it lacks)
    const VkBool32 spec_values[2] = {info->spec0, info->spec1};
    const VkSpecializationMapEntry spec_entries[2] = {{0, 0, 4}, {1, 4, 4}};
    VkSpecializationInfo spec{2, spec_entries, sizeof spec_values, spec_values};
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vk_shader(vk, info->vertex);
    stages[0].pName = "main";
    stages[0].pSpecializationInfo = &spec;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = info->fragment ? vk_shader(vk, info->fragment) : VK_NULL_HANDLE;
    stages[1].pName = "main";
    stages[1].pSpecializationInfo = &spec;
    if (!stages[0].module || (info->fragment && !stages[1].module)) {
        return VK_NULL_HANDLE;
    }

    // vertex layouts (docs/shaders.md, "Vertex formats")
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    switch (info->layout) {
    case Layout::mesh:
    case Layout::fighter:
        bindings.push_back({0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX});
        attributes.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position)});
        attributes.push_back({1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)});
        attributes.push_back({2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(MeshVertex, colour)});
        attributes.push_back({3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv)});
        if (info->layout == Layout::fighter) {
            // every section is drawn with its own bone 0 (src/render/mesh_draw.cpp)
            bindings.push_back({1, 4, VK_VERTEX_INPUT_RATE_INSTANCE});
            attributes.push_back({4, 1, VK_FORMAT_R32_SFLOAT, 0});
        }
        break;
    case Layout::screen_quad:
        bindings.push_back({0, 48, VK_VERTEX_INPUT_RATE_VERTEX});
        attributes.push_back({0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0});
        for (std::uint32_t uv = 0; uv < 4; ++uv) {
            attributes.push_back({1 + uv, 0, VK_FORMAT_R32G32_SFLOAT, 16 + uv * 8});
        }
        break;
    case Layout::screen_ff:
        bindings.push_back({0, 28, VK_VERTEX_INPUT_RATE_VERTEX});
        attributes.push_back({0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0});
        attributes.push_back({2, 0, VK_FORMAT_R8G8B8A8_UNORM, 16});
        attributes.push_back({3, 0, VK_FORMAT_R32G32_SFLOAT, 20});
        break;
    case Layout::none:
        break;
    }
    VkPipelineVertexInputStateCreateInfo input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
    input.pVertexBindingDescriptions = bindings.data();
    input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    input.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = info->layout == Layout::mesh || info->layout == Layout::fighter ||
                                info->layout == Layout::none
                            ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                            : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    // D3D clip space through a negative viewport height: the original's clockwise front faces stay
    // clockwise on screen, so CULL_CCW is VK_CULL_MODE_BACK_BIT
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = key.cull == Cull::back    ? VK_CULL_MODE_BACK_BIT
                      : key.cull == Cull::front ? VK_CULL_MODE_FRONT_BIT
                                                : VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = key.depth_test ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = key.depth_write ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth.stencilTestEnable = key.stencil != Stencil::off ? VK_TRUE : VK_FALSE;
    stencil_faces(key.stencil, depth.front, depth.back);
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = key.blend == Blend::no_colour
                               ? 0
                               : VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (key.blend == Blend::alpha || key.blend == Blend::add) {
        // D3D8 blends alpha with the colour's factors
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = blend.dstAlphaBlendFactor =
            key.blend == Blend::add ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
    } else if (key.blend == Blend::premultiplied) {
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blending{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo create{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    create.stageCount = info->fragment ? 2 : 1;
    create.pStages = stages;
    create.pVertexInputState = &input;
    create.pInputAssemblyState = &assembly;
    create.pViewportState = &viewport;
    create.pRasterizationState = &raster;
    create.pMultisampleState = &multisample;
    create.pDepthStencilState = &depth;
    create.pColorBlendState = &blending;
    create.pDynamicState = &dynamic;
    create.layout = vk.pipeline_layout;
    create.renderPass = key.pass == PassKind::scene ? vk.scene_pass : vk.screen_pass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!vk_check(
            vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &create, nullptr, &pipeline),
            "pipeline")) {
        return VK_NULL_HANDLE;
    }
    vk.pipelines[bits] = pipeline;
    return pipeline;
}
