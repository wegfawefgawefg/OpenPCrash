#include "render/vk_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

// The pass 11 composite (pass_open_effects): screen_effect_dof in battle, which
// downsamples the scene twice and blends it with the blur by depth (dof.frag, or dofblur.frag with
// the heat haze), else fullscreen_quad times scene_tint; then the port's own passes: the
// post pass over the 3D before the 2D (FXAA, brightness; only when asked for) and the gamma ramp
// over everything.

namespace {

// ScreenDraw (include/screen_draw.glsl)
struct ScreenDraw {
    float texel_scale[4][4];
    float c0[4], c1[4], c2[4];
    float bump_env[4];
};

// screen_ff.vert's vertex: x, y, z, rhw in the original's 640x480 pixels, colour, uv
struct ScreenVertex {
    float position[4];
    std::uint8_t colour[4];
    float uv[2];
};

// where each quad sits in screen_quads
constexpr VkDeviceSize quad_dofblur = 0;     // 0x25B500
constexpr VkDeviceSize quad_blur320 = 192;   // 0x25B500 + 0xC0
constexpr VkDeviceSize quad_blur160 = 384;   // 0x25B500 + 0x180
constexpr VkDeviceSize quad_dof = 576;       // 0x25B440
constexpr VkDeviceSize quad_copy = 768;      // fullscreen_quad's corners
constexpr VkDeviceSize quad_shadow = 768 + 4 * sizeof(ScreenVertex); // shadow_screen_quad's
constexpr VkDeviceSize quad_gamma = 768 + 8 * sizeof(ScreenVertex);  // the port's gamma pass
constexpr VkDeviceSize quads_size = 768 + 12 * sizeof(ScreenVertex);

// the original's screen-effect quads, 0x30 a vertex: clip position, four uvs in the texels of
// what each stage samples (D_0025B500, D_0025B440), quad-list order
constexpr float dofblur_quad[48] = {
    -1.003125f, -1.0f,       0, 1, 0, 1, 0,      120, 0,   480, 0,   480,
    -1.003125f, 1.0041667f,  0, 1, 0, 0, 0,      0,   0,   0,   0,   0,
    1.0f,       1.0041667f,  0, 1, 1, 0, 160,    0,   640, 0,   640, 0,
    1.0f,       -1.0f,       0, 1, 1, 1, 160,    120, 640, 480, 640, 480};
constexpr float blur320_quad[48] = {
    -1.003125f, -1.0f,      0, 1, 1,   481, -1,  479, -1,  481, 1,   479,
    -1.003125f, 1.0041667f, 0, 1, 1,   1,   -1,  -1,  -1,  1,   1,   -1,
    1.0f,       1.0041667f, 0, 1, 641, 1,   639, -1,  639, 1,   641, -1,
    1.0f,       -1.0f,      0, 1, 641, 481, 639, 479, 639, 481, 641, 479};
constexpr float blur160_quad[48] = {
    -1.003125f, -1.0f,      0, 1, 1,   241, -1,  239, -1,  241, 1,   239,
    -1.003125f, 1.0041667f, 0, 1, 1,   1,   -1,  -1,  -1,  1,   1,   -1,
    1.0f,       1.0041667f, 0, 1, 321, 1,   319, -1,  319, 1,   321, -1,
    1.0f,       -1.0f,      0, 1, 321, 241, 319, 239, 319, 241, 321, 239};
constexpr float dof_quad[48] = {
    -1.003125f, -1.0f,      0, 1, 0,   120, 0,   480, 0,   480, 0, 0,
    -1.003125f, 1.0041667f, 0, 1, 0,   0,   0,   0,   0,   0,   0, 0,
    1.0f,       1.0041667f, 0, 1, 160, 0,   640, 0,   640, 0,   0, 0,
    1.0f,       -1.0f,      0, 1, 160, 120, 640, 480, 640, 480, 0, 0};

// a quad list's four corners as a triangle strip
void put_quad(std::uint8_t* to, const float* quad) {
    const int order[4] = {0, 1, 3, 2};
    for (int i = 0; i < 4; ++i) {
        std::memcpy(to + i * 48, quad + order[i] * 12, 48);
    }
}

void put_screen_vertex(std::uint8_t* to, float x, float y, std::uint32_t argb, float u, float v) {
    ScreenVertex vertex{{x, y, 0, 1},
                        {static_cast<std::uint8_t>(argb >> 16), static_cast<std::uint8_t>(argb >> 8),
                         static_cast<std::uint8_t>(argb), static_cast<std::uint8_t>(argb >> 24)},
                        {u, v}};
    std::memcpy(to, &vertex, sizeof vertex);
}

// ARGB to rgba 0..1
void unpack(std::uint32_t argb, float* out) {
    out[0] = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
    out[1] = static_cast<float>((argb >> 8) & 0xFF) / 255.0f;
    out[2] = static_cast<float>(argb & 0xFF) / 255.0f;
    out[3] = static_cast<float>(argb >> 24) / 255.0f;
}

// a combiner constant as the runtime packs it, 8 bits a channel
float q8(float value) {
    return std::floor(std::fmin(std::fmax(value, 0.0f), 1.0f) * 255.0f + 0.5f) / 255.0f;
}

std::uint32_t put_block(RendererState& vk, std::uint32_t& cursor, const ScreenDraw& block) {
    const std::uint32_t offset = cursor;
    std::memcpy(static_cast<std::uint8_t*>(vk.draw_uniforms.mapped) + offset, &block, sizeof block);
    const std::uint32_t align = static_cast<std::uint32_t>(vk.uniform_alignment);
    cursor = (offset + static_cast<std::uint32_t>(sizeof block) + align - 1) / align * align;
    return offset;
}

void set_scale(ScreenDraw& block, int stage, float width, float height) {
    block.texel_scale[stage][0] = 1.0f / width;
    block.texel_scale[stage][1] = 1.0f / height;
}

// one screen draw: a target's pass, a program, four stages, a block, a quad
void screen_draw(RendererState& vk, VkCommandBuffer command, VkFramebuffer framebuffer,
                 const GpuImage& target, ScreenProgram program, const StageTexture* stages,
                 const DrawState& state, std::uint32_t& cursor, const ScreenDraw& block,
                 VkDeviceSize quad) {
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = vk.screen_pass;
    begin.framebuffer = framebuffer;
    begin.renderArea.extent = {static_cast<std::uint32_t>(target.width),
                               static_cast<std::uint32_t>(target.height)};
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    // D3D's clip space: y up, through a negative height
    const VkViewport viewport{0, static_cast<float>(target.height),
                              static_cast<float>(target.width),
                              -static_cast<float>(target.height), 0, 1};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &begin.renderArea);
    PipelineKey key;
    key.program = screen_program_base + static_cast<int>(program);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_for(vk, key));
    const std::uint32_t offset = put_block(vk, cursor, block);
    VkDescriptorSet sets[3] = {vk.frame_set, vk.draw_set, vk_texture_set(vk, stages)};
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, 0, 3,
                            sets, 1, &offset);
    vkCmdPushConstants(command, vk.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof state,
                       &state);
    vkCmdBindVertexBuffers(command, 0, 1, &vk.screen_quads.buffer, &quad);
    vkCmdDraw(command, 4, 1, 0, 0);
    vkCmdEndRenderPass(command);
}

StageTexture clamped(int texture, bool point = false) {
    StageTexture stage;
    stage.texture = texture;
    stage.u = stage.v = Address::Clamp;
    stage.point = point;
    return stage;
}

} // namespace

bool vk_create_composite(RendererState& vk) {
    // the wave texture (V8U8, a signed du/dv pair) and the ramp, and the bytes they come from
    if (!vk_create_image(vk, 64, 64, 1, VK_FORMAT_R8G8_SNORM,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, vk.wave) ||
        !vk_create_image(vk, 256, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, vk.ramp) ||
        !vk_create_mapped_buffer(vk, 64 * 64 * 2 + 256 * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 vk.staging) ||
        !vk_create_mapped_buffer(vk, quads_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 vk.screen_quads)) {
        return false;
    }
    std::uint8_t* quads = static_cast<std::uint8_t*>(vk.screen_quads.mapped);
    put_quad(quads + quad_dofblur, dofblur_quad);
    put_quad(quads + quad_blur320, blur320_quad);
    put_quad(quads + quad_blur160, blur160_quad);
    put_quad(quads + quad_dof, dof_quad);
    // fullscreen_quad: (-1, 480) to (640, -1), texels 0..640 x 0..480, a strip as given
    put_screen_vertex(quads + quad_copy, -1, 480, 0xFFFFFFFF, 0, 480);
    put_screen_vertex(quads + quad_copy + 28, -1, -1, 0xFFFFFFFF, 0, 0);
    put_screen_vertex(quads + quad_copy + 56, 640, 480, 0xFFFFFFFF, 640, 480);
    put_screen_vertex(quads + quad_copy + 84, 640, -1, 0xFFFFFFFF, 640, 0);
    // the gamma pass: the whole target, Frame.pixel's half pixel taken back out
    put_screen_vertex(quads + quad_gamma, -0.5f, 479.5f, 0xFFFFFFFF, 0, 1);
    put_screen_vertex(quads + quad_gamma + 28, -0.5f, -0.5f, 0xFFFFFFFF, 0, 0);
    put_screen_vertex(quads + quad_gamma + 56, 639.5f, 479.5f, 0xFFFFFFFF, 1, 1);
    put_screen_vertex(quads + quad_gamma + 84, 639.5f, -0.5f, 0xFFFFFFFF, 1, 0);
    return true;
}

void vk_write_dynamic_textures(RendererState& vk, VkCommandBuffer command,
                               const FrameSetup& setup) {
    // wave_texture_update: row i a sine of i and the phase, fading to 0 at the bottom, as
    // du (dv 0)
    std::uint8_t* bytes = static_cast<std::uint8_t*>(vk.staging.mapped);
    for (int i = 0; i < 64; ++i) {
        const double wave = (std::sin(static_cast<float>(i) * 0.7853982f + setup.wave_phase) + 1.0) *
                            0.5 * (static_cast<float>(64 - i) * 0.015625f) * 50.0;
        const std::uint8_t du = static_cast<std::uint8_t>(static_cast<int>(wave));
        for (int j = 0; j < 64; ++j) {
            bytes[(i * 64 + j) * 2] = du;
            bytes[(i * 64 + j) * 2 + 1] = 0;
        }
    }
    std::uint8_t* ramp = bytes + 64 * 64 * 2;
    for (int i = 0; i < 256; ++i) {
        ramp[i * 4] = setup.gamma[0][i];
        ramp[i * 4 + 1] = setup.gamma[1][i];
        ramp[i * 4 + 2] = setup.gamma[2][i];
        ramp[i * 4 + 3] = 255;
    }
    // shadow_screen_quad's corners in shadow_color
    std::uint8_t* quads = static_cast<std::uint8_t*>(vk.screen_quads.mapped);
    const float corners[4][2] = {{0, 480}, {0, 0}, {640, 480}, {640, 0}};
    for (int i = 0; i < 4; ++i) {
        put_screen_vertex(quads + quad_shadow + i * 28, corners[i][0], corners[i][1],
                          setup.shadow_colour, 0, 0);
    }

    for (GpuImage* image : {&vk.wave, &vk.ramp}) {
        vk_barrier(command, image->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {64, 64, 1};
    vkCmdCopyBufferToImage(command, vk.staging.buffer, vk.wave.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    copy.bufferOffset = 64 * 64 * 2;
    copy.imageExtent = {256, 1, 1};
    vkCmdCopyBufferToImage(command, vk.staging.buffer, vk.ramp.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    for (GpuImage* image : {&vk.wave, &vk.ramp}) {
        vk_barrier(command, image->image, VK_IMAGE_ASPECT_COLOR_BIT,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

void vk_record_composite(RendererState& vk, VkCommandBuffer command, const FrameSetup& setup,
                         std::uint32_t& cursor, bool post) {
    // the scene as pass 11 finds it, for the next frame's big screens
    vk_record_scene_last(vk, command);
    // the final target, or the post pass's when the 3D goes through it first
    const VkFramebuffer framebuffer = post ? vk.post_framebuffer : vk.final_framebuffer;
    const GpuImage& target = post ? vk.post : vk.final;
    ScreenDraw block{};
    DrawState state; // fog off; stage 0's alpha kill off (screen_effect_dof turns it off for the
                     // heat haze; elsewhere it would kill nothing, the scene's alpha being set)
    if (!setup.dof) {
        // fullscreen_quad: the scene in texels, times scene_tint
        StageTexture stages[4] = {clamped(texture_scene), {}, {}, {}};
        state.factor = setup.tint;
        state.texel = 1;
        screen_draw(vk, command, framebuffer, target, ScreenProgram::copy, stages, state, cursor,
                    block, quad_copy);
        return;
    }

    // the scene to 320x240, then to 160x120, four taps a texel apart
    StageTexture scene4[4] = {clamped(texture_scene), clamped(texture_scene),
                              clamped(texture_scene), clamped(texture_scene)};
    for (int s = 0; s < 4; ++s) {
        set_scale(block, s, 640, 480);
    }
    screen_draw(vk, command, vk.blur320_framebuffer, vk.blur320, ScreenProgram::blur, scene4,
                state, cursor, block, quad_blur320);
    StageTexture blur4[4] = {clamped(texture_blur320), clamped(texture_blur320),
                             clamped(texture_blur320), clamped(texture_blur320)};
    for (int s = 0; s < 4; ++s) {
        set_scale(block, s, 320, 240);
    }
    screen_draw(vk, command, vk.blur160_framebuffer, vk.blur160, ScreenProgram::blur, blur4,
                state, cursor, block, quad_blur160);

    // the blend by depth past the threshold, times the tint (PS c0 scene_tint, c1 0, c2.a)
    unpack(setup.tint, block.c0);
    for (float& c : block.c0) {
        c = q8(c);
    }
    block.c2[3] = q8(setup.dof_threshold);
    if (setup.heat_wave) {
        // dofblur: the wave's du/dv moves the blur and scene lookups by the bump matrix. Its depth
        // tap reads the default texcoord, the top-left texel, which in the original is outside the
        // safe rectangle and so always the cleared depth 1.0: white stands for it here, so the
        // full-window option blends the same (assets/shaders/original/dofblur.frag)
        StageTexture stages[4] = {clamped(texture_wave), clamped(texture_blur160),
                                  clamped(texture_scene), clamped(texture_white, true)};
        block.texel_scale[0][0] = block.texel_scale[0][1] = 1;
        set_scale(block, 1, 160, 120);
        set_scale(block, 2, 640, 480);
        set_scale(block, 3, 640, 480);
        block.bump_env[0] = setup.bump_scale;
        block.bump_env[1] = setup.bump_shear;
        block.bump_env[2] = setup.bump_scale;
        block.bump_env[3] = setup.bump_shear;
        screen_draw(vk, command, framebuffer, target, ScreenProgram::dofblur, stages, state, cursor,
                    block, quad_dofblur);
    } else {
        StageTexture stages[4] = {clamped(texture_blur160), clamped(texture_scene),
                                  clamped(texture_depth, true), {}};
        set_scale(block, 0, 160, 120);
        set_scale(block, 1, 640, 480);
        set_scale(block, 2, 640, 480);
        screen_draw(vk, command, framebuffer, target, ScreenProgram::dof, stages, state, cursor,
                    block, quad_dof);
    }
}

void vk_record_scene_copy(RendererState& vk, VkCommandBuffer command) {
    vk_barrier(command, vk.scene.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vk_barrier(command, vk.final.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy{};
    copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.extent = {static_cast<std::uint32_t>(vk.final.width),
                   static_cast<std::uint32_t>(vk.final.height), 1};
    vkCmdCopyImage(command, vk.scene.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.final.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    vk_barrier(command, vk.scene.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_barrier(command, vk.final.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void vk_record_gamma(RendererState& vk, VkCommandBuffer command, std::uint32_t& cursor) {
    StageTexture stages[4] = {clamped(texture_final), clamped(texture_ramp, true), {}, {}};
    screen_draw(vk, command, vk.output_framebuffer, vk.output, ScreenProgram::gamma, stages,
                DrawState{}, cursor, ScreenDraw{}, quad_gamma);
}

bool vk_post_wanted(const RendererState& vk) {
    return vk.options.antialias || vk.options.brightness != 100;
}

void vk_record_post(RendererState& vk, VkCommandBuffer command, std::uint32_t& cursor) {
    // c0: the brightness curve's power (BRIGHTNESS b: x^(100/b)), FXAA on
    ScreenDraw block{};
    block.c0[0] = 100.0f / static_cast<float>(std::clamp(vk.options.brightness, 25, 400));
    block.c0[1] = vk.options.antialias ? 1.0f : 0.0f;
    StageTexture stages[4] = {clamped(texture_post), {}, {}, {}};
    screen_draw(vk, command, vk.final_framebuffer, vk.final, ScreenProgram::post, stages,
                DrawState{}, cursor, block, quad_gamma);
}

void vk_record_shadow_quad(RendererState& vk, VkCommandBuffer command, const DrawItem& item,
                           const FrameSetup& setup) {
    (void)setup;
    // the whole target: the quad's corners are the original's 640x480 pixels
    const VkViewport viewport{0, static_cast<float>(vk.scene.height),
                              static_cast<float>(vk.scene.width),
                              -static_cast<float>(vk.scene.height), 0, 1};
    vkCmdSetViewport(command, 0, 1, &viewport);
    PipelineKey key;
    key.program = static_cast<int>(Program::shadow_quad);
    key.blend = item.blend;
    key.stencil = item.stencil;
    key.pass = PassKind::scene;
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_for(vk, key));
    vkCmdPushConstants(command, vk.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof item.state, &item.state);
    const VkDeviceSize offset = quad_shadow;
    vkCmdBindVertexBuffers(command, 0, 1, &vk.screen_quads.buffer, &offset);
    vkCmdDraw(command, 4, 1, 0, 0);
}
