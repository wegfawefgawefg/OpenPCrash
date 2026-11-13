#pragma once
// A stage's scenery on screen: its model set loaded, and each frame the objects stage_render_lods
// 0x6E127 would submit for the camera (zone, LOD by distance), queued by pass.
#include "assets/model_sets.hpp"
#include "assets/manifest.hpp"
#include "assets/stage_tables.hpp"
#include "cook/pack_library.hpp"
#include "render/scene_draw.hpp"
#include "stage/stage_animate.hpp"

#include <string>
#include <vector>

struct StageScene {
    int selector = -1;
    const StageInfo* info = nullptr;
    float lod_mid = 120, lod_far = 200;
    ModelSet models;
    GpuModelSet gpu;
    int texture_set = -1;  // the model set's texture set
    int texture_base = 0;  // its first texture slot (ModelRoot.base)
    StageAnimation animation; // presentation: stepped per world step, drawn interpolated
};

// from the stage's pack (stage_N), closed again once it's on the GPU
bool load_stage_scene(StageScene& scene, const StageTables& tables, int selector,
                      const AssetManifest& manifest, PackLibrary& packs, Renderer& renderer,
                      TextureCache& cache, std::string& error);

// the objects for an eye position (zone, LOD, animation), queued with view 0's matrix (their
// sort keys); the layer is the pass, 7 the shadow volumes. stage_animation_frame first
void stage_draw_items(const StageScene& scene, const float eye[3], const Mat4& view,
                      std::vector<PassItem>& out);
