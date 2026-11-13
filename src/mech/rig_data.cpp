// The pose's data in fixed point (mech/rig_data.hpp): the fighter tables' graphs and, per build,
// the classes, motion sets and weapon model node trees that load_mech (mech_parts.cpp) reads for
// the drawing, converted by their float bits. fighter_assets_load picks the same assets.
#include "mech/rig_data.hpp"

#include "assets/fighter_tables.hpp"
#include "assets/manifest.hpp"
#include "assets/model_sets.hpp"
#include "assets/motion_sets.hpp"
#include "battle/battle_data.hpp"
#include "cook/pack_library.hpp"

#include <bit>

namespace {

Fx float_fx(float value) {
    return fx_bits(std::bit_cast<std::uint32_t>(value));
}
Angle float_angle(float radians) {
    return angle_from_radians(float_fx(radians));
}

FxCurve curve_fx(const MotionCurve& curve) {
    FxCurve out;
    out.mode = curve.mode;
    out.times = curve.times;
    out.present = curve.present;
    for (float value : curve.values) {
        out.values.push_back(float_fx(value));
    }
    return out;
}

// the curves in the order motion_key_stream's cursor walks them (rotation, translation, steps),
// the three rotation streams taken, then the three footstep streams' curves
void footstep_markers(const MotionTrack& track, FxTrack& out) {
    if (!(track.flags & track_steps)) {
        return;
    }
    std::vector<const MotionCurve*> order;
    for (int k = 0; k < 3; ++k) {
        if (track.flags & track_rotate) {
            order.push_back(&track.rotate[k]);
        }
    }
    for (int k = 0; k < 3; ++k) {
        if (track.flags & track_translate) {
            order.push_back(&track.translate[k]);
        }
    }
    for (int k = 0; k < 3; ++k) {
        order.push_back(&track.steps[k]);
    }
    std::size_t at = 0;
    for (int k = 0; k < 3 && (track.flags & track_rotate); ++k) {
        at += order[at]->mode != 0 ? 1 : 0;
    }
    for (int k = 0; k < 3 && at < order.size(); ++k) {
        const MotionCurve& c = *order[at];
        if (c.mode != 0) {
            out.markers[k] = c.times;
            at += 1;
        }
    }
}

void motion_fx(const MotionSet& set, FxMotionSet& out) {
    out.slot_clips = set.slot_clips;
    for (const MotionClip& clip : set.clips) {
        FxClip& fx_clip = out.clips.emplace_back();
        for (const MotionTrack& track : clip) {
            FxTrack& t = fx_clip.emplace_back();
            t.flags = track.flags;
            for (int k = 0; k < 3; ++k) {
                t.rotate[k] = curve_fx(track.rotate[k]);
                t.translate[k] = curve_fx(track.translate[k]);
            }
            footstep_markers(track, t);
        }
    }
}

// a weapon block's streams, clip and frames (weapon_anim_init: an unlocked clip plays
// from 0 to its end)
// decomp: weapon_anim_init 0x9AEF5
void block_fx(const WeaponBlock& in, RigWeaponBlock& out) {
    for (int k = 0; k < 4; ++k) {
        for (const WeaponMuzzle& m : in.streams[k]) {
            out.streams[k].push_back(
                {{float_fx(m.point[0]), float_fx(m.point[1]), float_fx(m.point[2])}, m.node});
        }
    }
    out.clip = in.clip;
    out.end = in.clip ? fx((in.frames & 0x7FFF) - 1) : Fx{};
    out.locked = in.locked;
    out.open = in.locked ? fx(in.open) : Fx{};
    out.close = in.locked ? fx(in.close) : out.end;
    for (const WeaponClipChannel& c : in.channels) {
        RigClipChannel& channel = out.channels.emplace_back();
        channel.node = c.node;
        channel.flags = c.flags;
        for (int k = 0; k < 3; ++k) {
            channel.rotate[k] = curve_fx(c.rotate[k]);
            channel.translate[k] = curve_fx(c.translate[k]);
        }
    }
}

int table_at(const std::vector<int>& table, int index) {
    return index >= 0 && index < static_cast<int>(table.size()) ? table[index] : -1;
}

const Pack* asset_pack(PackLibrary& packs, const AssetManifest& manifest, int asset,
                       std::string& error) {
    const auto entry = manifest.assets.find(asset);
    if (entry == manifest.assets.end() || entry->second.pack.empty()) {
        error = "asset " + std::to_string(asset) + " is in no pack";
        return nullptr;
    }
    return pack_open(packs, entry->second.pack, error);
}

bool load_motion(PackLibrary& packs, const AssetManifest& manifest, int asset, FxMotionSet& out,
                 std::string& error) {
    if (asset < 0) {
        return true;
    }
    const Pack* pack = asset_pack(packs, manifest, asset, error);
    MotionSet set;
    if (!pack || !load_motion_set(*pack, asset, set, error)) {
        return false;
    }
    motion_fx(set, out);
    return true;
}

// objects[0]'s node tree, as the drawing places it (mech_parts.cpp, place_nodes)
bool load_model_nodes(PackLibrary& packs, const AssetManifest& manifest, int asset,
                      std::vector<RigModelNode>& out, std::string& error) {
    if (asset < 0) {
        return true;
    }
    const Pack* pack = asset_pack(packs, manifest, asset, error);
    ModelSet set;
    if (!pack || !load_model_set(*pack, asset, set, error)) {
        return false;
    }
    if (set.objects.empty()) {
        return true;
    }
    for (const ModelNode& node : set.objects[0].nodes) {
        out.push_back(
            {node.parent,
             {float_fx(node.translate[0]), float_fx(node.translate[1]),
              float_fx(node.translate[2])},
             {float_angle(node.euler[0]), float_angle(node.euler[1]), float_angle(node.euler[2])},
             node.flags});
    }
    return true;
}

// a build's rig: load_mech's classes and assets
bool load_rig(FighterRig& rig, const FighterTables& tables, PackLibrary& packs,
              const AssetManifest& manifest, int build, std::string& error) {
    const FighterBuild& row =
        tables.builds[build < 0 || build >= static_cast<int>(tables.builds.size()) ? 0 : build];
    rig.build = build;
    rig.body_class = table_at(tables.body_class, row.body);
    rig.leg_class = table_at(tables.leg_class, row.leg);
    for (int mount = 0; mount < 4; ++mount) {
        rig.weapon_fitted[mount] = row.weapons[mount] >= 0;
        const std::vector<int>& classes = mount < 2 ? tables.arm_class : tables.back_class;
        rig.weapon_class[mount] =
            row.weapons[mount] >= 0 ? table_at(classes, row.weapons[mount]) : -1;
    }
    // body class 2 wears its own pair on empty back mounts: back_model_assets_mount2/3[30]
    // (actor_back_weapons_place)
    for (int mount = 2; mount < 4; ++mount) {
        if (!rig.weapon_fitted[mount] && rig.body_class == 2) {
            rig.weapon_class[mount] = 30;
        }
    }
    rig.leg_graph = table_at(tables.leg_graphs, rig.leg_class);
    rig.arm_graph[0] =
        rig.weapon_class[0] >= 0 ? table_at(tables.arm_a_graphs, rig.weapon_class[0]) : -1;
    rig.arm_graph[1] =
        rig.weapon_class[1] >= 0 ? table_at(tables.arm_b_graphs, rig.weapon_class[1]) : -1;
    const auto arm_motion = [&](const std::vector<int>& motions, int arm_class) {
        const int motion_class = table_at(tables.arm_motion_class, arm_class);
        return motion_class >= 0 ? table_at(motions, rig.leg_class * 2 + motion_class) : -1;
    };
    const std::vector<int>* models[4] = {&tables.arm_a_models, &tables.arm_b_models,
                                         &tables.back_a_models, &tables.back_b_models};
    if (!load_motion(packs, manifest, table_at(tables.leg_motions, rig.leg_class), rig.leg_motion,
                     error) ||
        !load_motion(packs, manifest, table_at(tables.body_motions, rig.body_class),
                     rig.body_motion, error) ||
        !load_motion(packs, manifest, arm_motion(tables.arm_a_motions, rig.weapon_class[0]),
                     rig.arm_motion[0], error) ||
        !load_motion(packs, manifest, arm_motion(tables.arm_b_motions, rig.weapon_class[1]),
                     rig.arm_motion[1], error)) {
        return false;
    }
    // the fitted weapons' blocks (fighter_assets_load's asset_request into Weapon.model)
    const std::vector<int>* blocks[4] = {&tables.arm_a_blocks, &tables.arm_b_blocks,
                                         &tables.back_a_blocks, &tables.back_b_blocks};
    for (int mount = 0; mount < 4; ++mount) {
        const int block =
            rig.weapon_fitted[mount] ? table_at(*blocks[mount], rig.weapon_class[mount]) : -1;
        for (const WeaponBlockMuzzles& muzzles : tables.block_muzzles) {
            if (block < 0 || muzzles.asset != block) {
                continue;
            }
            for (const WeaponMuzzle& m : muzzles.muzzles) {
                rig.muzzles[mount].push_back(
                    {{float_fx(m.point[0]), float_fx(m.point[1]), float_fx(m.point[2])}, m.node});
            }
        }
        for (const WeaponBlock& b : tables.blocks) {
            if (block >= 0 && b.asset == block) {
                block_fx(b, rig.blocks[mount]);
            }
        }
    }
    for (int mount = 0; mount < 4; ++mount) {
        if (rig.weapon_class[mount] >= 0 &&
            !load_model_nodes(packs, manifest, table_at(*models[mount], rig.weapon_class[mount]),
                              rig.weapon_model[mount], error)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool load_fighter_rigs(BattleData& data, PackLibrary& packs, const AssetManifest& manifest,
                       const std::vector<int>& builds, std::string& error,
                       const std::vector<FighterBuild>& extra) {
    FighterTables tables;
    if (!load_fighter_tables(packs.core, tables, error)) {
        return false;
    }
    tables.builds.insert(tables.builds.end(), extra.begin(), extra.end());
    // the graphs
    data.rig_nodes.clear();
    for (const SvGraphNode& node : tables.nodes) {
        RigNode& out = data.rig_nodes.emplace_back();
        out.type = node.type;
        out.flags = node.flags;
        out.track = node.track;
        out.translate = {float_fx(node.translate[0]), float_fx(node.translate[1]),
                         float_fx(node.translate[2])};
        for (int k = 0; k < 3; ++k) {
            out.rotate[k] = float_angle(node.rotate[k]);
        }
        // the sign of the float itself: a twist of -7e-10 is under one Q16 step
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(node.rotate[2]);
        out.twist_positive = (bits & 0x80000000u) == 0 || (bits & 0x7FFFFFFFu) == 0;
        out.matrix = node.matrix;
        out.spheres = node.spheres;
        out.children = node.children;
    }
    // a rig per build
    for (int build : builds) {
        if (rig_of_build(data, build)) {
            continue;
        }
        FighterRig rig;
        if (!load_rig(rig, tables, packs, manifest, build, error)) {
            error = "build " + std::to_string(build) + " pose: " + error;
            return false;
        }
        data.rigs.push_back(std::move(rig));
    }
    return true;
}

const FxClip* fx_motion_slot(const FxMotionSet& set, int slot) {
    if (slot < 0 || slot >= static_cast<int>(set.slot_clips.size())) {
        return nullptr;
    }
    const int clip = set.slot_clips[slot];
    return clip >= 0 && clip < static_cast<int>(set.clips.size()) ? &set.clips[clip] : nullptr;
}

const FxTrack* fx_track(const FxClip* clip, int track) {
    return clip && track >= 0 && track < static_cast<int>(clip->size()) ? &(*clip)[track] : nullptr;
}

const FighterRig* rig_of_build(const BattleData& data, int build) {
    for (const FighterRig& rig : data.rigs) {
        if (rig.build == build) {
            return &rig;
        }
    }
    return nullptr;
}

// decomp: motion_curve_eval 0xAB02C
Fx fx_curve_value(const FxCurve& curve, Fx frame) {
    if (!curve.present) {
        return Fx{};
    }
    const std::vector<Fx>& v = curve.values;
    const int count = static_cast<int>(curve.times.size());
    const auto value = [&](int i) {
        return i >= 0 && i < static_cast<int>(v.size()) ? v[i] : Fx{};
    };
    if (curve.mode == 0) {
        return value(0);
    }
    // the first key at or after the frame (motion_curve_value's walk, in the same order; retail
    // walks on past the last key, the port stops there)
    int key = 0;
    while (key < count - 1 && fx(curve.times[key]) < frame) {
        ++key;
    }
    const Fx t0 = key == 0 ? Fx{} : fx(curve.times[key - 1]);
    const Fx t1 = count ? fx(curve.times[key]) : Fx{};
    if (curve.mode == curve_step) {
        return value(key);
    }
    const int stride = curve.mode == curve_linear || curve.mode == curve_ease ? 1
                       : curve.mode == curve_both_tangents                    ? 3
                                                                              : 2;
    const int at = key * stride;
    if (t0 == frame || t1 == t0) {
        return value(at);
    }
    const Fx v0 = value(at);
    const Fx v1 = value(at + stride);
    const Fx dt = frame - t0;
    const Fx u = fx_div_near(dt, t1 - t0);
    const Fx u1 = u - fx(1);
    const Fx shape = u1 + u1 - fx(1); // 2u - 3: with u^2 (v0 - v1) the cubic ease
    const Fx eased = fx_mul_near(fx_mul_near(fx_mul_near(shape, v0 - v1), u), u) + v0;
    const Fx u1dt = fx_mul_near(u1, dt);
    switch (curve.mode) {
    case curve_linear:
        return fx_mul_div(v1 - v0, dt, t1 - t0) + v0;
    case curve_ease:
        return eased;
    case curve_out_tangent: // out tangent only
        return eased + fx_mul_near(u1dt, fx_mul_near(u, value(at + 1)));
    case curve_in_tangent: // in tangent only
        return eased + fx_mul_near(u1dt, fx_mul_near(u1, value(at + 1)));
    case curve_both_tangents: // in and out, three values a key
        return eased +
               fx_mul_near(u1dt, fx_mul_near(u1, value(at + 1)) + fx_mul_near(u, value(at + 2)));
    default: // curve_in_then_out: in, then the out tangent after the next value
        return eased +
               fx_mul_near(u1dt, fx_mul_near(u1, value(at + 1)) + fx_mul_near(u, value(at + 3)));
    }
}
