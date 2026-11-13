// The backdrop 0x222 (decomp docs/port/render.md "Targets", pass_open_camo): the
// original draws passes 0..4 into 0x222 and, at the first pass 5 opener of a frame, copies it
// to 0x223 and goes on there, so the camouflage (pass 5) and the shockwave decals (pass 6) sample
// the picture without themselves. Here the scene pass draws on; a draw flagged `backdrop` ends it,
// copies the scene's 640x480 screen over the backdrop and begins it again, loading colour, depth
// and stencil, so the draws after it land on the same picture. One copy a flagged draw: each
// view's first pass 5 or 6 draw (retail copies once a frame, so a later split-screen view's
// camouflage saw a backdrop without its own scene: docs/bugs.md). And the capture 0x225 copied from
// the final target (vk_record_capture), and the scene kept for the next frame's big screens
// (vk_record_scene_last: retail's mesh 0x80000 samples 0x223 while drawing into it).
#include "render/vk_internal.hpp"

#include <algorithm>

namespace {

constexpr VkFormat colour_format = VK_FORMAT_R8G8B8A8_UNORM;

// the resume pass: the scene pass's attachments loaded from where it left them
bool make_resume_pass(RendererState& vk) {
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = colour_format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1] = attachments[0];
    attachments[1].format = vk.depth_format;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference colour{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colour;
    subpass.pDepthStencilAttachment = &depth;
    // the scene pass's dependencies (render pass compatibility: the pipelines are made for the
    // scene pass); the copy's own barriers order the backdrop and the loads
    VkSubpassDependency dependencies[2];
    vk_pass_dependencies(dependencies);
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = dependencies;
    return vk_check(vkCreateRenderPass(vk.device, &info, nullptr, &vk.resume_pass),
                    "create resume pass");
}

// the scene's 640x480 screen (stretched, or fitted 4:3 with bars) over the whole of `to`, which
// starts and ends sampled: the programs address it in the original's 640x480 texels
void copy_screen(RendererState& vk, VkCommandBuffer command, const GpuImage& to) {
    const float tw = static_cast<float>(vk.scene.width), th = static_cast<float>(vk.scene.height);
    float sx = tw / 640.0f, sy = th / 480.0f, ox = 0, oy = 0;
    if (vk.options.fit_43) {
        sx = sy = std::min(sx, sy);
        ox = (tw - 640.0f * sx) * 0.5f;
        oy = (th - 480.0f * sy) * 0.5f;
    }
    VkImageBlit blit{};
    blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {static_cast<int>(ox), static_cast<int>(oy), 0};
    blit.srcOffsets[1] = {static_cast<int>(ox + 640.0f * sx), static_cast<int>(oy + 480.0f * sy),
                          1};
    blit.dstOffsets[1] = {to.width, to.height, 1};
    vk_barrier(command, vk.scene.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vk_barrier(command, to.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdBlitImage(command, vk.scene.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, to.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    vk_barrier(command, to.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_barrier(command, vk.scene.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

} // namespace

bool vk_create_backdrop(RendererState& vk) {
    if (!vk.resume_pass && !make_resume_pass(vk)) {
        return false;
    }
    if (!vk_create_image(vk, vk.scene.width, vk.scene.height, 1, colour_format,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, vk.backdrop)) {
        return false;
    }
    if (!vk_create_image(vk, vk.scene.width, vk.scene.height, 1, colour_format,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, vk.scene_last)) {
        return false;
    }
    // sampled before any copy reaches them: start them in the sampled layout, the big screens
    // black until a frame with 3D was drawn
    VkCommandBuffer once = vk_begin_once(vk);
    vk_barrier(once, vk.backdrop.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_barrier(once, vk.scene_last.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkClearColorValue black{};
    const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(once, vk.scene_last.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black,
                         1, &all);
    vk_barrier(once, vk.scene_last.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!vk_end_once(vk, once)) {
        return false;
    }
    VkImageView views[2] = {vk.scene.view, vk.depth.view};
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = vk.resume_pass;
    framebuffer.attachmentCount = 2;
    framebuffer.pAttachments = views;
    framebuffer.width = static_cast<std::uint32_t>(vk.scene.width);
    framebuffer.height = static_cast<std::uint32_t>(vk.scene.height);
    framebuffer.layers = 1;
    return vk_check(vkCreateFramebuffer(vk.device, &framebuffer, nullptr, &vk.resume_framebuffer),
                    "create resume framebuffer");
}

void vk_destroy_backdrop(RendererState& vk) {
    if (vk.resume_framebuffer) {
        vkDestroyFramebuffer(vk.device, vk.resume_framebuffer, nullptr);
        vk.resume_framebuffer = VK_NULL_HANDLE;
    }
    vk_destroy_image(vk, vk.backdrop);
    vk_destroy_image(vk, vk.scene_last);
}

void vk_record_backdrop(RendererState& vk, VkCommandBuffer command) {
    vkCmdEndRenderPass(command);
    copy_screen(vk, command, vk.backdrop);
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = vk.resume_pass;
    pass.framebuffer = vk.resume_framebuffer;
    pass.renderArea.extent = {static_cast<std::uint32_t>(vk.scene.width),
                              static_cast<std::uint32_t>(vk.scene.height)};
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
}

void vk_record_scene_last(RendererState& vk, VkCommandBuffer command) {
    if (vk.scene_last.image) {
        copy_screen(vk, command, vk.scene_last);
    }
}

void vk_record_capture(RendererState& vk, VkCommandBuffer command) {
    if (!vk.capture_on || vk.capture_texture < 0 ||
        vk.capture_texture >= static_cast<int>(vk.textures.size())) {
        return;
    }
    const GpuImage& capture = vk.textures[static_cast<std::size_t>(vk.capture_texture)];
    if (!capture.image) {
        return;
    }
    VkImageBlit blit{};
    blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {vk.final.width, vk.final.height, 1};
    blit.dstOffsets[1] = {capture.width, capture.height, 1};
    vk_barrier(command, vk.final.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vk_barrier(command, capture.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdBlitImage(command, vk.final.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capture.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    vk_barrier(command, capture.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_barrier(command, vk.final.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

int renderer_capture_texture(Renderer& renderer) {
    RendererState& vk = *renderer.vk;
    if (vk.capture_texture >= 0 || vk.final.width <= 0) {
        return vk.capture_texture;
    }
    GpuImage image;
    if (!vk_create_image(vk, vk.final.width, vk.final.height, 1, vk.final.format,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, image)) {
        return -1;
    }
    VkCommandBuffer command = vk_upload_command(vk);
    vk_barrier(command, image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // kept for the run, whoever is uploading now
    vk.capture_texture = vk_keep_texture(vk, image, 0);
    return vk.capture_texture;
}

void renderer_capture_frames(Renderer& renderer, bool on) {
    renderer.vk->capture_on = on;
}
