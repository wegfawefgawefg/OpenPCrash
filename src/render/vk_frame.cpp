#include "render/vk_internal.hpp"
#include "render/vk_sprites.hpp"

#include "perf_log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Frame (include/frame.glsl)
struct FrameUniform {
    float fog_colour[4];
    float fog_params[4];
    float linear_size[4];
    float pixel[4];
};

PipelineKey key_of(const DrawItem& item) {
    PipelineKey key;
    key.program = static_cast<int>(item.program);
    key.blend = item.blend;
    key.depth_test = item.depth_test;
    key.depth_write = item.depth_write;
    key.cull = item.cull;
    key.stencil = item.stencil;
    key.pass = PassKind::scene;
    return key;
}

// the 3D viewport: a rectangle of the original's 640x480 screen scaled to the target, through a
// negative height so D3D's clip space comes out the right way up
// the 640x480 screen in the target: stretched over all of it, or (fit_43) fitted 4:3 with bars as
// the 2D is: scale x, scale y, offset x, offset y
void screen_fit(int width, int height, bool fit_43, float out[4]) {
    const float tw = static_cast<float>(width), th = static_cast<float>(height);
    out[0] = tw / 640.0f;
    out[1] = th / 480.0f;
    out[2] = out[3] = 0.0f;
    if (fit_43) {
        const float scale = std::min(out[0], out[1]);
        out[0] = out[1] = scale;
        out[2] = (tw - 640.0f * scale) * 0.5f;
        out[3] = (th - 480.0f * scale) * 0.5f;
    }
}

// the screen adjust in the scene target's pixels: the 2D's screen pixels (Ui::screen_shift) at the
// 2D's scale, the 4:3 box's (sprite_frame), so the 3D moves as far as the 2D over it
void shift_pixels(const RendererState& vk, const float* shift, float out[2]) {
    float fit[4];
    screen_fit(vk.scene.width, vk.scene.height, vk.options.fit_43, fit);
    const float scale = std::min(fit[0], fit[1]);
    out[0] = shift[0] * scale;
    out[1] = shift[1] * scale;
}

// a rectangle of the 640x480 screen in the target, moved by the screen adjust (render.md
// "viewports": retail moves every viewport by GameState.screen_shift_x/y before SetViewport),
// through a negative height so D3D's clip space comes out the right way up
VkViewport scene_viewport(const RendererState& vk, const float* rect, const float* shift) {
    float fit[4], moved[2];
    screen_fit(vk.scene.width, vk.scene.height, vk.options.fit_43, fit);
    shift_pixels(vk, shift, moved);
    const float x = fit[2] + rect[0] * fit[0] + moved[0];
    const float y = fit[3] + rect[1] * fit[1] + moved[1];
    const float w = rect[2] * fit[0], h = rect[3] * fit[1];
    return VkViewport{x, y + h, w, -h, 0, 1};
}

// a viewport's pixels as a scissor (the negative height undone)
VkRect2D viewport_scissor(const VkViewport& v) {
    const float top = v.y + v.height;
    return VkRect2D{{static_cast<std::int32_t>(std::max(0.0f, std::floor(v.x))),
                     static_cast<std::int32_t>(std::max(0.0f, std::floor(top)))},
                    {static_cast<std::uint32_t>(std::ceil(v.width)),
                     static_cast<std::uint32_t>(std::ceil(-v.height))}};
}

// the one view's scissor in a target of width x height: all of it, or with clip_to_2d the 2D's
// reach (the title-safe area moved out by the frame's reach)
VkRect2D scene_clip(const RendererState& vk, const FrameSetup& setup, int width, int height) {
    VkRect2D all{{0, 0}, {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)}};
    if (!setup.clip_to_2d) {
        return all;
    }
    const SpriteFrame f = sprite_frame(width, height, vk.options.fit_43, vk.options.hud_width);
    const float rx = sprite_frame_reach(f), ry = sprite_frame_reach_y(f);
    const float tw = static_cast<float>(width), th = static_cast<float>(height);
    // moved with the screen adjust, as the 3D and the 2D are
    const float sx = setup.screen_shift[0] * f.scale, sy = setup.screen_shift[1] * f.scale;
    const float x0 = std::clamp(std::floor(f.ox + sx + (32.0f - rx) * f.scale), 0.0f, tw);
    const float y0 = std::clamp(std::floor(f.oy + sy + (16.0f - ry) * f.scale), 0.0f, th);
    const float x1 = std::clamp(std::ceil(f.ox + sx + (608.0f + rx) * f.scale), x0, tw);
    const float y1 = std::clamp(std::ceil(f.oy + sy + (464.0f + ry) * f.scale), y0, th);
    return VkRect2D{{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0)},
                    {static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)}};
}

// the split-screen view an item is in (FrameSetup::views), 0 with one view
int item_view(const FrameSetup& setup, std::size_t item) {
    for (int v = 0; v < setup.view_count; ++v) {
        const FrameSetup::View& view = setup.views[v];
        if (item >= view.first && item < static_cast<std::size_t>(view.first) + view.count) {
            return v;
        }
    }
    return 0;
}

// soft focus off as the plain tinted copy (RenderOptions); the brightness is the post pass's
// (vk_record_post), over the 3D alone
FrameSetup adjusted_setup(const RendererState& vk, const FrameSetup& setup) {
    FrameSetup out = setup;
    if (!vk.options.soft_focus) {
        out.dof = false;
        out.heat_wave = false;
    }
    return out;
}

// render pass 13 (render.md "Passes": after the sprites of pass 12, depth cleared first): the
// late items (pass 13) or the cockpit's (pass 11), plain meshes, on the final target through the
// frame's viewport (the cockpit's on the post target when the 3D takes the post pass)
void record_late(RendererState& vk, const FrameSetup& setup, const FrameDraws& draws,
                 const std::vector<VkPipeline>& pipelines, const std::vector<VkDescriptorSet>& sets,
                 const std::vector<std::uint32_t>& offsets, bool cockpit, bool post = false) {
    // the overlay's colour writes done before the pass loads them
    VkMemoryBarrier written{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    written.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    written.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(vk.command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 1, &written, 0, nullptr,
                         0, nullptr);
    VkClearValue clears[2]{};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = vk.late_pass;
    pass.framebuffer = post ? vk.post_late_framebuffer : vk.late_framebuffer;
    pass.renderArea.extent = {static_cast<std::uint32_t>(vk.final.width),
                              static_cast<std::uint32_t>(vk.final.height)};
    pass.clearValueCount = 2;
    pass.pClearValues = clears;
    vkCmdBeginRenderPass(vk.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport = scene_viewport(vk, setup.viewport, setup.screen_shift);
    vkCmdSetViewport(vk.command, 0, 1, &viewport);
    const VkRect2D clip = scene_clip(vk, setup, vk.final.width, vk.final.height);
    vkCmdSetScissor(vk.command, 0, 1, &clip);
    const VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(vk.command, 1, 1, &vk.bone_zero.buffer, &zero);
    for (std::size_t i = 0; i < draws.items.size(); ++i) {
        const DrawItem& item = draws.items[i];
        if ((cockpit ? !item.cockpit : !item.late) || item.mesh < 0 || item.mesh >= static_cast<int>(vk.meshes.size()) ||
            !vk.meshes[item.mesh].vertices.buffer || !pipelines[i] || !sets[i] ||
            !item.index_count) {
            continue;
        }
        const GpuMesh& mesh = vk.meshes[item.mesh];
        vkCmdBindPipeline(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[i]);
        vkCmdBindVertexBuffers(vk.command, 0, 1, &mesh.vertices.buffer, &zero);
        vkCmdBindIndexBuffer(vk.command, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT16);
        VkDescriptorSet bind[3] = {vk.frame_set, vk.draw_set, sets[i]};
        vkCmdBindDescriptorSets(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, 0,
                                3, bind, 1, &offsets[i]);
        vkCmdPushConstants(vk.command, vk.pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof item.state, &item.state);
        vkCmdDrawIndexed(vk.command, item.index_count, 1, item.first_index, 0, 0);
    }
    vkCmdEndRenderPass(vk.command);
}

// the last frame's timestamps (its fence done) as the GPU's pass times, for the run log
void read_timestamps(RendererState& vk) {
    if (!vk.timestamps || !vk.timestamps_written) {
        return;
    }
    vk.timestamps_written = false;
    std::uint64_t ticks[timestamp_count];
    if (vkGetQueryPoolResults(vk.device, vk.timestamps, 0, timestamp_count, sizeof ticks, ticks,
                              sizeof ticks[0], VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
        return;
    }
    float ms[static_cast<int>(GpuPass::count)];
    for (std::uint32_t i = 0; i + 1 < timestamp_count; ++i) {
        const std::uint64_t ticks_between = (ticks[i + 1] - ticks[i]) & vk.timestamp_mask;
        ms[i] = static_cast<float>(static_cast<double>(ticks_between) * vk.timestamp_period * 1e-6);
    }
    perf_gpu(ms);
}

// a timestamp at the end of what came before (the start at the top of the pipe)
void timestamp(RendererState& vk, std::uint32_t index) {
    if (vk.timestamps) {
        vkCmdWriteTimestamp(vk.command,
                            index == 0 ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                       : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            vk.timestamps, index);
    }
}

} // namespace

float renderer_depth_probe(Renderer& renderer, float x, float y) {
    RendererState& vk = *renderer.vk;
    vk.probe_at[0] = x;
    vk.probe_at[1] = y;
    return vk.probe_value;
}

std::uint32_t push_uniform(FrameDraws& draws, const void* block, std::size_t size) {
    const std::size_t offset = (draws.uniforms.size() + 15) / 16 * 16;
    draws.uniforms.resize(offset + size);
    std::memcpy(draws.uniforms.data() + offset, block, size);
    return static_cast<std::uint32_t>(offset);
}

float renderer_scene_aspect(const Renderer& renderer, const float viewport[4]) {
    if (renderer.height <= 0 || viewport[3] <= 0) {
        return 1.0f;
    }
    float fit[4];
    screen_fit(renderer.width, renderer.height, renderer.options.fit_43, fit);
    return viewport[2] * fit[0] / (viewport[3] * fit[1]);
}

bool renderer_draw_frame(Renderer& renderer, const FrameSetup& original_setup,
                         const FrameDraws& draws, const std::string& capture_path) {
    RendererState& vk = *renderer.vk;
    const std::uint64_t frame_start = perf_now();
    FrameSetup setup = adjusted_setup(vk, original_setup);
    setup.screen_shift[0] = static_cast<float>(renderer.screen_shift[0]);
    setup.screen_shift[1] = static_cast<float>(renderer.screen_shift[1]);
    vk.screen_shift[0] = setup.screen_shift[0];
    vk.screen_shift[1] = setup.screen_shift[1];
    // one frame in flight: the last one done before this one's buffers and commands are written
    // (a lost device ends the run)
    std::uint64_t blocked = perf_now();
    if (!vk_check(vkWaitForFences(vk.device, 1, &vk.fence, VK_TRUE, UINT64_MAX), "wait frame")) {
        return false;
    }
    blocked = perf_now() - blocked;
    perf_add(PerfPart::fence, blocked);
    read_timestamps(vk);
    const std::uint64_t uploads_start = perf_now();
    if (!vk_flush_uploads(vk) || !vk_fit_window(vk, renderer)) {
        return false;
    }
    blocked += perf_now() - uploads_start; // counted as uploads, or a resize
    vk_release_freed(vk);
    // the last frame's depth probe (24-bit unorm in the low bits, or a float)
    if (vk.probe_recorded && vk.probe.mapped) {
        std::uint32_t word = 0;
        std::memcpy(&word, vk.probe.mapped, 4);
        if (vk.depth_format == VK_FORMAT_D24_UNORM_S8_UINT) {
            vk.probe_value = static_cast<float>(word & 0xFFFFFF) / 16777215.0f;
        } else {
            std::memcpy(&vk.probe_value, &word, 4);
        }
        vk.probe_recorded = false;
    }

    // the frame block; the per-draw blocks into the ring, each at the device's alignment
    FrameUniform frame{};
    std::copy(setup.fog_colour, setup.fog_colour + 4, frame.fog_colour);
    std::copy(setup.fog_params, setup.fog_params + 4, frame.fog_params);
    frame.linear_size[0] = 640;
    frame.linear_size[1] = 480;
    frame.linear_size[2] = 1.0f / 640;
    frame.linear_size[3] = 1.0f / 480;
    frame.pixel[0] = 0.5f;
    std::memcpy(vk.frame_uniform.mapped, &frame, sizeof frame);
    const std::uint32_t align = static_cast<std::uint32_t>(vk.uniform_alignment);
    std::vector<std::uint32_t>& offsets = vk.frame_offsets;
    offsets.assign(draws.items.size(), 0);
    std::uint32_t cursor = 0;
    for (std::size_t i = 0; i < draws.items.size(); ++i) {
        const DrawItem& item = draws.items[i];
        offsets[i] = cursor;
        if (item.uniform_size && cursor + item.uniform_size <= vk.draw_uniforms.size - 2048) {
            std::memcpy(static_cast<std::uint8_t*>(vk.draw_uniforms.mapped) + cursor,
                        draws.uniforms.data() + item.uniform_offset, item.uniform_size);
            cursor = (cursor + item.uniform_size + align - 1) / align * align;
        }
    }

    // the billboards (set 1 binding 1)
    if (!draws.storage.empty()) {
        std::memcpy(vk.storage.mapped, draws.storage.data(),
                    std::min<std::size_t>(draws.storage.size(), vk.storage.size));
    }

    // pipelines and texture sets made before recording (both cached)
    std::vector<VkPipeline>& pipelines = vk.frame_pipelines;
    std::vector<VkDescriptorSet>& sets = vk.frame_sets;
    pipelines.assign(draws.items.size(), VK_NULL_HANDLE);
    sets.assign(draws.items.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < draws.items.size(); ++i) {
        if (draws.items[i].program != Program::shadow_quad) {
            pipelines[i] = vk_pipeline_for(vk, key_of(draws.items[i]));
            sets[i] = vk_texture_set(vk, draws.items[i].stages);
        }
    }

    std::uint32_t image_index = 0;
    const std::uint64_t acquire_start = perf_now();
    VkResult acquired = vkAcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX, vk.acquired,
                                              VK_NULL_HANDLE, &image_index);
    const std::uint64_t acquire_time = perf_now() - acquire_start;
    perf_add(PerfPart::acquire, acquire_time);
    blocked += acquire_time;
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        vk.swap_stale = true;
        return true;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        return vk_check(acquired, "acquire");
    }
    vkResetFences(vk.device, 1, &vk.fence);
    vkResetCommandBuffer(vk.command, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(vk.command, &begin);
    if (vk.timestamps) {
        vkCmdResetQueryPool(vk.command, vk.timestamps, 0, timestamp_count);
    }
    timestamp(vk, 0);
    vk_record_stream(vk, vk.command);
    vk_write_dynamic_textures(vk, vk.command, setup);

    // the scene: cleared to clear_color, depth 1, stencil 0 (render_frame_open)
    VkClearValue clears[2];
    std::memcpy(clears[0].color.float32, setup.clear, sizeof setup.clear);
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = vk.scene_pass;
    pass.framebuffer = vk.scene_framebuffer;
    pass.renderArea.extent = {static_cast<std::uint32_t>(vk.scene.width),
                              static_cast<std::uint32_t>(vk.scene.height)};
    pass.clearValueCount = 2;
    pass.pClearValues = clears;
    vkCmdBeginRenderPass(vk.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    // 2D under the 3D (overlay pass 0)
    vk_sprites_record(vk, SpriteLayer::Under);
    // one view through the frame's viewport over the whole target, or each split-screen view's
    // draws through its rectangle and only inside it
    VkViewport viewport = scene_viewport(vk, setup.viewport, setup.screen_shift);
    vkCmdSetViewport(vk.command, 0, 1, &viewport);
    const VkRect2D clip = scene_clip(vk, setup, vk.scene.width, vk.scene.height);
    vkCmdSetScissor(vk.command, 0, 1, &clip);
    int bound_view = -1;
    VkPipeline bound = VK_NULL_HANDLE;
    int bound_mesh = -1;
    const VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(vk.command, 1, 1, &vk.bone_zero.buffer, &zero);
    for (std::size_t i = 0; i < draws.items.size(); ++i) {
        const DrawItem& item = draws.items[i];
        if (item.late || item.cockpit) {
            continue; // pass 13 after the 2D of pass 12, pass 11 after the composite
        }
        if (item.backdrop) {
            // pass_open_camo's copy (vk_backdrop.cpp): a new pass, so the state again
            vk_record_backdrop(vk, vk.command);
            vkCmdSetViewport(vk.command, 0, 1, &viewport);
            const VkRect2D scissor = setup.view_count > 0 ? viewport_scissor(viewport) : clip;
            vkCmdSetScissor(vk.command, 0, 1, &scissor);
            vkCmdBindVertexBuffers(vk.command, 1, 1, &vk.bone_zero.buffer, &zero);
            bound = VK_NULL_HANDLE;
            bound_mesh = -1;
        }
        if (setup.view_count > 0) {
            const int view = item_view(setup, i);
            if (view != bound_view) {
                viewport = scene_viewport(vk, setup.views[view].viewport, setup.screen_shift);
                const VkRect2D scissor = viewport_scissor(viewport);
                vkCmdSetViewport(vk.command, 0, 1, &viewport);
                vkCmdSetScissor(vk.command, 0, 1, &scissor);
                bound_view = view;
            }
        }
        if (item.program == Program::shadow_quad) {
            vk_record_shadow_quad(vk, vk.command, item, setup);
            vkCmdSetViewport(vk.command, 0, 1, &viewport);
            bound = VK_NULL_HANDLE;
            bound_mesh = -1;
            continue;
        }
        if (item.program == Program::particle) {
            // billboards: six vertices an instance, no vertex buffer (src/effects)
            const std::uint32_t last = item.first_index + item.index_count;
            if (!pipelines[i] || !sets[i] || !item.index_count || last * 48u > vk.storage.size) {
                continue;
            }
            vkCmdBindPipeline(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[i]);
            bound = pipelines[i];
            VkDescriptorSet bind[3] = {vk.frame_set, vk.draw_set, sets[i]};
            vkCmdBindDescriptorSets(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    vk.pipeline_layout, 0, 3, bind, 1, &offsets[i]);
            vkCmdPushConstants(vk.command, vk.pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof item.state, &item.state);
            vkCmdDraw(vk.command, 6, item.index_count, 0, item.first_index);
            continue;
        }
        if (item.mesh < 0 || item.mesh >= static_cast<int>(vk.meshes.size()) ||
            !vk.meshes[item.mesh].vertices.buffer || !pipelines[i] ||
            !sets[i] || !item.index_count) {
            continue;
        }
        if (pipelines[i] != bound) {
            vkCmdBindPipeline(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[i]);
            bound = pipelines[i];
        }
        if (item.mesh != bound_mesh) {
            const GpuMesh& mesh = vk.meshes[item.mesh];
            vkCmdBindVertexBuffers(vk.command, 0, 1, &mesh.vertices.buffer, &zero);
            vkCmdBindIndexBuffer(vk.command, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT16);
            bound_mesh = item.mesh;
        }
        VkDescriptorSet bind[3] = {vk.frame_set, vk.draw_set, sets[i]};
        vkCmdBindDescriptorSets(vk.command, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, 0,
                                3, bind, 1, &offsets[i]);
        vkCmdPushConstants(vk.command, vk.pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof item.state, &item.state);
        vkCmdDrawIndexed(vk.command, item.index_count, 1, item.first_index, 0, 0);
    }
    vkCmdEndRenderPass(vk.command);

    // the depth probe: one texel of the depth aspect, read back at the next frame's start
    if (vk.probe_at[0] >= 0 && !draws.items.empty() &&
        (vk.probe.buffer || vk_create_mapped_buffer(vk, 16, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                     vk.probe))) {
        float moved[2];
        shift_pixels(vk, setup.screen_shift, moved);
        const int px = std::clamp(
            static_cast<int>(vk.probe_at[0] * vk.depth.width / 640.0f + moved[0]), 0,
            vk.depth.width - 1);
        const int py = std::clamp(
            static_cast<int>(vk.probe_at[1] * vk.depth.height / 480.0f + moved[1]), 0,
            vk.depth.height - 1);
        vk_barrier(vk.command, vk.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        copy.imageOffset = {px, py, 0};
        copy.imageExtent = {1, 1, 1};
        vkCmdCopyImageToBuffer(vk.command, vk.depth.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               vk.probe.buffer, 1, &copy);
        vk_barrier(vk.command, vk.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        vk.probe_recorded = true;
        vk.probe_at[0] = -1;
    }
    timestamp(vk, 1);

    // the pass 11 composite into the final target (a frame with 3D only), then the gamma ramp
    // into the output. The port's post pass (FXAA, BRIGHTNESS) takes the 3D first when asked:
    // the composite and the cockpit's meshes into the post target, then through it into the
    // final one, so the 2D after is untouched
    const bool post = !draws.items.empty() && vk_post_wanted(vk);
    if (draws.items.empty()) {
        vk_record_scene_copy(vk, vk.command);
    } else {
        vk_record_composite(vk, vk.command, setup, cursor, post);
    }
    // the passes from 11 on the final target: the cockpit instruments, then the 2D over the 3D
    // (overlay passes 12, 14, 15) with pass 13's meshes between 12 and 14
    if (std::any_of(draws.items.begin(), draws.items.end(),
                    [](const DrawItem& item) { return item.cockpit; })) {
        record_late(vk, setup, draws, pipelines, sets, offsets, true, post);
    }
    timestamp(vk, 2);
    if (post) {
        vk_record_post(vk, vk.command, cursor);
    }
    const bool late = std::any_of(draws.items.begin(), draws.items.end(),
                                  [](const DrawItem& item) { return item.late; });
    if (!late) {
        vk_sprites_record(vk, SpriteLayer::Over);
    } else {
        vk_sprites_record(vk, SpriteLayer::Over12);
        record_late(vk, setup, draws, pipelines, sets, offsets, false);
        vk_sprites_record(vk, SpriteLayer::Over14);
    }
    timestamp(vk, 3);
    vk_record_capture(vk, vk.command);
    vk_record_gamma(vk, vk.command, cursor);
    timestamp(vk, 4);

    // the output to the window, scaled to the swapchain's size
    vk_barrier(vk.command, vk.output.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkImage swap_image = vk.swap_images[image_index];
    vk_barrier(vk.command, swap_image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit{};
    blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {vk.output.width, vk.output.height, 1};
    blit.dstOffsets[1] = {static_cast<int>(vk.swap_extent.width),
                          static_cast<int>(vk.swap_extent.height), 1};
    // nearest at the window's own size, filtered when the render scale differs
    const bool same_size = vk.output.width == static_cast<int>(vk.swap_extent.width) &&
                           vk.output.height == static_cast<int>(vk.swap_extent.height);
    vkCmdBlitImage(vk.command, vk.output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swap_image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   same_size ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    vk_barrier(vk.command, swap_image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    timestamp(vk, 5);
    vkEndCommandBuffer(vk.command);
    const std::uint64_t present_start = perf_now();
    perf_add(PerfPart::record, present_start - frame_start - blocked);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &vk.acquired;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &vk.command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &vk.finished[image_index];
    if (!vk_check(vkQueueSubmit(vk.queue, 1, &submit, vk.fence), "submit frame")) {
        return false;
    }
    vk.timestamps_written = vk.timestamps != VK_NULL_HANDLE;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &vk.finished[image_index];
    present.swapchainCount = 1;
    present.pSwapchains = &vk.swapchain;
    present.pImageIndices = &image_index;
    const VkResult presented = vkQueuePresentKHR(vk.queue, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        vk.swap_stale = true;
    } else if (presented == VK_ERROR_DEVICE_LOST) {
        return vk_check(presented, "present");
    }
    perf_add(PerfPart::present, perf_now() - present_start);

    if (!capture_path.empty()) {
        const PerfScope timed(PerfPart::capture);
        if (!vk_check(vkWaitForFences(vk.device, 1, &vk.fence, VK_TRUE, UINT64_MAX),
                      "wait frame") ||
            !vk_save_capture(vk, capture_path)) {
            return false;
        }
        std::printf("captured %s\n", capture_path.c_str());
    }
    return true;
}
