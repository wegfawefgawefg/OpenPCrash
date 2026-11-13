#include "render/vk_internal.hpp"
#include "render/vk_sprites.hpp"

#include <algorithm>
#include <cstring>

namespace {

VkSamplerAddressMode address_mode(Address address) {
    return address == Address::Wrap     ? VK_SAMPLER_ADDRESS_MODE_REPEAT
           : address == Address::Mirror ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                                        : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

// copies bytes into a new device-local buffer through a staging one
bool upload_buffer(RendererState& vk, const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                   GpuBuffer& out) {
    GpuBuffer staging;
    if (!vk_create_buffer(
            vk, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging)) {
        return false;
    }
    void* mapped = nullptr;
    vkMapMemory(vk.device, staging.memory, 0, size, 0, &mapped);
    std::memcpy(mapped, data, size);
    vkUnmapMemory(vk.device, staging.memory);
    bool ok = vk_create_buffer(vk, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, out);
    if (ok) {
        VkCommandBuffer command = vk_upload_command(vk);
        VkBufferCopy copy{0, 0, size};
        vkCmdCopyBuffer(command, staging.buffer, out.buffer, 1, &copy);
    }
    vk_upload_keep(vk, staging);
    return ok;
}

// the next mip down: a 2x2 box, an odd edge's last texel kept
std::vector<std::uint8_t> half_size(const std::vector<std::uint8_t>& rgba, int width, int height,
                                    int& out_width, int& out_height) {
    out_width = width > 1 ? width / 2 : 1;
    out_height = height > 1 ? height / 2 : 1;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(out_width) * out_height * 4);
    for (int y = 0; y < out_height; ++y) {
        for (int x = 0; x < out_width; ++x) {
            for (int c = 0; c < 4; ++c) {
                int sum = 0;
                for (int k = 0; k < 4; ++k) {
                    const int sx = std::min(x * 2 + (k & 1), width - 1);
                    const int sy = std::min(y * 2 + (k >> 1), height - 1);
                    sum += rgba[(static_cast<std::size_t>(sy) * width + sx) * 4 + c];
                }
                out[(static_cast<std::size_t>(y) * out_width + x) * 4 + c] =
                    static_cast<std::uint8_t>((sum + 2) / 4);
            }
        }
    }
    return out;
}

// a handle for a new mesh or texture: a freed slot first, else a new one
// (its owner noted: renderer_set_owner)
template <class T>
int keep(std::vector<T>& list, std::vector<int>& free, std::vector<int>& owners, int owner,
         const T& value) {
    int slot = -1;
    if (!free.empty()) {
        slot = free.back();
        free.pop_back();
        list[static_cast<std::size_t>(slot)] = value;
    } else {
        list.push_back(value);
        slot = static_cast<int>(list.size()) - 1;
    }
    owners.resize(list.size(), 0);
    owners[static_cast<std::size_t>(slot)] = owner;
    return slot;
}

} // namespace

void renderer_free_mesh(Renderer& renderer, int mesh) {
    RendererState& vk = *renderer.vk;
    if (mesh < 0 || mesh >= static_cast<int>(vk.meshes.size()) ||
        !vk.meshes[static_cast<std::size_t>(mesh)].vertices.buffer) {
        return;
    }
    vk.freed_meshes.push_back(vk.meshes[static_cast<std::size_t>(mesh)]);
    vk.meshes[static_cast<std::size_t>(mesh)] = {};
    vk.free_meshes.push_back(mesh);
}

void renderer_free_texture(Renderer& renderer, int texture) {
    RendererState& vk = *renderer.vk;
    // the white texture stays
    if (texture <= texture_white || texture >= static_cast<int>(vk.textures.size()) ||
        !vk.textures[static_cast<std::size_t>(texture)].image) {
        return;
    }
    vk.freed_textures.push_back(vk.textures[static_cast<std::size_t>(texture)]);
    vk.textures[static_cast<std::size_t>(texture)] = {};
    vk.free_textures.push_back(texture);
}

void renderer_set_owner(Renderer& renderer, int owner) {
    renderer.vk->owner = owner;
}

void renderer_free_owner(Renderer& renderer, int owner) {
    RendererState& vk = *renderer.vk;
    if (owner == 0) {
        return;
    }
    for (std::size_t i = 0; i < vk.mesh_owner.size(); ++i) {
        if (vk.mesh_owner[i] == owner) {
            renderer_free_mesh(renderer, static_cast<int>(i));
            vk.mesh_owner[i] = 0;
        }
    }
    for (std::size_t i = 0; i < vk.texture_owner.size(); ++i) {
        if (vk.texture_owner[i] == owner) {
            renderer_free_texture(renderer, static_cast<int>(i));
            vk.texture_owner[i] = 0;
        }
    }
}

void vk_release_freed(RendererState& vk) {
    for (GpuMesh& mesh : vk.freed_meshes) {
        vk_destroy_buffer(vk, mesh.vertices);
        vk_destroy_buffer(vk, mesh.indices);
    }
    vk.freed_meshes.clear();
    if (!vk.freed_textures.empty()) {
        for (GpuImage& image : vk.freed_textures) {
            vk_destroy_image(vk, image);
        }
        vk.freed_textures.clear();
        vk_reset_descriptors(vk);
    }
}

RendererMemory renderer_memory(const Renderer& renderer) {
    const RendererState& vk = *renderer.vk;
    RendererMemory out;
    for (const GpuMesh& mesh : vk.meshes) {
        if (mesh.vertices.buffer) {
            ++out.meshes;
            out.mesh_bytes += mesh.vertices.size + mesh.indices.size;
        }
    }
    for (const GpuImage& image : vk.textures) {
        if (image.image) {
            ++out.textures;
            // the top level and its mips, as 4 bytes a texel at most (block textures are less)
            out.texture_texels += static_cast<std::uint64_t>(image.width) * image.height * 4 / 3;
        }
    }
    out.slots = static_cast<int>(vk.meshes.size() + vk.textures.size());
    return out;
}

int renderer_upload_mesh(Renderer& renderer, const std::vector<MeshVertex>& vertices,
                         const std::vector<std::uint16_t>& indices) {
    RendererState& vk = *renderer.vk;
    GpuMesh mesh;
    if (vertices.empty() ||
        !upload_buffer(vk, vertices.data(), vertices.size() * sizeof(MeshVertex),
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertices)) {
        return -1;
    }
    if (!indices.empty() && !upload_buffer(vk, indices.data(), indices.size() * 2,
                                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.indices)) {
        vk_destroy_buffer(vk, mesh.vertices);
        return -1;
    }
    return keep(vk.meshes, vk.free_meshes, vk.mesh_owner, vk.owner, mesh);
}

bool renderer_update_mesh(Renderer& renderer, int mesh, const std::vector<MeshVertex>& vertices) {
    RendererState& vk = *renderer.vk;
    if (mesh < 0 || mesh >= static_cast<int>(vk.meshes.size())) {
        return false;
    }
    GpuBuffer& target = vk.meshes[mesh].vertices;
    const VkDeviceSize size = vertices.size() * sizeof(MeshVertex);
    if (size == 0 || size > target.size) {
        return false;
    }
    GpuBuffer staging;
    if (!vk_create_buffer(
            vk, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging)) {
        return false;
    }
    void* mapped = nullptr;
    vkMapMemory(vk.device, staging.memory, 0, size, 0, &mapped);
    std::memcpy(mapped, vertices.data(), size);
    vkUnmapMemory(vk.device, staging.memory);
    // recorded with the pending uploads, which run after the frame in flight is done and before
    // the next one draws (vk_flush_uploads)
    VkCommandBuffer command = vk_upload_command(vk);
    VkBufferCopy copy{0, 0, size};
    vkCmdCopyBuffer(command, staging.buffer, target.buffer, 1, &copy);
    vk_upload_keep(vk, staging);
    return true;
}

int renderer_upload_texture(Renderer& renderer, int width, int height,
                            const std::vector<std::uint8_t>& rgba) {
    RendererState& vk = *renderer.vk;
    // the mip chain, made here (the converted PNGs hold the top level only)
    std::vector<std::vector<std::uint8_t>> levels{rgba};
    std::vector<std::pair<int, int>> sizes{{width, height}};
    while (sizes.back().first > 1 || sizes.back().second > 1) {
        int w = 0, h = 0;
        levels.push_back(half_size(levels.back(), sizes.back().first, sizes.back().second, w, h));
        sizes.push_back({w, h});
    }
    std::vector<std::uint8_t> packed;
    std::vector<VkDeviceSize> offsets;
    for (const std::vector<std::uint8_t>& level : levels) {
        offsets.push_back(packed.size());
        packed.insert(packed.end(), level.begin(), level.end());
    }

    GpuImage image;
    const int level_count = static_cast<int>(levels.size());
    if (!vk_create_image(vk, width, height, level_count, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, image)) {
        return -1;
    }
    GpuBuffer staging;
    if (!vk_create_buffer(
            vk, packed.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging)) {
        vk_destroy_image(vk, image);
        return -1;
    }
    void* mapped = nullptr;
    vkMapMemory(vk.device, staging.memory, 0, packed.size(), 0, &mapped);
    std::memcpy(mapped, packed.data(), packed.size());
    vkUnmapMemory(vk.device, staging.memory);
    VkCommandBuffer command = vk_upload_command(vk);
    vk_barrier(command, image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, level_count);
    std::vector<VkBufferImageCopy> copies;
    for (int level = 0; level < level_count; ++level) {
        VkBufferImageCopy copy{};
        copy.bufferOffset = offsets[level];
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<std::uint32_t>(level), 0,
                                 1};
        copy.imageExtent = {static_cast<std::uint32_t>(sizes[level].first),
                            static_cast<std::uint32_t>(sizes[level].second), 1};
        copies.push_back(copy);
    }
    vkCmdCopyBufferToImage(command, staging.buffer, image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<std::uint32_t>(copies.size()), copies.data());
    vk_barrier(command, image.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               level_count);
    vk_upload_keep(vk, staging);
    return keep(vk.textures, vk.free_textures, vk.texture_owner, vk.owner, image);
}

int vk_keep_texture(RendererState& vk, const GpuImage& image, int owner) {
    return keep(vk.textures, vk.free_textures, vk.texture_owner, owner, image);
}

int renderer_upload_blocks(Renderer& renderer, BlockFormat format, int width, int height,
                           int levels, const void* blocks, std::size_t size) {
    RendererState& vk = *renderer.vk;
    const VkFormat formats[] = {VK_FORMAT_BC1_RGB_UNORM_BLOCK, VK_FORMAT_BC3_UNORM_BLOCK,
                                VK_FORMAT_BC7_UNORM_BLOCK};
    const VkDeviceSize block_bytes = format == BlockFormat::bc1 ? 8 : 16;
    // each level's offset in the blocks, checked against their size
    std::vector<VkBufferImageCopy> copies;
    VkDeviceSize at = 0;
    for (int level = 0; level < levels; ++level) {
        const int w = std::max(width >> level, 1);
        const int h = std::max(height >> level, 1);
        VkBufferImageCopy copy{};
        copy.bufferOffset = at;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<std::uint32_t>(level), 0,
                                 1};
        copy.imageExtent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};
        copies.push_back(copy);
        at += static_cast<VkDeviceSize>((w + 3) / 4) * ((h + 3) / 4) * block_bytes;
    }
    if (levels < 1 || at > size) {
        return -1;
    }

    GpuImage image;
    if (!vk_create_image(vk, width, height, levels, formats[static_cast<int>(format)],
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, image)) {
        return -1;
    }
    GpuBuffer staging;
    if (!vk_create_buffer(
            vk, at, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging)) {
        vk_destroy_image(vk, image);
        return -1;
    }
    void* mapped = nullptr;
    vkMapMemory(vk.device, staging.memory, 0, at, 0, &mapped);
    std::memcpy(mapped, blocks, at);
    vkUnmapMemory(vk.device, staging.memory);
    VkCommandBuffer command = vk_upload_command(vk);
    vk_barrier(command, image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels);
    vkCmdCopyBufferToImage(command, staging.buffer, image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<std::uint32_t>(copies.size()), copies.data());
    vk_barrier(command, image.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               levels);
    vk_upload_keep(vk, staging);
    return keep(vk.textures, vk.free_textures, vk.texture_owner, vk.owner, image);
}

// the sprite layer's own textures (movies, the conversations' sheets): uploaded once, a
// movie's picture replaced each frame
namespace {

// the picture's pixels into level 0 through the renderer's pending uploads, which run before the
// next frame is recorded (after the last one has finished sampling the old picture)
bool copy_pixels(RendererState& vk, const GpuImage& image, VkImageLayout from,
                 const std::vector<std::uint8_t>& rgba) {
    const VkDeviceSize size = static_cast<VkDeviceSize>(image.width) * image.height * 4;
    GpuBuffer staging;
    if (rgba.size() < size ||
        !vk_create_mapped_buffer(vk, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging)) {
        return false;
    }
    std::memcpy(staging.mapped, rgba.data(), size);
    VkCommandBuffer command = vk_upload_command(vk);
    vk_barrier(command, image.image, VK_IMAGE_ASPECT_COLOR_BIT, from,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<std::uint32_t>(image.width),
                        static_cast<std::uint32_t>(image.height), 1};
    vkCmdCopyBufferToImage(command, staging.buffer, image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    vk_barrier(command, image.image, VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_upload_keep(vk, staging);
    return true;
}

} // namespace

int renderer_upload_sprite_texture(Renderer& renderer, int width, int height,
                                   const std::vector<std::uint8_t>& rgba) {
    RendererState& vk = *renderer.vk;
    GpuImage image;
    if (!vk_create_image(vk, width, height, 1, VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, image)) {
        return -1;
    }
    if (!copy_pixels(vk, image, VK_IMAGE_LAYOUT_UNDEFINED, rgba)) {
        vk_destroy_image(vk, image);
        return -1;
    }
    vk.textures.push_back(image);
    return static_cast<int>(vk.textures.size()) - 1;
}

bool renderer_update_sprite_texture(Renderer& renderer, int texture,
                                    const std::vector<std::uint8_t>& rgba) {
    RendererState& vk = *renderer.vk;
    if (texture < 0 || texture >= static_cast<int>(vk.textures.size())) {
        return false;
    }
    return copy_pixels(vk, vk.textures[static_cast<std::size_t>(texture)],
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, rgba);
}

// the samplers by stage address and filter, and the four-stage texture sets by their textures
// and samplers (cached until the targets change: vk_reset_descriptors)
VkSampler vk_sampler(RendererState& vk, Address u, Address v, bool point) {
    const int key = (static_cast<int>(u) * 3 + static_cast<int>(v)) * 2 + (point ? 1 : 0);
    const auto found = vk.samplers.find(key);
    if (found != vk.samplers.end()) {
        return found->second;
    }
    // linear with linear mips as the original sets every mesh stage; point for the targets read
    // texel by texel
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = info.minFilter = point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    info.mipmapMode = point ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = address_mode(u);
    info.addressModeV = address_mode(v);
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.maxLod = point ? 0.0f : VK_LOD_CLAMP_NONE;
    // the PC's texture filtering setting (RenderOptions::anisotropy) on the filtered samplers
    if (!point && vk.options.anisotropy > 1 && vk.max_anisotropy > 1.0f) {
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy =
            std::min(static_cast<float>(vk.options.anisotropy), vk.max_anisotropy);
    }
    VkSampler sampler = VK_NULL_HANDLE;
    vk_check(vkCreateSampler(vk.device, &info, nullptr, &sampler), "sampler");
    vk.samplers[key] = sampler;
    return sampler;
}

VkDescriptorSet vk_texture_set(RendererState& vk, const StageTexture* stages) {
    std::vector<int> key(8);
    for (int s = 0; s < 4; ++s) {
        key[s * 2] = stages[s].texture;
        key[s * 2 + 1] = (static_cast<int>(stages[s].u) * 3 + static_cast<int>(stages[s].v)) * 2 +
                         (stages[s].point ? 1 : 0);
    }
    const auto found = vk.texture_sets.find(key);
    if (found != vk.texture_sets.end()) {
        return found->second;
    }
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = vk.descriptor_pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &vk.texture_layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!vk_check(vkAllocateDescriptorSets(vk.device, &allocate, &set), "texture set")) {
        return VK_NULL_HANDLE;
    }
    VkDescriptorImageInfo images[4];
    VkWriteDescriptorSet writes[4]{};
    for (int s = 0; s < 4; ++s) {
        const StageTexture& stage = stages[s];
        VkImageView view = vk.textures[texture_white].view;
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bool point = stage.point;
        switch (stage.texture) {
        case texture_scene:
            view = vk.scene.view;
            break;
        case texture_backdrop:
            view = vk.backdrop.view;
            break;
        case texture_scene_last:
            view = vk.scene_last.view;
            break;
        case texture_blur320:
            view = vk.blur320.view;
            break;
        case texture_blur160:
            view = vk.blur160.view;
            break;
        case texture_final:
            view = vk.final.view;
            break;
        case texture_post:
            view = vk.post.view;
            break;
        case texture_wave:
            view = vk.wave.view;
            break;
        case texture_ramp:
            view = vk.ramp.view;
            break;
        case texture_depth:
            // gathered (depth_byte.glsl), so nearest: D24 needn't filter
            view = vk.depth.depth_view;
            layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            point = true;
            break;
        default:
            if (stage.texture >= 0 && stage.texture < static_cast<int>(vk.textures.size()) &&
                vk.textures[stage.texture].view) {
                view = vk.textures[stage.texture].view;
            }
            break;
        }
        images[s] = {vk_sampler(vk, stage.u, stage.v, point), view, layout};
        writes[s].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[s].dstSet = set;
        writes[s].dstBinding = static_cast<std::uint32_t>(s);
        writes[s].descriptorCount = 1;
        writes[s].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[s].pImageInfo = &images[s];
    }
    vkUpdateDescriptorSets(vk.device, 4, writes, 0, nullptr);
    vk.texture_sets[key] = set;
    return set;
}
