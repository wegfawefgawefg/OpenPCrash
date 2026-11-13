#pragma once
// What the world poses a fighter from, in Q16.16: the SV articulation graphs, the motion curve
// sets its build plays (legs, arms, body), the leg class's attachment vectors and the weapon
// models' node trees. Converted once at load from the cooked packs' floats by their bits
// (fx_bits), so every platform gets the same numbers; held in BattleData.
#include "assets/fighter_tables.hpp"
#include "fx/angle.hpp"
#include "fx/vec.hpp"

#include <cstdint>
#include <string>
#include <vector>

// one motion curve in fixed point (assets/motion_sets.hpp has the float one)
struct FxCurve {
    std::uint32_t mode = 0;
    std::vector<std::uint16_t> times;
    std::vector<Fx> values;
    bool present = false;
};
// motion_curve_eval at a frame in ticks, the same arithmetic as motion_curve_value
Fx fx_curve_value(const FxCurve& curve, Fx frame);

// MotionTrack: rotation (flags 0x700, radians) and translation (0xE0), port frame
struct FxTrack {
    std::uint32_t flags = 0;
    FxCurve rotate[3];
    FxCurve translate[3];
    // the key times motion_select_descriptor's three footstep streams read when the track
    // has 0x3800 (presentation's footsteps, view/footsteps.cpp): after the rotation curves
    // the cursor meets the translation curves first if there are any (asset 1758, docs/bugs.md),
    // and a constant curve holds it in place (motion_key_stream doesn't step past one); empty for
    // a constant
    std::vector<std::uint16_t> markers[3];
};
using FxClip = std::vector<FxTrack>;
struct FxMotionSet {
    std::vector<int> slot_clips; // slot -> clip, -1 none
    std::vector<FxClip> clips;
};
// the clip a slot plays, or null
const FxClip* fx_motion_slot(const FxMotionSet& set, int slot);
// a clip's track, or null
const FxTrack* fx_track(const FxClip* clip, int track);

// SvNode (include/tables.h) as the walk reads it, children as indices into BattleData::rig_nodes
struct RigNode {
    int type = 0; // 2 an IK chain root, 4 a short chain's end, 7 the weapon switch
    std::uint32_t flags = 0;
    int track = -1;
    FxVec3 translate;
    Angle rotate[3];
    bool twist_positive = true; // rotate z >= 0 as a float (the IK's bend side reads its sign)
    int matrix = -1;
    int spheres = -1;
    std::vector<int> children; // -1: a weapon slot (type 7)
};

// a weapon model's node (objects[0]), as weapon_model_nodes_place places it
struct RigModelNode {
    int parent = -1;
    FxVec3 translate;
    Angle euler[3];          // applied x, then y, then z
    std::uint32_t flags = 0; // model_node_aimed
};
// RigModelNode.flags 0x100 (weapon_model_nodes_place, model_flagged_nodes_spin): set from the
// arm's pose or spun by the back aim
inline constexpr std::uint32_t model_node_aimed = 0x100;

// a weapon block's muzzle point (weapon_attach_matrix's stream 0) under a model node
struct RigMuzzle {
    FxVec3 point;
    int node = -1;
};

// a weapon clip's channel (NodeAnim): the node it writes and the curves of the channels its flags
// name (0x400, 0x200, 0x100 rotate x y z; 0x80, 0x40, 0x20 translate x y z), port frame, the
// writer's flips baked in (model_node_write_transform)
struct RigClipChannel {
    int node = -1;
    std::uint32_t flags = 0;
    FxCurve rotate[3]; // radians, wrapped through 16 bits when sampled
    FxCurve translate[3];
};

// a weapon block (Weapon.model; docs/port/data.md section 10) beyond its muzzles: the attach
// streams (0 muzzles, 1 the flash model 0x3F3's point, 2 the casing's origin, 3 the casing's
// direction: a direction in the port frame, weapon_eject_casing turns it by the casing
// point's node matrix without translation), the clip and its open and close frames
struct RigWeaponBlock {
    std::vector<RigMuzzle> streams[4];
    bool clip = false;  // anim_player_start found one
    Fx end;             // anim_frame_count: (frames & 0x7FFF) - 1, ticks
    bool locked = false; // weapon_anim_init: list 0 has the open and close frames
    Fx open, close;      // ticks (an unlocked clip: 0 and end)
    std::vector<RigClipChannel> channels;
};

// sv_leg_attachment_table: a point a in the bound node's frame, the selector (0 a point, 1 and 2
// with a direction b, 3 an offset from the position), port frame
struct LegAttachment {
    FxVec3 a;
    int selector = -1;
    FxVec3 b;
};
inline constexpr int leg_attachment_count = 14;

// what one build is posed from
struct FighterRig {
    int build = -1;
    int body_class = -1, leg_class = -1;
    int weapon_class[4] = {-1, -1, -1, -1}; // arm A, arm B, back A, back B (30: class 2's backs)
    bool weapon_fitted[4] = {};             // scoobee_weapon_id != -1
    int leg_graph = -1, arm_graph[2] = {-1, -1};
    FxMotionSet leg_motion, arm_motion[2], body_motion;
    std::vector<RigModelNode> weapon_model[4];
    // the weapon blocks' muzzles by mount (weapon_muzzle_count is their count), empty when the
    // mount has no weapon
    std::vector<RigMuzzle> muzzles[4];
    // the fitted weapons' blocks by mount (streams[0] are the muzzles above); empty when the mount
    // has no weapon
    RigWeaponBlock blocks[4];
};

struct BattleData;
struct PackLibrary;
struct AssetManifest;
// the graphs (from the core pack's fighter tables) and a rig for each build
// (extra: rows after the table's, as load_battle_data's)
bool load_fighter_rigs(BattleData& data, PackLibrary& packs, const AssetManifest& manifest,
                       const std::vector<int>& builds, std::string& error,
                       const std::vector<FighterBuild>& extra = {});
// the rig of a build, or null
const FighterRig* rig_of_build(const BattleData& data, int build);
