#pragma once
// The scene curves (decomp scene_camera_rows 0x258E68; data/tables/scene_cameras.sexp from the
// scenes family): per scene state its length (scene_duration, the camera row's first
// word), the root motion an entrance walks (scene_root_delta) in Q16.16 for the world,
// and the camera curves (scene_camera_sample) in floats for presentation.
#include "assets/motion_sets.hpp"
#include "mech/rig_data.hpp"

#include <string>

struct Pack;

inline constexpr int scene_state_count = 0x2F;

// the world's part: what scene_root_delta reads
struct SceneWalks {
    int duration[scene_state_count] = {}; // ticks, 0 for a state with no camera row
    struct Root {
        bool present = false;
        FxCurve x, y, z; // the root track's translation, port frame
        FxCurve yaw;     // its rotation about y (flags 0x700), the motion family's sign
        bool turns = false;
    } roots[scene_state_count];
};

// presentation's part: roll (radians), fov (degrees), eye x y z, target x y z
struct SceneCamera {
    bool present = false;
    bool relative = false; // to the scene's focus and heading
    int duration = 0;
    MotionCurve curves[8];
};

bool load_scene_walks(SceneWalks& out, const Pack& core, std::string& error);
bool load_scene_cameras(SceneCamera (&out)[scene_state_count], const Pack& core,
                        std::string& error);
