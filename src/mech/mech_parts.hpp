#pragma once
// One fighter put together from a build row the way fighter_assets_load and
// actor_part_batches_build do: body and legs skinned to the leg graph's palette (one bone
// per model node with sections, body first), the arms and back weapons along their own models'
// node trees from the mounts the pose captures (weapon_model_nodes_place), everything in
// the paint the build's colour picks for its leg class (part_color_assets).
#include "assets/fighter_tables.hpp"
#include "assets/manifest.hpp"
#include "assets/model_sets.hpp"
#include "assets/motion_sets.hpp"
#include "cook/pack_library.hpp"
#include "mech/mech_pose.hpp"
#include "render/scene_draw.hpp"
#include "stage/stage_scene.hpp"

#include <string>

enum MechPart {
    part_body,
    part_legs,
    part_arm_a,
    part_arm_b,
    part_back_a,
    part_back_b,
    part_count
};

struct MechParts {
    const FighterTables* tables = nullptr;
    int build = -1;
    int body_class = -1, leg_class = -1;
    int weapon_class[4] = {-1, -1, -1, -1};
    ModelSet models[part_count];
    GpuModelSet gpu[part_count];
    bool loaded[part_count] = {};
    int paint = -1; // renderer texture
    // the body's sticker model (body_sticker_model_assets[body class], slot 0x592 + player) and
    // its sticker's picture (sv_sticker_assets[sticker], texture slot 0x210 + player)
    ModelSet sticker_model;
    GpuModelSet sticker_gpu;
    int sticker_texture = -1;
    MotionSet leg_motion, body_motion, arm_a_motion, arm_b_motion;
    int arm_a_graph = -1, arm_b_graph = -1, leg_graph = -1;
};

// from the part packs of its classes (body_C, leg_C, arm_a_C, arm_b_C, back_a_C, back_b_C) and
// its leg class's paint pack, each closed again once loaded. parts_only: a build missing its
// body or legs loads what it has (the garage stands' parts laid out, scoobee_parts_draw_spread)
bool load_mech(MechParts& mech, const FighterTables& tables, int build,
               const AssetManifest& manifest, PackLibrary& packs, Renderer& renderer,
               TextureCache& cache, std::string& error, bool parts_only = false);

// load_mech in its three steps (src/mech/mech_load.cpp), for a loader thread: open on the main
// thread (it may cook a pack), read on any thread (it touches only the packs open opened and its
// own copies), then upload and close on the main thread. Between open and close nothing else may
// close those packs.
struct MechPicture {
    const Pack* pack = nullptr;
    std::string path; // the texture's name in the pack, the cache's key
    BlockFormat format = BlockFormat::bc7;
    int width = 0, height = 0, levels = 0;
    std::vector<std::uint8_t> blocks; // read's copy
};
struct MechLoad {
    std::vector<std::string> opened; // pack names, closed by mech_load_close
    const Pack* part_pack[part_count] = {};
    int model_asset[part_count] = {-1, -1, -1, -1, -1, -1};
    int motion_asset[4] = {-1, -1, -1, -1}; // legs, body, arm A, arm B
    int sticker_model = -1;
    MechPicture paint, sticker;
    std::string error; // read's
};
bool mech_load_open(MechParts& mech, MechLoad& load, const FighterTables& tables, int build,
                    const AssetManifest& manifest, PackLibrary& packs, std::string& error,
                    bool parts_only = false);
bool mech_load_read(MechParts& mech, MechLoad& load);
void mech_load_upload(MechParts& mech, MechLoad& load, Renderer& renderer, TextureCache& cache);
void mech_load_close(MechLoad& load, PackLibrary& packs);
// the pose input for a motion slot at a frame, standing at world
MechPoseInput mech_pose_input(const MechParts& mech, int slot, float frame, const Mat4& world);
struct BattleData;
// --pose-compare (pose_compare.cpp): the world's fixed-point pose of the build against the float
// walk over its clips, printed
void pose_compare(const MechParts& mech, const BattleData& data, int build);
// the fighter's draw items for a pose (pass 4 in the original, here drawn after the stage's pass 2)
// (body false: the weapons only, actor_draw's cockpit view)
void mech_draw_items(const MechParts& mech, const MechPose& pose, const float eye[3],
                     std::vector<PassItem>& out, bool body = true);
// actor_draw's sticker: the sticker model at the body matrix in pass 2 with flags 0x400 (a
// cutout), or cloaked in pass 8 with the cloak's alpha on the texture factor
void mech_sticker_items(const MechParts& mech, const MechPose& pose, int pass,
                        std::uint32_t flags, std::uint32_t factor, std::vector<PassItem>& out);
