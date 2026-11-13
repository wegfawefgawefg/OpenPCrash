#pragma once
// A fighter's pose in the world, Q16.16, port frame: what actor_pose_build (the SV graph
// walk with its IK chains), actor_back_weapons_place, actor_arm_weapons_place and
// actor_attachments_update leave on the Actor for gameplay and the drawing: the node
// palette, the captured mounts, the body node (the camera's head_position), the attachment points
// and every weapon model node. Built in the fighter's step after the move (mech/pose_walk.cpp,
// mech/pose_weapons.cpp); the view interpolates it and draws from it.
//
// For the weapons: a weapon's muzzle is weapon_attach_matrix's point (the weapon block's
// attachment list) under its model node, then the node's matrix here, then turned by pi about y
// (weapon_muzzle_matrix): transform_coord(point, weapon_nodes[mount][node]). The nodes are
// already under their mount: arms under mounts 2 and 3, backs under the body (mount 4).
#include "fx/mat.hpp"

#include <cstdint>

inline constexpr int pose_parts = 3;     // the legs and body, arm A's graph, arm B's
inline constexpr int pose_matrices = 20; // the graphs use matrix indices 0..18
inline constexpr int weapon_model_nodes = 16;
inline constexpr int attachment_slots = 20; // 0..13 the leg's vectors, 14..19 the feet

// captured by actor_node_capture_matrices, by node flag
enum PoseMount {
    mount_aim_a, // flag 0x10, Actor +0x2FDC: arm A's aim frame
    mount_aim_b, // flag 0x20, mount_matrix_b: arm B's aim frame
    mount_arm_a, // flag 0x40, +0x2FE4: arm A's model hangs here
    mount_arm_b, // flag 0x80, mount_matrix_back_b: arm B's
    mount_body,  // flag 4, body_matrix: the back weapons hang here; its place is head_position
    mount_flag8, // flag 8, flag8_matrix
    pose_mount_count
};

// ActorAttachment: kind is the selector (0 a point, 1 and 2 with a direction, 3 attachment 13's
// offset from the position, 4 a foot), -1 unbound. Bound to a palette entry by node flag
// 0x40000 << slot (attachment_bind_matrices), which stays bound from step to step
struct FighterAttachment {
    int kind = -1;
    FxVec3 position, previous, direction;
    int part = -1, matrix = -1;
};

struct FighterPoseState {
    FxMat4 palette[pose_parts][pose_matrices];
    std::uint32_t stored[pose_parts] = {}; // a bit per palette entry the walk wrote
    FxMat4 mount[pose_mount_count];
    bool mounted[pose_mount_count] = {};
    FxVec3 head_position; // camera head_position: the body node's place
    FighterAttachment attachments[attachment_slots];
    int foot_count = 0;
    // the weapon models' nodes by mount (arm A, arm B, back A, back B), each under its mount
    FxMat4 weapon_nodes[4][weapon_model_nodes];
    int weapon_node_count[4] = {};
    // the weapon models' node values as the clips (weapon_anim_step) and the backs' spin
    // (model_flagged_nodes_spin) leave them: the original writes them into each fighter's
    // copy of the model, where they stay from update to update. Port frame; the rest pose at spawn
    FxVec3 weapon_translate[4][weapon_model_nodes];
    Angle weapon_euler[4][weapon_model_nodes][3];
    // the backs' aim (back_aim_matrix: z toward the reticle, rotation only) and launch_pitch
    // (spread_a + spread_b + elevation, the original's sense), the spun node (+0x2FEC)
    FxMat4 back_aim[2];
    Angle back_launch_pitch[2];
    int back_spin_node[2] = {};
    // the pose blend (sv_collision_spheres_select): the last pose's offset at the first node
    Fx blend_offset_x, blend_offset_z;
    int stores[pose_parts] = {};
};

struct Fighter;
struct BattleData;
struct FighterRig;
struct StageCollision;

// what the walk reads: the fighter's fields, or a measurement's (pose_compare.cpp)
struct PoseInput {
    const BattleData* data = nullptr;
    const FighterRig* rig = nullptr;
    int slot = 28; // motion_slot_a
    Fx frame;      // motion_phase, ticks
    int motion_state = 11;
    FxVec3 position;
    Angle heading;
    Angle aim_yaw;     // port sense
    Angle pitch;       // camera pitch, port sense
    Angle wheel_angle; // the original's sense
    Fx turn_rate;      // radians, the original's sense (z turns aren't mirrored)
    Fx node_frame[3];  // the body set's phases: 0x400, 0x800, 0x2000 and up (actor_phases_update)
    bool grounded = false; // pulls the feet's IK targets onto the stage
    Fx blend;               // this step's pose blend weight, 0 none
    // the arms' raise (sv_pose_weapon_node): raise / 5 of a ranged weapon, 0 none
    Fx arm_raise[2];
    FxVec3 aim_point[2]; // reticle_track_world or reticle_center_world by mount_free_aim
};

// actor_pose_build: the walk into pose (palette, mounts, head, bound attachments, feet)
void pose_walk(const PoseInput& input, FighterPoseState& pose);
// the input from a fighter as its step left it
PoseInput pose_input_of(const Fighter& f, const BattleData& data);

// pose_weapons.cpp: the parts of actor_step after the move, in its order: the pose, the back and
// arm weapons, the wheel spin, and actor_effects_update's attachments (fighter_step calls this);
// stand: battle_phase 5 (a model stand), where the weapon clips and the backs' aim are skipped
void fighter_pose_step(Fighter& f, const BattleData& data, bool stand = false);
// actor_phases_update (before the turn rate)
void fighter_phases_update(Fighter& f);
// the whole pose at spawn, so the first step's contact and camera have it
void fighter_pose_init(Fighter& f, const BattleData& data);
