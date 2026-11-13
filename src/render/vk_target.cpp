#include "render/vk_internal.hpp"

#include "perf_log.hpp"

#include <SDL3_image/SDL_image.h>
#include <algorithm>

namespace {

constexpr VkFormat colour_format = VK_FORMAT_R8G8B8A8_UNORM;

// v-sync on is FIFO (always there); off is mailbox where the surface has it (no tearing, no
// wait), else immediate
VkPresentModeKHR present_mode(RendererState& vk) {
    if (vk.options.vsync) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &count, modes.data());
    for (VkPresentModeKHR wanted : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
        if (std::find(modes.begin(), modes.end(), wanted) != modes.end()) {
            return wanted;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

const char* present_mode_name(VkPresentModeKHR mode) {
    switch (mode) {
    case VK_PRESENT_MODE_FIFO_KHR:
        return "fifo (v-sync)";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "mailbox";
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "immediate";
    default:
        return "other";
    }
}

bool make_swapchain(RendererState& vk, VkExtent2D extent) {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &capabilities);
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &count, formats.data());
    if (formats.empty()) {
        return SDL_SetError("no Vulkan surface formats");
    }
    VkSurfaceFormatKHR format = formats.front();
    for (const VkSurfaceFormatKHR& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
            format = f;
            break;
        }
    }
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = vk.surface;
    info.minImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount) {
        info.minImageCount = std::min(info.minImageCount, capabilities.maxImageCount);
    }
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR alpha :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR}) {
        if (capabilities.supportedCompositeAlpha & alpha) {
            info.compositeAlpha = alpha;
            break;
        }
    }
    info.presentMode = present_mode(vk);
    vk.present_mode = info.presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = vk.swapchain;
    VkSwapchainKHR next = VK_NULL_HANDLE;
    if (!vk_check(vkCreateSwapchainKHR(vk.device, &info, nullptr, &next), "create swapchain")) {
        return false;
    }
    if (vk.swapchain) {
        vkDestroySwapchainKHR(vk.device, vk.swapchain, nullptr);
    }
    vk.swapchain = next;
    vk.swap_format = format.format;
    vk.swap_extent = extent;
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &count, nullptr);
    vk.swap_images.resize(count);
    vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &count, vk.swap_images.data());
    // a present's semaphore an image
    while (vk.finished.size() < count) {
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore made = VK_NULL_HANDLE;
        if (!vk_check(vkCreateSemaphore(vk.device, &semaphore, nullptr, &made), "semaphore")) {
            return false;
        }
        vk.finished.push_back(made);
    }
    return true;
}

// the scene pass: colour and depth-stencil cleared, both kept for the composite to sample; the
// screen pass: one colour target drawn over whole
bool make_render_passes(RendererState& vk) {
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = colour_format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1] = attachments[0];
    attachments[1].format = vk.depth_format;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference colour{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colour;
    subpass.pDepthStencilAttachment = &depth;
    VkSubpassDependency dependencies[2];
    vk_pass_dependencies(dependencies);
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = dependencies;
    if (!vk_check(vkCreateRenderPass(vk.device, &info, nullptr, &vk.scene_pass),
                  "create scene pass")) {
        return false;
    }
    // pass 13 (late items): the final target as the 2D of pass 12 left it, over a depth cleared
    // again; compatible with the scene pass, so the meshes' pipelines draw in it
    VkAttachmentDescription late[2] = {attachments[0], attachments[1]};
    late[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    late[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    late[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    late[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    info.pAttachments = late;
    if (!vk_check(vkCreateRenderPass(vk.device, &info, nullptr, &vk.late_pass),
                  "create late pass")) {
        return false;
    }
    info.pAttachments = attachments;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    subpass.pDepthStencilAttachment = nullptr;
    info.attachmentCount = 1;
    return vk_check(vkCreateRenderPass(vk.device, &info, nullptr, &vk.screen_pass),
                    "create screen pass");
}

// D24S8 like the original's 0x224, D32 with stencil where a device can't sample D24
VkFormat pick_depth_format(RendererState& vk) {
    const VkFormatFeatureFlags needs =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    for (VkFormat format : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(vk.physical, format, &properties);
        if ((properties.optimalTilingFeatures & needs) == needs) {
            return format;
        }
    }
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
}

bool make_framebuffer(RendererState& vk, VkRenderPass pass, const GpuImage& colour,
                      const GpuImage* depth, VkFramebuffer& out) {
    VkImageView views[2] = {colour.view, depth ? depth->view : VK_NULL_HANDLE};
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = pass;
    framebuffer.attachmentCount = depth ? 2 : 1;
    framebuffer.pAttachments = views;
    framebuffer.width = static_cast<std::uint32_t>(colour.width);
    framebuffer.height = static_cast<std::uint32_t>(colour.height);
    framebuffer.layers = 1;
    return vk_check(vkCreateFramebuffer(vk.device, &framebuffer, nullptr, &out),
                    "create framebuffer");
}

bool make_colour_target(RendererState& vk, int width, int height, VkImageUsageFlags extra,
                        GpuImage& out) {
    return vk_create_image(vk, std::max(width, 1), std::max(height, 1), 1, colour_format,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                               extra,
                           VK_IMAGE_ASPECT_COLOR_BIT, out);
}

} // namespace

void vk_pass_dependencies(VkSubpassDependency (&out)[2]) {
    // in: what was sampled, copied or drawn before is done before this pass draws or loads
    out[0] = {};
    out[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    out[0].dstSubpass = 0;
    out[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    out[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    out[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    out[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // out: what is drawn is read by the next pass's shaders or copies
    out[1] = {};
    out[1].srcSubpass = 0;
    out[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    out[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    out[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    out[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    out[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
}

void vk_destroy_targets(RendererState& vk) {
    for (VkFramebuffer* framebuffer :
         {&vk.scene_framebuffer, &vk.blur320_framebuffer, &vk.blur160_framebuffer,
          &vk.final_framebuffer, &vk.output_framebuffer, &vk.late_framebuffer, &vk.post_framebuffer,
          &vk.post_late_framebuffer}) {
        if (*framebuffer) {
            vkDestroyFramebuffer(vk.device, *framebuffer, nullptr);
            *framebuffer = VK_NULL_HANDLE;
        }
    }
    for (GpuImage* image :
         {&vk.scene, &vk.depth, &vk.blur320, &vk.blur160, &vk.final, &vk.output, &vk.post}) {
        vk_destroy_image(vk, *image);
    }
    vk_destroy_backdrop(vk);
}

bool vk_fit_window(RendererState& vk, Renderer& renderer) {
    int window_w = 0, window_h = 0;
    SDL_GetWindowSizeInPixels(vk.window, &window_w, &window_h);
    window_w = std::max(window_w, 1);
    window_h = std::max(window_h, 1);
    // the targets at the render scale's share of the window (the output is scaled to the window
    // at the end), never under the original's 320x240 or past 8192
    const int scale = std::clamp(vk.options.render_scale, 25, 400);
    int width = std::clamp(window_w * scale / 100, std::min(window_w, 320), 8192);
    int height = std::clamp(window_h * scale / 100, std::min(window_h, 240), 8192);
    if (!vk.swap_stale && vk.scene.image && width == vk.scene.width &&
        height == vk.scene.height) {
        return true;
    }
    vkDeviceWaitIdle(vk.device);
    if (!vk.scene_pass) {
        vk.depth_format = pick_depth_format(vk);
        if (!make_render_passes(vk)) {
            return false;
        }
    }

    // the swapchain at the surface's size (the window's when the surface leaves it to us)
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &capabilities);
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent = {static_cast<std::uint32_t>(window_w), static_cast<std::uint32_t>(window_h)};
    }
    if (extent.width == 0 || extent.height == 0 || !make_swapchain(vk, extent)) {
        return false;
    }
    vk.swap_stale = false;

    // the targets at the window's size, the downsamples at a half and a quarter of it (the
    // original's 640x480, 320x240 and 160x120)
    vk_destroy_targets(vk);
    // (the scene and the final target also copy one to the other on a frame without 3D)
    if (!make_colour_target(vk, width, height, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, vk.scene) ||
        !vk_create_image(vk, width, height, 1, vk.depth_format,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, vk.depth) ||
        !make_colour_target(vk, width / 2, height / 2, 0, vk.blur320) ||
        !make_colour_target(vk, width / 4, height / 4, 0, vk.blur160) ||
        !make_colour_target(vk, width, height,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            vk.final) ||
        !make_colour_target(vk, width, height, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, vk.output) ||
        !make_colour_target(vk, width, height, 0, vk.post)) {
        return false;
    }
    // the depth aspect alone, for the dof to sample
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = vk.depth.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = vk.depth_format;
    view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (!vk_check(vkCreateImageView(vk.device, &view, nullptr, &vk.depth.depth_view),
                  "depth view") ||
        !make_framebuffer(vk, vk.scene_pass, vk.scene, &vk.depth, vk.scene_framebuffer) ||
        !make_framebuffer(vk, vk.screen_pass, vk.blur320, nullptr, vk.blur320_framebuffer) ||
        !make_framebuffer(vk, vk.screen_pass, vk.blur160, nullptr, vk.blur160_framebuffer) ||
        !make_framebuffer(vk, vk.screen_pass, vk.final, nullptr, vk.final_framebuffer) ||
        !make_framebuffer(vk, vk.screen_pass, vk.output, nullptr, vk.output_framebuffer) ||
        !make_framebuffer(vk, vk.late_pass, vk.final, &vk.depth, vk.late_framebuffer) ||
        !make_framebuffer(vk, vk.screen_pass, vk.post, nullptr, vk.post_framebuffer) ||
        !make_framebuffer(vk, vk.late_pass, vk.post, &vk.depth, vk.post_late_framebuffer) ||
        !vk_create_backdrop(vk)) {
        return false;
    }
    vk_reset_descriptors(vk);
    renderer.width = width;
    renderer.height = height;
    perf_swapchain(1, present_mode_name(vk.present_mode), static_cast<int>(vk.swap_images.size()),
                   static_cast<int>(extent.width), static_cast<int>(extent.height), width, height);
    return true;
}

void renderer_set_options(Renderer& renderer, const RenderOptions& options) {
    RendererState& vk = *renderer.vk;
    const RenderOptions old = vk.options;
    renderer.options = options;
    vk.options = options;
    if (old.vsync != options.vsync || old.render_scale != options.render_scale) {
        vk.swap_stale = true;
    }
    if (old.anisotropy != options.anisotropy) {
        // the samplers again, and every texture set that holds one
        vkDeviceWaitIdle(vk.device);
        for (auto& [key, sampler] : vk.samplers) {
            vkDestroySampler(vk.device, sampler, nullptr);
        }
        vk.samplers.clear();
        vk_reset_descriptors(vk);
    }
}

// the output's pixels to a PNG (it waits for the frame)
bool vk_save_capture(RendererState& vk, const std::string& path) {
    const VkDeviceSize size = static_cast<VkDeviceSize>(vk.output.width) * vk.output.height * 4;
    GpuBuffer readback;
    if (!vk_create_mapped_buffer(vk, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback)) {
        return false;
    }
    VkCommandBuffer command = vk_begin_once(vk);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<std::uint32_t>(vk.output.width),
                        static_cast<std::uint32_t>(vk.output.height), 1};
    vkCmdCopyImageToBuffer(command, vk.output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback.buffer, 1, &copy);
    bool ok = vk_end_once(vk, command);
    if (ok) {
        std::vector<std::uint8_t> pixels(static_cast<const std::uint8_t*>(readback.mapped),
                                         static_cast<const std::uint8_t*>(readback.mapped) + size);
        // opaque, the way the screen shows it
        for (std::size_t i = 3; i < pixels.size(); i += 4) {
            pixels[i] = 255;
        }
        SDL_Surface* surface =
            SDL_CreateSurfaceFrom(vk.output.width, vk.output.height, SDL_PIXELFORMAT_ABGR8888,
                                  pixels.data(), vk.output.width * 4);
        ok = surface && IMG_SavePNG(surface, path.c_str());
        SDL_DestroySurface(surface);
    }
    vk_destroy_buffer(vk, readback);
    return ok;
}
