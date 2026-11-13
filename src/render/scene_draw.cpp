#include "render/scene_draw.hpp"

#include "cook/pack.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

int texture_for(Renderer& renderer, TextureCache& cache, const std::string& path) {
    if (path.empty()) {
        return -1;
    }
    const auto found = cache.loaded.find(path);
    if (found != cache.loaded.end()) {
        return found->second;
    }
    // the cooked texture, BC blocks with their mips, straight from the pack
    const PackEntry* entry = cache.pack ? pack_find(*cache.pack, PackKind::texture, path) : nullptr;
    int handle = -1;
    if (entry) {
        const BlockFormat format = entry->format == TextureFormat::bc1   ? BlockFormat::bc1
                                   : entry->format == TextureFormat::bc3 ? BlockFormat::bc3
                                                                         : BlockFormat::bc7;
        handle = renderer_upload_blocks(
            renderer, format, static_cast<int>(entry->width), static_cast<int>(entry->height),
            static_cast<int>(entry->levels), pack_blob(*cache.pack, *entry), entry->size);
    }
    if (handle < 0) {
        std::fprintf(stderr, "texture: no %s in pack %s\n", path.c_str(),
                     cache.pack ? cache.pack->name.c_str() : "(none)");
    }
    cache.loaded[path] = handle;
    return handle;
}

void build_texture_slots(TextureSlots& slots, const AssetManifest& manifest,
                         const std::vector<int>& preferred_sets) {
    slots.pictures.clear();
    auto place = [&](const ManifestEntry& entry) {
        for (int k = 0; k < entry.slot_count; ++k) {
            char name[16];
            std::snprintf(name, sizeof name, "/%02d.png", k);
            slots.pictures.emplace(entry.first_slot + k, entry.file + name);
        }
    };
    for (int id : preferred_sets) {
        const auto found = manifest.assets.find(id);
        if (found != manifest.assets.end() && found->second.kind == "texture-set") {
            place(found->second);
        }
    }
    for (const auto& [id, entry] : manifest.assets) {
        if (entry.kind == "texture-set" && entry.first_slot >= 0) {
            place(entry);
        }
    }
}

int slot_texture(Renderer& renderer, TextureCache& cache, const TextureSlots& slots, int slot) {
    const auto found = slots.pictures.find(slot);
    return found == slots.pictures.end() ? -1 : texture_for(renderer, cache, found->second);
}

void load_slot_textures(Renderer& renderer, TextureCache& cache, const TextureSlots& slots,
                        const Pack& pack) {
    const Pack* previous = cache.pack;
    cache.pack = &pack;
    for (const auto& [slot, path] : slots.pictures) {
        if (!cache.loaded.count(path) && pack_find(pack, PackKind::texture, path)) {
            texture_for(renderer, cache, path);
        }
    }
    cache.pack = previous;
}

bool upload_model_set(Renderer& renderer, TextureCache& cache, const ModelSet& set,
                      GpuModelSet& out) {
    out = GpuModelSet{};
    for (const ModelObject& object : set.objects) {
        out.object_mesh.push_back(
            object.vertices.empty()
                ? -1
                : renderer_upload_mesh(renderer, object.vertices, object.indices));
        std::vector<int> textures;
        for (const ModelSection& section : object.sections) {
            textures.push_back(texture_for(renderer, cache, section.picture));
        }
        out.section_texture.push_back(textures);
    }
    return true;
}

// decomp: model_node_local_matrix 0xAA186
Mat4 node_local_matrix(const ModelNode& node) {
    Mat4 m = mat_scaling(node.scale[0], node.scale[1], node.scale[2]);
    m = mat_multiply(m, mat_rotation_x(node.euler[0]));
    m = mat_multiply(m, mat_rotation_y(node.euler[1]));
    m = mat_multiply(m, mat_rotation_z(node.euler[2]));
    return mat_multiply(m,
                        mat_translation(node.translate[0], node.translate[1], node.translate[2]));
}

MeshRecord section_draw(const ModelSection& section, int mesh, int texture, const Mat4& world,
                        int pass, std::uint32_t mesh_flags) {
    (void)pass;
    MeshRecord record;
    record.sections = &section;
    record.section_count = 1;
    record.mesh = mesh;
    record.paint = texture;
    record.world = world;
    record.flags = mesh_flags;
    return record;
}

CameraMatrices camera_matrices(const float eye[3], float yaw, float pitch, float aspect,
                               float fov, float roll) {
    // the camera's frame in the port's right-handed world: forward, right = forward x up, up =
    // right x forward, which is the original's left-handed view space seen through the z mirror
    const Vec3f forward{std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                        std::cos(yaw) * std::cos(pitch)};
    const Vec3f right = vec_normalize(vec_cross(forward, Vec3f{0, 1, 0}));
    const Vec3f up = vec_cross(right, forward);
    CameraMatrices out;
    out.eye = Vec3f{eye[0], eye[1], eye[2]};
    Mat4& view = out.view;
    view.m[0][0] = right.x;
    view.m[1][0] = right.y;
    view.m[2][0] = right.z;
    view.m[0][1] = up.x;
    view.m[1][1] = up.y;
    view.m[2][1] = up.z;
    view.m[0][2] = forward.x;
    view.m[1][2] = forward.y;
    view.m[2][2] = forward.z;
    view.m[3][0] = -vec_dot(right, out.eye);
    view.m[3][1] = -vec_dot(up, out.eye);
    view.m[3][2] = -vec_dot(forward, out.eye);
    if (roll != 0.0f) {
        view = mat_multiply(view, mat_rotation_z(roll));
    }

    // D3DXMatrixPerspectiveFovLH; the renderer's negative viewport height turns y over
    const float near_z = 0.1f, far_z = 10000.0f;
    const float y_scale = 1.0f / std::tan(fov * 3.14159265f / 180.0f / 2.0f);
    Mat4& projection = out.projection;
    projection.m[0][0] = y_scale / aspect;
    projection.m[1][1] = y_scale;
    projection.m[2][2] = far_z / (far_z - near_z);
    projection.m[2][3] = 1;
    projection.m[3][2] = -near_z * far_z / (far_z - near_z);
    projection.m[3][3] = 0;
    return out;
}
