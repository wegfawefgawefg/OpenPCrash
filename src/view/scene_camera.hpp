#pragma once
// The battle's scene cameras as presentation (decomp docs/port/cameras.md "Scene cameras",
// "Blending", "Cuts and blends"; camera_scene_update, scene_camera_sample,
// camera_scene_intro_set, camera_scene_rail_set, camera_view4_front,
// camera_view5_rails, camera_demo_set). After each step the drawn cameras
// (View's main and seat cameras, which take_step_poses filled with each fighter's own) are
// replaced where a scene runs:
// - a human's entrance (scenes 1..4, blended into the fighter's camera over its last 20 ticks)
//   and door exit (6): the scene's curves about its focus and heading, with their fov and roll;
// - a human's death (7, past its first step): the front view of the killer, then of the wreck;
// - a boss's entrance (5): its curves, on the main view; a boss's defeat (0x13..0x17): the stage
//   rail nearest it, pulled to within 10 of it;
// - the result screen's demo battle: the rails within 12.5 of the followed fighter;
// - a replay (battle phase 2): outside its scenes the human is seen through the replay's view
//   (camera_replay_pass: 0 cockpit, 1 chase, 2 side, 3 side high, 4 front, 5 rails).
// A cut (a camera taken over, a rail point changed) isn't interpolated across. The world never
// reads any of it: the fighters' own cameras stay the aim.
#include "battle/scene_curves.hpp"
#include "view/view.hpp"

#include <array>
#include <string>
#include <vector>

struct Pack;

struct SceneCameraTables {
    bool loaded = false;
    SceneCamera rows[scene_state_count];
    // camera_rails by day mode (the stage selector), port frame, and each stage zone's
    // [first, end) of them (camera_rail_zone_ranges)
    std::vector<std::array<float, 3>> rail_points[8];
    int rail_zones[8][5][2] = {};
};
bool load_scene_camera_tables(SceneCameraTables& out, const Pack& core, std::string& error);

struct SceneCameraState {
    struct Shown {
        CameraPose last; // what the view showed after the last step
        int source = -1; // which camera that was (cuts are changes of it)
    };
    Shown main;
    Shown seats[max_pad_players];
    // ActorCamera.rail_repick_clock and rail_point per slot (camera_init zeroes them)
    int rail_clock[fighter_slots] = {};
    int rail_point[fighter_slots] = {};
    int selector = 0; // the stage's day mode
    int replay_view = -1; // replay_view 0..5 in a replay, -1 none
    // ActorCamera.eye_replay: the replay's own eye per slot, the chase's history in its view 1
    FxVec3 eye_replay[fighter_slots];
    bool eye_replay_set[fighter_slots] = {};
};

// scene_camera_sample's curves at a frame (fractional between steps), blended into the fighter's
// own camera by `mix`; the pose keeps `at` (view.cpp samples again at draw time)
CameraPose scene_curve_pose(const SceneCurveAt& at);

// the main view shows a replay's view 0 (camera_replay_pass's cockpit)
bool replay_cockpit_shown(const SceneCameraState& state);

// PC CONFIG's FIELD OF VIEW (RenderOptions::fov): degrees across a 16:9 view, as PC games give it,
// 82..130 by 2. 82 stands for retail's 52 degrees vertical (2 atan(tan 26 x 16/9) is 81.9); the
// rest turn into the vertical angle the same way, and wider or narrower windows keep that
// vertical (Hor+), as at 52
inline constexpr int fov_original = 82, fov_widest = 130;
// the vertical degrees a FIELD OF VIEW stands for: 52 at 82, 88.5 at 120, 100.7 at 130
float fight_fov_vertical(int fov);
// FIELD OF VIEW on a drawn camera shown from `source` (SceneCameraState::Shown's, 0 the fighter's
// own): the fighter's own views (the chase views, the cockpit, each split-screen view) and a
// replay's widen by the setting's vertical over 52, an entrance as it hands over to the fighter's
// camera by the fighter's share of its blend, any other scene camera not at all. Presentation
// only: the world's aim and lock-on keep retail's 52 (weapons/hud_aim.cpp)
void fight_fov_apply(CameraPose& pose, int source, int fov);
// the world's screen units (its 52 degrees) in a view widened by `widen` degrees: the world's
// units times this are the drawn view's, about its centre (tan 26 / tan(26 + widen / 2)), 1 for 0
float fight_fov_unit(float widen);

// after take_step_poses for the step that made `after`
void scene_cameras_step(SceneCameraState& state, const SceneCameraTables& tables, View& view,
                        const World& after);
// the rails camera on a fighter (camera_scene_rail_set): eye at its rail point, pulled to within
// `reach` of the fighter; steps the slot's rail clock
CameraPose rail_camera(SceneCameraState& state, const SceneCameraTables& tables,
                       const World& world, int slot, float reach);
