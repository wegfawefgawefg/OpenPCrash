#pragma once
// What the cockpit view (view 0) changes in what is drawn, as presentation (decomp
// docs/port/cameras.md "Shake", "Who reads the camera later in the frame"):
// - actor_draw leaves out a human's own body, its sticker and its shadow while the camera
//   is in its cockpit (the weapons still draw), and actor_decals_step its lamp trails;
// - the hit shake (hit_shake_jitter): the world moves the fighter's own look-at by it
//   (FighterCamera::shake_shift, up; battle/camera_views.cpp), so the aim, the lock boxes and the
//   senses shake as retail's. The view draws the camera without it and adds it back scaled by PC
//   CONFIG's CAMERA SHAKE, and takes the HUD's screen points back by the rest: the scale never
//   reaches the aim.
#include "view/view.hpp"

// actor_draw's flag_3: the slot's own body is hidden (a human in the cockpit view outside a scene,
// or a replay seen from view 0)
bool own_body_hidden(const World& world, int slot, int replay_view);
// actor_decals_step: a human's lamp trails are hidden in the cockpit view or switching to it
bool own_lamps_hidden(const World& world, int slot);
// how much of the slot's camera is view 0 now: 1 in it, the blend's share while switching
float cockpit_share(const World& world, int slot);
// the drawn camera's target moved by the world's shake this step times scale (0..1, PC CONFIG's
// CAMERA SHAKE); the angles taken again from the moved target
void cockpit_shake_apply(CameraPose& pose, const World& world, int slot, float scale);
// the death scene's first step (scene 7, step 0): camera_scene_update draws the human's own
// camera with camera_shake_up's tilt in view 0 (the world's FighterCamera::up), as a roll about
// the view axis times scale. cockpit_shake_apply does it for the live camera; a replay calls it
// for whichever replay view shows (retail's replay look-at takes the tilt by the fighter's own
// view, not the replay's)
void dying_tilt_apply(CameraPose& pose, const World& world, int slot, float scale);
// a replay seen from view 0: camera_view0_cockpit adds the hit shake's jitter to the target there
// too (the up stays plain), times scale
void replay_shake_apply(CameraPose& pose, const World& world, int slot, float scale);
// how far the HUD's screen points (the world's, through its shaken look-at) move back toward the
// plain look-at's for a scale under 1: (1 - scale) of the reticle centre's move, in the world's
// screen units (y up)
void cockpit_shake_hud(const World& world, int slot, float scale, float out[2]);
