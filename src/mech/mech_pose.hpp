#pragma once
// A fighter's pose in floats. The world's pose (mech/fighter_pose.hpp, Q16.16, in the step) is
// what the battle draws, converted and interpolated by the view into this struct; the float walk
// here poses the free camera's fighter (no battle, so no world fighter) and is pose_compare.cpp's
// reference. The walk: the SV articulation graph walked the way actor_pose_build and
// sv_pose_walk walk it, with the IK chains of sv_ik_chain, writing one matrix per
// graph node that has a matrix index (the palette the part batches are drawn with). Floats, in the
// port frame: the graph and motion files carry the readers' flips already (docs/assets.md), and the
// angles this code adds itself (aim yaw, camera pitch, wheel spin, the body class 1 tilt) are
// negated where the z mirror negates them (x and y rotations).
#include "assets/fighter_tables.hpp"
#include "assets/motion_sets.hpp"
#include "render/d3dx_math.hpp"

// what the walk reads from the fighter (Actor fields in the decomp)
struct MechPoseInput {
    const FighterTables* tables = nullptr;
    int graph_root = -1;                  // leg graph node (graph_root, Actor +0x148)
    int arm_a_root = -1;                  // arm A graph (graph_root_weapon_a), -1 none
    int arm_b_root = -1;                  // arm B graph (graph_root_weapon_b)
    const MotionClip* clip = nullptr;     // the leg set's clip for the slot (motion_clip)
    const MotionClip* clip_a = nullptr;   // arm A's (motion_clip_c)
    const MotionClip* clip_b = nullptr;   // arm B's (motion_clip_b)
    const MotionClip* body_clips[7] = {}; // the body set's slots (f_158)
    float frame = 0;                      // motion_phase, 60 Hz ticks
    int motion_state = 11;                // idle
    int body_class = 0;
    int leg_class = 0;
    float aim_yaw = 0;
    float turn_rate = 0;
    float pitch = 0; // the camera's
    float wheel_angle = 0;
    Mat4 world; // translate by position, then rotate y by heading
};

struct MechPose {
    // by pose part (0 chassis, 1 arm A, 2 arm B), by the node's matrix index
    Mat4 matrices[3][32];
    bool stored[3][32] = {};
    // matrices taken by actor_node_capture_matrices: flags 0x10 (Actor +0x2FDC), 0x20
    // (mount_matrix_b), 0x40 (arm A's mount, +0x2FE4), 0x80 (arm B's, +0x2FE8), 4 (body_matrix)
    Mat4 mount[5];
    bool mounted[5] = {};
    int ik_tip_rotations =
        0; // IK tip tracks that also hold rotation (the original reads those as the target)
    // the weapon models' nodes by mount (arm A, arm B, back A, back B) when the pose comes from
    // the world (mech/fighter_pose.hpp); the float walk leaves them empty and the drawing places
    // the models from the mounts itself
    Mat4 weapon_nodes[4][16];
    int weapon_node_count[4] = {};
};

void build_mech_pose(const MechPoseInput& input, MechPose& pose);

// sv_ik_solve_three and sv_ik_solve_two (src/mech/mech_ik.cpp): the chain's joint
// matrices from the root frame, the target and the authored link lengths
void ik_solve_three(const Vec3f& target, const Mat4& top, float length_a, float length_b,
                    float twist, Mat4& joint, Mat4& end, Mat4& tip);
void ik_solve_two(const Vec3f& target, const Mat4& top, float length, Mat4& joint, Mat4& end);
