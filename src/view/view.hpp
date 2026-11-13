#pragma once
// This machine's view of the world: what the last two steps looked like (for drawing between them),
// and the input gathered since the last step.
#include "mech/mech_pose.hpp"
#include "world/inputs.hpp"
#include "world/world.hpp"

#include <SDL3/SDL.h>

struct SceneCamera;

// a scene camera's curves as one step sampled them, kept so a drawn frame between two steps
// samples them again at its own fractional frame (decomp cameras.md: the frame is a float);
// `mix` and `own_*` are an entrance's last 20 ticks blending into the fighter's camera
struct SceneCurveAt {
    const SceneCamera* row = nullptr; // none: the pose as it is
    float frame = 0;
    float heading = 0; // radians, the scene's
    float focus[3] = {};
    float mix = 1; // the scene's share
    float own_position[3] = {}, own_target[3] = {};
};

// the camera in floats, as one step left it: the free camera's place and angles, or the battle
// camera's eye and target (yaw and pitch then come from the interpolated pair)
struct CameraPose {
    float position[3] = {};
    float yaw = 0;   // radians, 0 along +z
    float pitch = 0; // radians, up positive
    bool looks_at = false;
    float target[3] = {};
    float fov = 52.0f; // degrees (view_fov; a scene curve's in a scene camera)
    float roll = 0.0f; // radians about the view's axis (a scene curve's)
    SceneCurveAt scene; // a scene camera's, sampled again at draw time
};

// a fighter as one step left it, in floats: where to draw it, and its pose from the world's
// (mech/fighter_pose.hpp: the palette, the mounts and the weapon nodes), which the drawing uses
struct FighterPose {
    bool present = false;
    float position[3] = {};
    float heading = 0; // radians, port frame: forward is (sin, 0, cos)
    int clip = 28;     // the motion slot playing (Actor.motion_slot_a)
    float phase = 0;   // motion_phase, ticks
    int motion_state = 11;
    // the original's sense, as MechPoseInput takes them (mech_pose.cpp mirrors them itself)
    float aim_yaw = 0;
    float pitch = 0;
    float turn_rate = 0;
    MechPose pose; // the world's, in floats
    // the camouflage's look (src/render/camo_draw.hpp, camo_pose_from): its state, the reveal
    // sphere camo_update hands the shader, and the k this fighter's task sets (-1 none)
    int camo_state = 11;
    float camo_timer = 0; // Actor.camo_timer, ticks (the sticker's fade)
    float camo_sphere[4] = {};
    float camo_k = -1;
    // actor_draw's flag_3 off: the body, its sticker and shadow left out, the weapons drawn (a
    // human in its cockpit view, src/view/cockpit_view.hpp); set at draw time
    bool body_hidden = false;
};

struct StepPose {
    CameraPose camera; // the main view's
    FighterPose fighters[fighter_slots];
    CameraPose seat_cameras[max_pad_players]; // each seat's fighter's, for split screen
};

// keys held and mouse motion since the last step, turned into Inputs at step time
struct LatchedInput {
    bool forward = false, back = false, left = false, right = false, up = false, down = false,
         fast = false;
    float mouse_x = 0, mouse_y = 0; // pixels moved since the last step
    bool mouse_look = false;        // right button held (or the window grabbed)
};

struct View {
    StepPose previous; // before the last step
    StepPose current;  // after it
    LatchedInput input;
    int follow = -1; // whose camera the main view shows (--follow); -1 seat 0's fighter
};

CameraPose camera_pose(const FlyCamera& camera);
// the pose to draw: previous to current by alpha (0..1); yaw by the short way round
CameraPose interpolate_camera(const View& view, float alpha);
// a seat's own view (split screen), and the blend both use
CameraPose interpolate_seat_camera(const View& view, int seat, float alpha);
CameraPose interpolate_camera_pair(const CameraPose& a, const CameraPose& b, float alpha);
// the slot of the fighter a seat plays (Fighter::controller), -1 none
int seat_slot(const World& world, int seat);
// a fighter between the two steps: position and heading blended, the clip's phase too while the
// same clip plays on without wrapping, and every pose matrix (places lerped, turns nlerped)
FighterPose interpolate_fighter(const View& view, int slot, float alpha);
// the poses before a step and after it: the battle camera follows --follow's fighter or seat 0's
// when there is a battle, else the free camera
void take_step_poses(View& view, const World& before, const World& after);

// input (src/view/input.cpp)
void handle_event(View& view, const SDL_Event& event, SDL_Window* window);
Inputs build_inputs(View& view);
