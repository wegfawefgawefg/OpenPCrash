#pragma once
// The stage's things that move, on screen: the props, the supply craft and its crates (from the
// world, drawn between the last two steps), the sky craft and the night's moon (presentation of
// its own). The draws transcribe stage_prop_render, supply_craft_draw,
// pickups_draw, sky_craft_draw and stage_render_lods' moon (decomp
// src/hand/falling_debris.c, src/od/u_06dfc0.c), in the port frame.
#include "assets/manifest.hpp"
#include "cook/pack_library.hpp"
#include "render/d3dx_math.hpp"
#include "stage/stage_scene.hpp"
#include "stage/stage_world.hpp"

#include <map>
#include <string>
#include <vector>

// a model set loaded for the stage's things, by slot, with its texture set's first slot
struct ViewModelSet {
    ModelSet set;
    GpuModelSet gpu;
    int texture_base = 0;
};

// the sky craft (sky_craft_route_step): only flies and draws, nothing reads it
struct SkyCraft {
    int route = 0, last_segment = 0, segment = 0;
    float t = 0;
    float pos[3] = {}, previous[3] = {};
    int wing_state = 0; // 1 opening, 2 closing, 3 and 4 hold, 5 parked by stage_props_state_push_5
    float wing = 0, wing_previous = 0; // radians
    float rotor = 0, rotor_previous = 0;
    float glow_phase = 0, glow = 1; // sky_glow_phase, D_0021C6D0
    Mat4 matrix, matrix_previous;
    // the panels (sky_craft_screen_update)
    bool slideshow = true;
    int image = 0;
    float scroll = 0;
    bool screens_dirty = true; // the panels' meshes to rewrite before the next draw
};

struct StageView {
    const StageData* data = nullptr;
    int selector = 0;
    int night = 0;
    int day = 0; // the moon's phase
    std::vector<ViewModelSet> sets;
    std::map<int, std::pair<int, int>> slots; // slot -> (set, object)
    // grid_mdl as frame_cadence_init sets it up (uvs, layer 0x40), its own mesh
    ModelObject quad;
    int quad_mesh = -1;
    int screen_meshes[2] = {-1, -1}; // the sky craft's two panels
    int screen_textures[5] = {-1, -1, -1, -1, -1}; // the adverts, renderer textures

    // the world's stage after the last two steps, as floats
    struct PropPose {
        Mat4 previous, current;
        int model = -1;
        bool drawn = false;
    };
    std::vector<PropPose> props;
    Mat4 craft_previous, craft_current;
    float hatch_previous = 0, hatch_current = 0;
    float rotor_previous = 0, rotor_current = 0;
    int craft_state = 0;
    float crate_previous[3][3] = {}, crate_current[3][3] = {};
    float crate_glow[3] = {};
    bool crate_shown[3] = {};
    int crate_kind[3] = {};
    float beacon = 1; // craft_beacon_pulse, never reset in retail (docs/bugs.md)
    SkyCraft sky;
};

bool load_stage_view(StageView& view, const StageData& data, int selector, int day,
                     const AssetManifest& manifest, PackLibrary& packs, Renderer& renderer,
                     TextureCache& textures, std::string& error);
// after each world step: the poses of the world's stage, and the sky craft's step
void stage_view_step(StageView& view, const StageWorld& world);
// the draws between the last two steps, queued with view 0's matrix
void stage_view_draw(StageView& view, Renderer& renderer, const Mat4& view_matrix,
                     const float eye[3], float alpha, std::vector<PassItem>& out);

// sky_craft.cpp
void sky_craft_start(SkyCraft& sky, const StageData& data, int selector);
void sky_craft_step(SkyCraft& sky, const StageData& data);
void sky_craft_draw(StageView& view, Renderer& renderer, const Mat4& view_matrix, float alpha,
                    std::vector<PassItem>& out);

// shared by the two (stage_view.cpp)
Mat4 mat_of(const FxMat4& m);
Mat4 mat_lerp(const Mat4& a, const Mat4& b, float t);
// a model queued by slot (model_render_submit and its _color, _bit25 kin): mesh -1 its own,
// paint a renderer texture on stage 0 for every section, factor the texture factor (flag 0x20)
void draw_slot(const StageView& view, int slot, const Mat4& world, int pass, std::uint32_t flags,
               const Mat4& view_matrix, std::vector<PassItem>& out, int mesh = -1,
               int paint = -1, std::uint32_t factor = 0xFFFFFFFF);
// a texture by the original's texture slot (the texture set holding it, from its pack)
int texture_by_slot(const AssetManifest& manifest, PackLibrary& packs, Renderer& renderer,
                    TextureCache& textures, int slot);
