#pragma once
// The camouflage's look (decomp docs/port/render.md "Fighters and camouflage", "multi_optical";
// src/od/u_082de0.c actor_draw, src/o2/u_0b2020.c pass_open_camo and
// pass_draw_camo, src/hand/u_0bee60.c shader_constants_multi_optical): a fighter
// in camouflage states 1..8 draws its parts in pass 5 through multi_optical / Blend, a refraction
// of the backdrop pushed along its normals by at least the shader's k, except inside the reveal
// sphere camo_update places (the aim point lifted from 0 to 10 over the fade, radius 5), where it
// fades back in. The drawing of camouflage lives here; the world's side is mech/fighter_camo.hpp.
#include "render/mesh_draw.hpp"

#include <vector>

struct World;
struct Fighter;
struct FighterPose;

// camo_update's tail for the view: the state, texture_slot_vec4_set's reveal sphere (aim point
// + (0, lift + 10, 0), radius 5; lift -10 ready or recharging, timer / 60 x -10 fading in, 0
// hidden, (60 - timer) / 60 x -10 fading out, (20 - timer) / 20 x -10 broken), and the k this
// fighter's task hands optical_shader_k_set (the chip's camo_distortion / 100 for a human outside
// VS, else 0.25)
void camo_pose_from(const World& world, const Fighter& f, FighterPose& pose);
// camo_is_state_1_to_8: drawn in pass 5
bool camo_pose_cloaked(const FighterPose& pose);
// the k in force when the frame draws: each fighter task sets its own in slot order and
// optical_shader_k_set ignores values outside 0..1, so the last fighter's in-range value (0.25 at
// the start). With an AI after the human that is 0.25 (retail as read, docs/bugs.md)
float camo_frame_k(const FighterPose* fighters, int count);
// pass_draw_camo with shader_constants_multi_optical, one draw a section like pass 4's; the
// view's first draw copies the scene into the backdrop (DrawItem::backdrop). rect is the view's
// rectangle of the 640x480 screen (the viewport the constants give the shader)
void draw_camo_fighters(const DrawContext& context, const std::vector<const PassItem*>& queue,
                        float k, const float rect[4], FrameDraws& out);
