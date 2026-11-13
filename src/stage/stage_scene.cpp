#include "stage/stage_scene.hpp"

#include "render/mesh_flags.hpp"
#include "render/mesh_queue.hpp"

#include <cmath>

bool load_stage_scene(StageScene& scene, const StageTables& tables, int selector,
                      const AssetManifest& manifest, PackLibrary& packs, Renderer& renderer,
                      TextureCache& cache, std::string& error) {
    if (selector < 0 || selector >= 8) {
        error = "no stage selector " + std::to_string(selector);
        return false;
    }
    scene.selector = selector;
    scene.info = &tables.stages[selector];
    scene.lod_mid = tables.lod_mid;
    scene.lod_far = tables.lod_far;
    const auto entry = manifest.assets.find(scene.info->model_set);
    if (entry == manifest.assets.end()) {
        error = "the manifest has no model set " + std::to_string(scene.info->model_set);
        return false;
    }
    const std::string name = stage_pack_name(selector);
    const Pack* pack = pack_open(packs, name, error);
    if (!pack || !load_model_set(*pack, scene.info->model_set, scene.models, error)) {
        pack_close(packs, name);
        return false;
    }
    scene.models.first_slot = entry->second.first_slot;
    // a section's tex[s] counts from its texture set's first slot (resource_load_type0's base)
    scene.texture_set = entry->second.texture_set;
    const auto set = manifest.assets.find(scene.texture_set);
    scene.texture_base = set != manifest.assets.end() ? set->second.first_slot : 0;
    cache.pack = pack;
    const bool ok = upload_model_set(renderer, cache, scene.models, scene.gpu);
    cache.pack = nullptr;
    pack_close(packs, name);
    if (ok) {
        stage_animation_load(scene.animation, *scene.info, scene.models, scene.gpu, renderer);
    }
    return ok;
}

void stage_draw_items(const StageScene& scene, const float eye[3], const Mat4& view,
                      std::vector<PassItem>& out) {
    if (!scene.info) {
        return;
    }
    // the zone of the eye picks the visible rows (stage_zone_functions; VS draws zone 0)
    const int zone = stage_zone_at(scene.selector, eye);
    for (const StageObject& row : scene.info->objects) {
        if (!row.zones[zone]) {
            continue;
        }
        // LOD by the eye's distance to the near model's centre: far from 200, mid from 120
        auto object_of = [&](int slot) { return slot - scene.models.first_slot; };
        int slot = row.lods[0];
        const int near_object = object_of(row.model);
        if (near_object >= 0 && near_object < static_cast<int>(scene.models.objects.size())) {
            const float* c = scene.models.objects[near_object].centre;
            const float length =
                std::sqrt((eye[0] - c[0]) * (eye[0] - c[0]) + (eye[1] - c[1]) * (eye[1] - c[1]) +
                          (eye[2] - c[2]) * (eye[2] - c[2]));
            if (length >= scene.lod_far) {
                slot = row.lods[2] != -1   ? row.lods[2]
                       : row.lods[1] != -1 ? row.lods[1]
                                           : row.lods[0];
            } else if (length >= scene.lod_mid) {
                slot = row.lods[1] != -1 ? row.lods[1] : row.lods[0];
            }
        }
        const int object = object_of(slot);
        if (object < 0 || object >= static_cast<int>(scene.models.objects.size())) {
            continue;
        }
        // stage_object_animate: kinds 1 and 2 turn about y at one and two times the sky
        // phase (the original's -phase; the port's y turn is negated), the rest draw their own
        // copy (stage_animate.cpp). model_render_submit, or model_render_submit_bit25 (no fog)
        // for submit 0; the root's flags start at 0 (model_root_defaults)
        Mat4 world = mat_identity();
        if (row.animate == 1 || row.animate == 2) {
            world = mat_rotation_y(scene.animation.sky_turn_drawn * static_cast<float>(row.animate));
        }
        std::uint32_t flags = row.submit ? 0 : mesh_no_fog;
        const StageAnimatedObject* animated = stage_animated(scene.animation, object);
        if (animated) {
            // kind 7 sets its flags outright, the others add theirs
            flags = animated->kind == 7 ? animated->mesh_flags : flags | animated->mesh_flags;
            queue_object(animated->drawn, animated->mesh, scene.texture_base, world, row.layer,
                         flags, view, out);
            continue;
        }
        queue_object(scene.models.objects[object], scene.gpu.object_mesh[object],
                     scene.texture_base, world, row.layer, flags, view, out);
    }
}
