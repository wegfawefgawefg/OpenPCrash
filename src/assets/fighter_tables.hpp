#pragma once
// data/tables/fighter_assets.sexp and data/tables/sv_graphs.sexp (the extractor's fighters and
// skeletons families, cooked into the core pack): which assets make a fighter, and the SV
// articulation graphs that pose it.
#include "assets/motion_sets.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// SvNode.flags (sv_pose_walk, actor_node_capture_matrices, attachment_bind_matrices)
inline constexpr std::uint32_t node_root = 0x2;    // exactly 2: the root track's travel
inline constexpr std::uint32_t node_body = 0x4;    // body_matrix: aim_yaw and the lean
inline constexpr std::uint32_t node_flag8 = 0x8;   // flag8_matrix
inline constexpr std::uint32_t node_aim_a = 0x10;  // arm A's aim frame
inline constexpr std::uint32_t node_aim_b = 0x20;  // mount_matrix_b: arm B's aim frame
inline constexpr std::uint32_t node_aims = 0x30;   // either aim frame
inline constexpr std::uint32_t node_arm_a = 0x40;  // arm A's model hangs here
inline constexpr std::uint32_t node_arm_b = 0x80;  // mount_matrix_back_b: arm B's
inline constexpr std::uint32_t node_aimed = 0xF0;  // aim frames and arms: turned to the reticle
inline constexpr std::uint32_t node_pitch = 0x100; // the camera pitch
inline constexpr std::uint32_t node_wheel = 0x200; // the wheels' turn
inline constexpr std::uint32_t node_body_set_0 = 0x400; // the body set's slots 0, 1, 3, 4, 5, 6
inline constexpr std::uint32_t node_body_set_1 = 0x800;
inline constexpr std::uint32_t node_body_set_3 = 0x2000;
inline constexpr std::uint32_t node_body_set_4 = 0x4000;
inline constexpr std::uint32_t node_body_set_5 = 0x8000;
inline constexpr std::uint32_t node_body_set = 0x1EC00; // any of them (sv_pose_flagged_node_sample)
inline constexpr std::uint32_t node_foot = 0x20000;     // a footstep point
inline constexpr std::uint32_t node_attachment_0 = 0x40000; // << k binds attachment k (0..13)

// SvNode (include/tables.h), children resolved to indices in FighterTables::nodes
struct SvGraphNode {
    std::uint32_t address = 0; // in the XBE, the node's name
    int type = 0; // 0, 1, 2 (IK chain root), 3 (link), 4 (chain end), 7 (weapon switch)
    std::uint32_t flags = 0;
    int track = -1;
    float translate[3] = {}; // port frame, what the static path applies
    float rotate[3] = {};
    int matrix = -1;
    int spheres = -1;
    int remap = -1;
    int f34 = 0;
    std::vector<int> children; // -1: a weapon slot (type 7 nodes)
};

struct FighterBuild {
    int body = -1, leg = -1;           // part record rows
    int weapons[4] = {-1, -1, -1, -1}; // arm A, arm B, back A, back B record rows
    int colour = 0, sticker = -1;
    std::string name;
};

// a weapon block's muzzle (kind 10, resource 0's stream 0): a point under one of its model's
// nodes, port frame
struct WeaponMuzzle {
    float point[3] = {};
    int node = -1;
};
struct WeaponBlockMuzzles {
    int asset = -1;
    std::vector<WeaponMuzzle> muzzles;
};

// a weapon block whole (kind 10, docs/port/data.md section 10; tools/extract/fighters.cpp
// weapon_blocks_text): resource 0's attach streams 0..3 (0 muzzles, 1 the flash model 0x3F3's
// point, 2 the casing's origin, 3 the casing's direction), port frame; resource 1's clip; resource
// 2's open and close frames
struct WeaponClipChannel {
    int node = -1;
    std::uint32_t flags = 0;  // 0x400..0x100 rotate x y z, 0x80..0x20 translate x y z (written)
    MotionCurve rotate[3];    // radians, port frame (x, -y, -z of the disc's curves)
    MotionCurve translate[3]; // port frame (x, -y, -z of the disc's curves)
};
struct WeaponBlock {
    int asset = -1;
    std::vector<WeaponMuzzle> streams[4];
    bool locked = false;     // resource 2's list 0 has open and close frames
    int open = 0, close = 0; // those frames, ticks
    bool clip = false;       // anim_player_start found a clip
    int frames = 0;          // NodeAnimSet.frames as stored (end = (frames & 0x7FFF) - 1)
    std::vector<WeaponClipChannel> channels;
};

struct FighterTables {
    std::vector<int> body_models, body_sticker_models, leg_models, arm_a_models, arm_b_models,
        back_a_models, back_b_models;
    std::vector<int> leg_motions, body_motions, arm_a_motions, arm_b_motions, arm_motion_class;
    std::vector<std::vector<int>> part_paints;               // by leg class, by colour
    std::vector<int> leg_graphs, arm_a_graphs, arm_b_graphs; // node indices, -1 none
    // the weapon blocks by class (arm A, arm B, back A, back B) and each block's muzzles. Empty in
    // an extract from before the fighters family wrote them
    std::vector<int> arm_a_blocks, arm_b_blocks, back_a_blocks, back_b_blocks;
    std::vector<WeaponBlockMuzzles> block_muzzles;
    std::vector<WeaponBlock> blocks; // empty in an extract from before weapon-blocks
    std::vector<int> body_class, leg_class, arm_class, back_class; // by part record row
    std::vector<FighterBuild> builds;
    // attachment 13 of each leg class's fixed attachment vectors (sv_leg_attachment_table), in the
    // node's frame: the upper contact sphere's and the chase camera's head point. Empty in an
    // extract from before the fighters family wrote leg-attachments
    std::vector<std::array<float, 3>> leg_head_points;
    std::vector<SvGraphNode> nodes;
};

struct Pack;
// from the core pack
bool load_fighter_tables(const Pack& core, FighterTables& tables, std::string& error);

// pack layout (src/cook/pack_format.hpp)
template <class A> void io(A& a, SvGraphNode& n) {
    a(n.address);
    a(n.type);
    a(n.flags);
    a(n.track);
    a(n.translate);
    a(n.rotate);
    a(n.matrix);
    a(n.spheres);
    a(n.remap);
    a(n.f34);
    a(n.children);
}
template <class A> void io(A& a, FighterBuild& b) {
    a(b.body);
    a(b.leg);
    a(b.weapons);
    a(b.colour);
    a(b.sticker);
    a(b.name);
}
template <class A> void io(A& a, WeaponBlockMuzzles& b) {
    a(b.asset);
    a(b.muzzles);
}
template <class A> void io(A& a, WeaponClipChannel& c) {
    a(c.node);
    a(c.flags);
    for (int k = 0; k < 3; ++k) {
        io(a, c.rotate[k]);
        io(a, c.translate[k]);
    }
}
template <class A> void io(A& a, WeaponBlock& b) {
    a(b.asset);
    for (std::vector<WeaponMuzzle>& stream : b.streams) {
        a(stream);
    }
    a(b.locked);
    a(b.open);
    a(b.close);
    a(b.clip);
    a(b.frames);
    a(b.channels);
}
template <class A> void io(A& a, FighterTables& t) {
    for (std::vector<int>* list :
         {&t.body_models,  &t.body_sticker_models, &t.leg_models,    &t.arm_a_models,
          &t.arm_b_models, &t.back_a_models,       &t.back_b_models, &t.leg_motions,
          &t.body_motions, &t.arm_a_motions,       &t.arm_b_motions, &t.arm_motion_class,
          &t.leg_graphs,   &t.arm_a_graphs,        &t.arm_b_graphs,  &t.body_class,
          &t.leg_class,    &t.arm_class,           &t.back_class,    &t.arm_a_blocks,
          &t.arm_b_blocks, &t.back_a_blocks,       &t.back_b_blocks}) {
        a(*list);
    }
    a(t.part_paints);
    a(t.builds);
    a(t.leg_head_points);
    a(t.block_muzzles);
    a(t.blocks);
    a(t.nodes);
}
