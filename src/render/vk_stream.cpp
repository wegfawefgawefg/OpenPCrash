// The frame's streamed updates (docs/performance.md): the vertices the game rewrites as it draws
// (the stage's scrolling textures and fading lamps, the trails, the sky craft's screens) and the
// movie's picture go through one mapped ring into their device-local homes, copied by the frame's
// own commands before anything draws. They took a staging buffer of their own each (allocated and
// freed every frame) and a submit and a wait for the queue to go idle before the frame. The ring
// has two halves: the game fills one for the next frame while the GPU may still be copying out of
// the other. An update that doesn't fit takes the old way (vk_upload_command).
#include "render/vk_internal.hpp"

#include <algorithm>
#include <cstring>

namespace {

constexpr VkDeviceSize stream_half_size = 4u << 20; // a movie picture of 640x480 is 1.2 MB

// the next aligned offset with room for size bytes in this frame's half, or false
bool stream_place(RendererState& vk, VkDeviceSize size, VkDeviceSize& at) {
    if (!vk.stream.mapped) {
        return false;
    }
    const VkDeviceSize start = (vk.stream_used + 15) / 16 * 16;
    if (start + size > stream_half_size) {
        return false;
    }
    at = static_cast<VkDeviceSize>(vk.stream_half) * stream_half_size + start;
    vk.stream_used = start + size;
    return true;
}

} // namespace

bool vk_create_stream(RendererState& vk) {
    return vk_create_mapped_buffer(vk, stream_half_size * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   vk.stream);
}

void vk_destroy_stream(RendererState& vk) {
    vk_destroy_buffer(vk, vk.stream);
    vk.stream_copies.clear();
}

bool vk_stream_buffer(RendererState& vk, VkBuffer to, const void* data, VkDeviceSize size) {
    VkDeviceSize at = 0;
    if (!stream_place(vk, size, at)) {
        return false;
    }
    std::memcpy(static_cast<std::uint8_t*>(vk.stream.mapped) + at, data, size);
    StreamCopy copy;
    copy.buffer = to;
    copy.from = at;
    copy.size = size;
    vk.stream_copies.push_back(copy);
    return true;
}

bool vk_stream_image(RendererState& vk, const GpuImage& image, const void* rgba) {
    const VkDeviceSize size = static_cast<VkDeviceSize>(image.width) * image.height * 4;
    VkDeviceSize at = 0;
    if (!stream_place(vk, size, at)) {
        return false;
    }
    std::memcpy(static_cast<std::uint8_t*>(vk.stream.mapped) + at, rgba, size);
    StreamCopy copy;
    copy.image = image.image;
    copy.width = static_cast<std::uint32_t>(image.width);
    copy.height = static_cast<std::uint32_t>(image.height);
    copy.from = at;
    copy.size = size;
    vk.stream_copies.push_back(copy);
    return true;
}

void vk_stream_forget(RendererState& vk, VkBuffer buffer, VkImage image) {
    vk.stream_copies.erase(std::remove_if(vk.stream_copies.begin(), vk.stream_copies.end(),
                                          [&](const StreamCopy& copy) {
                                              return (buffer && copy.buffer == buffer) ||
                                                     (image && copy.image == image);
                                          }),
                           vk.stream_copies.end());
}

void vk_record_stream(RendererState& vk, VkCommandBuffer command) {
    if (!vk.stream_copies.empty()) {
        // the last frame's reads of the vertices done before they are written
        VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
        for (const StreamCopy& copy : vk.stream_copies) {
            if (copy.buffer) {
                const VkBufferCopy region{copy.from, 0, copy.size};
                vkCmdCopyBuffer(command, vk.stream.buffer, copy.buffer, 1, &region);
                continue;
            }
            vk_barrier(command, copy.image, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy region{};
            region.bufferOffset = copy.from;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {copy.width, copy.height, 1};
            vkCmdCopyBufferToImage(command, vk.stream.buffer, copy.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            vk_barrier(command, copy.image, VK_IMAGE_ASPECT_COLOR_BIT,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        // the new vertices written before this frame's draws read them
        VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        after.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &after, 0, nullptr, 0,
                             nullptr);
        vk.stream_copies.clear();
    }
    // the other half for the next frame: the frame that copied out of it is done (its fence)
    vk.stream_half ^= 1;
    vk.stream_used = 0;
}
