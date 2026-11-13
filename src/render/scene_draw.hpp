#pragma once
// What the stage and the fighters share on their way to the renderer: model sets uploaded with
// their pictures, the texture table by slot, node matrices, the camera, and the queue record of a
// run of sections (the original's 0x90-byte Mesh, model_render_section_range) that
// src/render/mesh_draw.cpp turns into draws with the original's render states.
#include "assets/manifest.hpp"
#include "assets/model_sets.hpp"
#include "render/d3dx_math.hpp"
#include "render/renderer.hpp"

#include <map>
#include <string>
#include <vector>

struct Pack;

// texture key (its PNG's path under data/) -> renderer texture; keys not uploaded yet are found in
// the cooked pack being loaded
struct TextureCache {
    std::map<std::string, int> loaded;
    const Pack* pack = nullptr;
};
int texture_for(Renderer& renderer, TextureCache& cache, const std::string& path);

// the original's texture table (TEXTABLE at texture_slots_base): a slot's picture, by the texture
// sets the manifest places there; the given sets win where sets share slots
struct TextureSlots {
    std::map<int, std::string> pictures; // slot -> PNG path
};
void build_texture_slots(TextureSlots& slots, const AssetManifest& manifest,
                         const std::vector<int>& preferred_sets);
// the renderer texture in a slot; -1 for an empty slot or one no loaded pack held
int slot_texture(Renderer& renderer, TextureCache& cache, const TextureSlots& slots, int slot);
// every slot picture the pack holds, uploaded (the stage's and the battle's texture sets)
void load_slot_textures(Renderer& renderer, TextureCache& cache, const TextureSlots& slots,
                        const Pack& pack);

// a model set on the GPU: one mesh per object, one texture per section (-1 untextured)
struct GpuModelSet {
    std::vector<int> object_mesh;
    std::vector<std::vector<int>> section_texture;
};
bool upload_model_set(Renderer& renderer, TextureCache& cache, const ModelSet& set,
                      GpuModelSet& out);

// model_node_local_matrix: scale, then x, y, z rotation, then translation
Mat4 node_local_matrix(const ModelNode& node);

// a queued run of sections: the original's Mesh record
struct MeshRecord {
    const ModelSection* sections = nullptr; // Mesh.subs, nsubs of them
    int section_count = 0;
    int mesh = -1;         // the renderer mesh
    int texture_base = 0;  // ModelRoot.base: a section's tex[s] + base is its texture slot
    int paint = -1;        // a renderer texture drawn on stage 0 instead (the fighters' paint)
    Mat4 world;            // the matrix stack's top when queued
    std::uint32_t flags = 0;          // Mesh.flags (ModelRoot.flags)
    std::uint32_t factor = 0xFFFFFFFF; // Mesh.factor, the texture factor with flag 0x20
    float center[3] = {};  // Mesh.center and radius (the node's box, model space); a zero box
    float half[3] = {};    // always passes the cull
    float heading = 0;     // fighters: the actor's heading (multi_matrix_pl's normal frame)
    float reveal[4] = {};  // pass 5: the camouflage's reveal sphere (texture_slot_vec4_set)
    // what the effect programs read besides (src/effects): rainbow's strength, scroll and band
    // phase; effopt's reveal sphere (centre, radius) and glow
    float effect[5] = {};
};

// a record and the pass it is queued in, with its queue key (docs/port/render.md, "Draw queues")
struct PassItem {
    int pass = 2;
    float distance = 0;
    std::uint32_t key = 0;
    MeshRecord item;
};

// one section as a record with the given world matrix, its stage 0 texture (the paint, or -1 for
// the section's own by slot) and mesh flags, queued in a pass
MeshRecord section_draw(const ModelSection& section, int mesh, int texture, const Mat4& world,
                        int pass, std::uint32_t mesh_flags);

// a free camera as the original's matrices: D3DXMatrixLookAtLH's view (built in the port's mirrored
// frame, which draws the same picture) and PerspectiveFovLH's projection, 52 degrees vertical,
// near 0.1, far 10000, for the viewport's aspect
struct CameraMatrices {
    Mat4 view;
    Mat4 projection;
    Vec3f eye;
};
// fov in degrees (viewports_fov_set: 52 but in the scene cameras); roll in radians, the view turned
// about its axis after the look (view x RotZ(roll), camera_scene_update)
CameraMatrices camera_matrices(const float eye[3], float yaw, float pitch, float aspect,
                               float fov = 52.0f, float roll = 0.0f);
