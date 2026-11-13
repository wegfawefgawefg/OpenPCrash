// The overlay passes on Vulkan (vk_sprites.hpp). The corners are built on the CPU exactly as
// pass_draw_overlay builds them for its constant banks, then put through spritemat's
// pixel-to-clip formula; the batches break where the original's do (texture or filter change).
// Pass 0 draws into the scene target before the meshes (so the pass 11 composite takes it in, as
// the original's), passes 12, 14 and 15 onto the final target after the composite and before the
// gamma ramp.
#include "render/vk_sprites.hpp"

#include "render/vk_internal.hpp"
#include "render/vk_sprite_state.hpp"
#include "talk/draw2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace {

// stage 0 the sprite's texture, clamped, point unless flag 0x10 (the renderer's set cache, made
// again after a resize)
VkDescriptorSet texture_set(RendererState& vk, int texture, bool linear) {
    StageTexture stages[4];
    stages[0].texture = texture != texture_none ? texture : texture_white;
    stages[0].u = stages[0].v = Address::Clamp;
    stages[0].point = !linear;
    return vk_texture_set(vk, stages);
}

// the overlay pass a layer goes to (render_sprite_quad): 0 under the 3D, 12, 14, 15 over it
int sprite_pass(int layer) {
    const unsigned b = static_cast<unsigned>(layer);
    if (b <= 8) {
        return 0;
    }
    if (b >= 0x1000 && b < 0x7fff) {
        return 14;
    }
    return b == 0x7fff ? 15 : 12;
}

// the viewport a pass draws through, in 640x480 pixels: x, y, width, height (viewport_rect, one
// view; pass 15 the whole screen: render.md "Views and cameras")
void pass_viewport(int pass, float out[4]) {
    if (pass == 15) {
        out[0] = 0, out[1] = 0, out[2] = 640, out[3] = 480;
    } else {
        out[0] = 32, out[1] = 16, out[2] = 576, out[3] = 448;
    }
}

// a pass's viewport, or for a quad with a view in passes 0 and 12, that view's
void quad_viewport(const SpriteState& s, int pass, int view, float out[4]) {
    if (view >= 0 && view < s.view_count && (pass == 0 || pass == 12)) {
        std::copy(s.views[view], s.views[view] + 4, out);
        return;
    }
    pass_viewport(pass, out);
}

int quad_view(const SpriteState& s, const Quad2D& q) {
    const int pass = sprite_pass(q.layer);
    if (q.flags & quad_full_window) {
        return -1;
    }
    return q.view >= 0 && q.view < s.view_count && (pass == 0 || pass == 12) ? q.view : -1;
}

// render_sprite_quad's colour (alpha times z, truncated) and pass_draw_overlay's corners for one
// quad, moved by (ox, oy), then spritemat's clip formula for a w x h viewport; false when its
// alpha is 0
bool quad_vertices(const Quad2D& q, const SlotTexture& t, float ox, float oy, float w, float h,
                   SpriteVertex out[6]) {
    const int alpha = static_cast<int>(static_cast<float>(q.color >> 24) * q.alpha);
    if ((alpha & 0xff) == 0) {
        return false;
    }
    // texel coordinates (flag 2) are the texture's own; the rest divide by the record's sheet
    const float tw = (q.flags & 2) ? static_cast<float>(t.width) : q.sheet_w;
    const float th = (q.flags & 2) ? static_cast<float>(t.height) : q.sheet_h;
    float u0 = q.u0 / tw, u1 = q.u1 / tw, v0 = q.v0 / th, v1 = q.v1 / th;
    if (q.flags & sprite_mirror_u) {
        std::swap(u0, u1);
    }
    if (q.flags & sprite_mirror_v) {
        std::swap(v0, v1);
    }
    // whole pixels unless flag 0x10; corners in viewport pixels, turned about the pivot
    const float x = (q.flags & sprite_smooth) ? q.x : std::floor(q.x);
    const float y = (q.flags & sprite_smooth) ? q.y : std::floor(q.y);
    const float x0 = x - 32.0f + ox, y0 = y - 16.0f + oy;
    const float x1 = q.w + x0, y1 = q.h + y0;
    const float px = x0 + q.rot[0], py = y0 + q.rot[1];
    const float c = std::cos(q.rot[2]), s = std::sin(q.rot[2]);
    const float corners[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const float uvs[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    const float colour[4] = {
        static_cast<float>((q.color >> 16) & 0xff) / 255.0f,
        static_cast<float>((q.color >> 8) & 0xff) / 255.0f,
        static_cast<float>(q.color & 0xff) / 255.0f,
        static_cast<float>(alpha & 0xff) / 255.0f,
    };
    const float k = (q.flags & 1) ? 0.0f : 1.0f;
    // the blur's radius in texels as a uv distance
    const float blur = q.blur > 0 ? q.blur / std::max(tw, 1.0f) : 0.0f;
    const float hw = w * 0.5f - 0.5f, hh = h * 0.5f - 0.5f;
    SpriteVertex v[4];
    for (int i = 0; i < 4; ++i) {
        const float dx = corners[i][0] - px, dy = corners[i][1] - py;
        const float cx = dx * c - dy * s + px;
        const float cy = dx * s + dy * c + py;
        // spritemat (D3D's clip square), then D3D's pixel-centre convention to Vulkan's: half a
        // pixel right and down, and y down
        v[i].position[0] = (cx - hw) / hw + 1.0f / w;
        v[i].position[1] = (cy - hh) / hh + 1.0f / h;
        v[i].uv[0] = uvs[i][0];
        v[i].uv[1] = uvs[i][1];
        std::memcpy(v[i].colour, colour, sizeof colour);
        v[i].colour[3] *= (i == 0 || i == 3) ? q.fade[0] : q.fade[1]; // corners 0, 3 the left
        v[i].k = k;
        v[i].blur = blur;
        v[i].period[0] = q.blur_period[1] / std::max(tw, 1.0f);
        v[i].period[1] = q.blur_period[0] / std::max(th, 1.0f);
    }
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
        out[i] = v[order[i]];
    }
    return true;
}

// the frame a view (or the one view, -1) is placed in, for a target of width x height
SpriteFrame view_frame(const RenderOptions& options, const SpriteState& s, int view, int width,
                       int height) {
    const float* drawn = view >= 0 && view < s.view_count ? s.drawn[view] : nullptr;
    return sprite_frame(width, height, options.fit_43, options.hud_width, drawn);
}

// a quad's clip in the 640x480 screen for its anchor, from its pass's (or view's) viewport: the
// viewport itself, moved out by the edge for left and right, widened to the window for free
void anchor_clip(const float view[4], const SpriteFrame& f, int anchor, float out[4]) {
    std::copy(view, view + 4, out);
    if (anchor == anchor_left) {
        out[0] -= f.edge;
    } else if (anchor == anchor_right) {
        out[0] += f.edge;
    } else if (anchor == anchor_free) {
        const float reach = sprite_frame_reach(f), reach_y = sprite_frame_reach_y(f);
        out[0] -= reach;
        out[1] -= reach_y;
        out[2] += reach * 2.0f;
        out[3] += reach_y * 2.0f;
    }
}

void build_runs(SpriteState& s, const RendererState& vk) {
    s.runs.clear();
    // passes in order, layers ascending, submit order kept for equal layers (retail's quicksort
    // leaves those undefined)
    std::vector<std::size_t> order(s.quads.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const Quad2D& qa = s.quads[a].quad;
        const Quad2D& qb = s.quads[b].quad;
        const int pa = sprite_pass(qa.layer), pb = sprite_pass(qb.layer);
        if (pa != pb) {
            return pa < pb;
        }
        if (qa.layer != qb.layer) {
            return static_cast<unsigned>(qa.layer) < static_cast<unsigned>(qb.layer);
        }
        return qa.sequence < qb.sequence;
    });
    auto* out = static_cast<SpriteVertex*>(s.vertices.mapped);
    std::uint32_t written = 0;
    for (std::size_t index : order) {
        if (written / 6 >= sprite_max_quads) {
            break;
        }
        const SpriteItem& item = s.quads[index];
        const int pass = sprite_pass(item.quad.layer);
        const int quad_in = quad_view(s, item.quad);
        float view[4];
        quad_viewport(s, pass, quad_in, view);
        // a full-window quad: placed in its pass's viewport as any, drawn through the whole screen;
        // the rest through the viewport reaching past the box to the window's edges
        const bool wide = (item.quad.flags & quad_full_window) != 0;
        const SpriteFrame f = view_frame(vk.options, s, quad_in, vk.scene.width, vk.scene.height);
        const float rx = sprite_frame_reach(f), ry = sprite_frame_reach_y(f);
        const float ox = wide ? view[0] : rx, oy = wide ? view[1] : ry;
        const float vw = wide ? 640.0f : view[2] + rx * 2.0f;
        const float vh = wide ? 480.0f : view[3] + ry * 2.0f;
        if (!quad_vertices(item.quad, item.texture, ox, oy, vw, vh, out + written)) {
            continue;
        }
        const int texture = item.texture.texture;
        const bool linear = (item.quad.flags & sprite_smooth) != 0;
        const std::uint8_t anchor =
            item.quad.anchor == anchor_auto ? std::uint8_t{anchor_centre} : item.quad.anchor;
        if (s.runs.empty() || s.runs.back().pass != pass || s.runs.back().view != quad_in ||
            s.runs.back().wide != wide || s.runs.back().anchor != anchor ||
            s.runs.back().texture != texture || s.runs.back().linear != linear) {
            s.runs.push_back({pass, quad_in, wide, anchor, texture, linear, written, 0});
        }
        s.runs.back().count += 6;
        written += 6;
    }
}

} // namespace

void renderer_set_sprites(Renderer& renderer, const Draw2D& draw,
                          const std::vector<SlotTexture>& slot_textures) {
    RendererState& vk = *renderer.vk;
    if (vk.sprites) {
        vk.sprites->quads.clear();
    }
    renderer_add_sprites(renderer, draw, slot_textures);
}

void renderer_set_sprite_views(Renderer& renderer, const float (*rects)[4],
                               const float (*drawn)[4], int count) {
    RendererState& vk = *renderer.vk;
    if (!vk.sprites) {
        vk.sprites = new SpriteState;
    }
    SpriteState& s = *vk.sprites;
    s.view_count = std::clamp(count, 0, 4);
    for (int v = 0; v < s.view_count; ++v) {
        std::copy(rects[v], rects[v] + 4, s.views[v]);
        std::copy(drawn[v], drawn[v] + 4, s.drawn[v]);
    }
}

SpriteFrame renderer_sprite_frame(const Renderer& renderer, int view) {
    static const SpriteState none;
    const SpriteState& s = renderer.vk && renderer.vk->sprites ? *renderer.vk->sprites : none;
    return view_frame(renderer.options, s, view, renderer.width, renderer.height);
}

int renderer_sprite_view_count(const Renderer& renderer) {
    return renderer.vk && renderer.vk->sprites ? renderer.vk->sprites->view_count : 0;
}

const float* renderer_sprite_view_rect(const Renderer& renderer, int view) {
    static const float one[4] = {32, 16, 576, 448};
    if (view < 0 || view >= renderer_sprite_view_count(renderer)) {
        return one;
    }
    return renderer.vk->sprites->views[view];
}

void renderer_add_sprites(Renderer& renderer, const Draw2D& draw,
                          const std::vector<SlotTexture>& slot_textures) {
    RendererState& vk = *renderer.vk;
    if (!vk.sprites) {
        vk.sprites = new SpriteState;
    }
    std::vector<SpriteItem>& items = vk.sprites->quads;
    for (const Quad2D& quad : draw.quads) {
        SpriteItem item{quad, {}};
        if (quad.slot >= 0) {
            if (quad.slot >= static_cast<int>(slot_textures.size()) ||
                slot_textures[static_cast<std::size_t>(quad.slot)].texture == texture_none) {
                continue; // nothing in that slot: the original draws whatever was there last
            }
            item.texture = slot_textures[static_cast<std::size_t>(quad.slot)];
        }
        items.push_back(item);
    }
}

void vk_sprites_record(RendererState& vk, SpriteLayer layer) {
    if (!vk.sprites || vk.sprites->failed) {
        return;
    }
    SpriteState& s = *vk.sprites;
    if (layer == SpriteLayer::Under) {
        if (!s.layout && !vk_sprite_pipelines_create(vk, s)) {
            s.failed = true;
            return;
        }
        build_runs(s, vk);
    }
    // which passes this layer draws
    const auto in_layer = [layer](int pass) {
        switch (layer) {
        case SpriteLayer::Under:
            return pass == 0;
        case SpriteLayer::Over12:
            return pass == 12;
        case SpriteLayer::Over14:
            return pass >= 14;
        default:
            return pass != 0;
        }
    };
    const bool any = std::any_of(s.runs.begin(), s.runs.end(),
                                 [&](const SpriteRun& run) { return in_layer(run.pass); });
    if (!any || !s.scene_pipeline) {
        return;
    }

    // over the 3D: a pass of its own on the final target, loading the composite's picture
    VkCommandBuffer command = vk.command;
    if (layer != SpriteLayer::Under) {
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        begin.renderPass = s.load_pass;
        begin.framebuffer = vk.final_framebuffer;
        begin.renderArea.extent = {static_cast<std::uint32_t>(vk.final.width),
                                   static_cast<std::uint32_t>(vk.final.height)};
        vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    }

    // the 640x480 screen fitted into the target with bars, aspect kept, each view's frame reaching
    // past that box in a wide window
    const GpuImage& target = layer == SpriteLayer::Under ? vk.scene : vk.final;
    const float tw = static_cast<float>(target.width), th = static_cast<float>(target.height);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      layer == SpriteLayer::Under ? s.scene_pipeline : s.final_pipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &s.vertices.buffer, &offset);
    int bound_pass = -1, bound_view = -2, bound_anchor = -1;
    bool bound_wide = false;
    for (const SpriteRun& run : s.runs) {
        if (!in_layer(run.pass)) {
            continue;
        }
        if (run.pass != bound_pass || run.view != bound_view || run.wide != bound_wide ||
            run.anchor != bound_anchor) {
            float view[4], clip[4];
            quad_viewport(s, run.pass, run.view, view);
            const SpriteFrame f = view_frame(vk.options, s, run.view, target.width, target.height);
            const float x0 = f.ox + f.shift * f.scale, y0 = f.oy;
            const float rx = sprite_frame_reach(f), ry = sprite_frame_reach_y(f);
            VkViewport viewport{x0 + (view[0] - rx) * f.scale, y0 + (view[1] - ry) * f.scale,
                                (view[2] + rx * 2.0f) * f.scale, (view[3] + ry * 2.0f) * f.scale,
                                0, 1};
            anchor_clip(view, f, run.anchor, clip);
            // the screen adjust moved the 2D (Ui::screen_shift): its box clips go with it
            if (run.anchor != anchor_free) {
                clip[0] += vk.screen_shift[0];
                clip[1] += vk.screen_shift[1];
            }
            float sx0 = x0 + clip[0] * f.scale, sy0 = y0 + clip[1] * f.scale;
            float sx1 = x0 + (clip[0] + clip[2]) * f.scale;
            float sy1 = y0 + (clip[1] + clip[3]) * f.scale;
            if (run.wide) {
                viewport = VkViewport{0, 0, tw, th, 0, 1}; // the 640x480 over the whole target
                sx0 = sy0 = 0, sx1 = tw, sy1 = th;
            }
            // whole pixels inside the target (a scissor can't start left of or above it)
            sx0 = std::clamp(std::floor(sx0), 0.0f, tw);
            sy0 = std::clamp(std::floor(sy0), 0.0f, th);
            sx1 = std::clamp(std::ceil(sx1), sx0, tw);
            sy1 = std::clamp(std::ceil(sy1), sy0, th);
            const VkRect2D scissor{
                {static_cast<std::int32_t>(sx0), static_cast<std::int32_t>(sy0)},
                {static_cast<std::uint32_t>(sx1 - sx0), static_cast<std::uint32_t>(sy1 - sy0)}};
            vkCmdSetViewport(command, 0, 1, &viewport);
            vkCmdSetScissor(command, 0, 1, &scissor);
            bound_pass = run.pass;
            bound_view = run.view;
            bound_wide = run.wide;
            bound_anchor = run.anchor;
        }
        VkDescriptorSet set = texture_set(vk, run.texture, run.linear);
        if (!set) {
            continue;
        }
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, s.layout, 0, 1, &set, 0,
                                nullptr);
        vkCmdDraw(command, run.count, 1, run.first, 0);
    }
    if (layer != SpriteLayer::Under) {
        vkCmdEndRenderPass(command);
    }
}

void vk_sprites_destroy(RendererState& vk) {
    if (!vk.sprites) {
        return;
    }
    SpriteState& s = *vk.sprites;
    vk_destroy_buffer(vk, s.vertices);
    for (VkPipeline pipeline : {s.scene_pipeline, s.final_pipeline}) {
        if (pipeline) {
            vkDestroyPipeline(vk.device, pipeline, nullptr);
        }
    }
    if (s.load_pass) {
        vkDestroyRenderPass(vk.device, s.load_pass, nullptr);
    }
    if (s.layout) {
        vkDestroyPipelineLayout(vk.device, s.layout, nullptr);
    }
    delete vk.sprites;
    vk.sprites = nullptr;
}
