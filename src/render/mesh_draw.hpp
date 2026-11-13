#pragma once
// The original's mesh passes as draws (decomp docs/port/render.md, "Mesh drawing", "Shadows"):
// pass_draw_meshes picks each sub-mesh's states, texture stages and program, the program's
// handler (material_transform_dispatch, src/render/mesh_constants.cpp) its constants;
// pass_draw_fighters and shadow_volumes_draw the same for their passes.
#include "render/mesh_queue.hpp"
#include "render/stage_lighting.hpp"

// what a pass is drawn with: the view's camera, the lights, the texture table
struct DrawContext {
    Renderer* renderer = nullptr;
    TextureCache* cache = nullptr;
    const TextureSlots* slots = nullptr;
    const SceneLights* lights = nullptr;
    Mat4 view, projection, view_projection;
    Vec3f eye; // camera_eye_row
    int pass = 2;
    bool battle = true; // lit_shader_alt: the point shaders in pass 2
    // the textures the renderer loads by name (render_device_setup, shaders_init)
    int bump_light = -1, yurayura = -1, wave02 = -1;
    float water_bump_angle = 0; // water_bump_angle, advanced per update by the caller
    int muteki = -1;            // systemdata\\muteki01.tga, the shield bubble's (rainbow)
    float viewport[4] = {0, 0, 640, 480}; // the view's rectangle of the 640x480 screen
};

// pass_draw_meshes: a mesh pass's sorted queue (passes 1, 2, 8, 9, 11, 13)
void draw_meshes(const DrawContext& context, const std::vector<const PassItem*>& queue,
                 FrameDraws& out);
// pass_draw_fighters: pass 4, multi_matrixPL in battle
void draw_fighters(const DrawContext& context, const std::vector<const PassItem*>& queue,
                   FrameDraws& out);
// pass_draw_distortion: pass 6, effopt over the backdrop (the contact decals); `copy`:
// the first draw copies the backdrop (no pass 5 draw did)
void draw_distortion(const DrawContext& context, const std::vector<const PassItem*>& queue,
                     bool copy, FrameDraws& out);
// shadow_volumes_draw: pass 3 (fighters' volume batches, skinned) and pass 7 (static volumes, culled,
// then the shadow quad)
void draw_shadow_volumes(const DrawContext& context, const std::vector<const PassItem*>& queue,
                         FrameDraws& out);
