#include "battle/battle_data.hpp"

#include "assets/collision_grid.hpp"
#include "assets/manifest.hpp"
#include "assets/motion_sets.hpp"
#include "assets/pack_sexp.hpp"
#include "assets/stage_tables.hpp"
#include "cook/cook_plan.hpp"
#include "cook/pack_library.hpp"
#include "battle/sexp_fx.hpp"
#include "assets/fighter_tables.hpp"
#include "cook/sexp_read.hpp"

#include <bit>
#include <cstdio>

namespace {

Fx float_fx(float value) {
    return fx_bits(std::bit_cast<std::uint32_t>(value));
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

// fighter_assets.sexp's per-leg-class tables: the motion timing, the radius, the root-motion
// scales, and each class's motion set asset
bool load_leg_tables(BattleData& data, const Pack& core, std::vector<int>& motions,
                     std::string& error) {
    gsexp::ParseResult parsed;
    const char* name = "tables/fighter_assets.sexp";
    if (!pack_sexp(core, name, parsed, error)) {
        return false;
    }
    bool radius = false, root = false;
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node form = parsed.root(i);
        const gsexp::Node head = form.head();
        if (head.is_atom("leg-motions")) {
            for (std::size_t k = 1; k < form.child_count(); ++k) {
                motions.push_back(atom_int(form.child_at(k)));
            }
        } else if (head.is_atom("motion-timing")) {
            // (leg n (root-track phase-a phase-b phase-end) x 37)
            for (gsexp::Node leg : form.children()) {
                const int index = leg.is_list() ? atom_int(leg.child_at(1)) : -1;
                if (index < 0 || index >= leg_class_count) {
                    continue;
                }
                for (int slot = 0; slot < motion_slot_count; ++slot) {
                    const gsexp::Node row = leg.child_at(2 + slot);
                    MotionTiming& timing = data.legs[index].timing[slot];
                    timing.root_track = atom_int(row.child_at(0));
                    timing.loop_start = atom_fx(row.child_at(1));
                    timing.loop_end = atom_fx(row.child_at(2));
                    timing.end = atom_fx(row.child_at(3));
                }
            }
        } else if (head.is_atom("leg-radius") || head.is_atom("leg-root-motion")) {
            const bool is_radius = head.is_atom("leg-radius");
            for (gsexp::Node row : form.children()) {
                const int index = row.is_list() ? atom_int(row.child_at(0)) : -1;
                if (index < 0 || index >= leg_class_count) {
                    continue;
                }
                LegClassData& leg = data.legs[index];
                if (is_radius) {
                    // actor_init: the second column, clamped 0..2
                    leg.radius = fx_clamp(atom_fx(row.child_at(2)), Fx{}, fx(2));
                } else {
                    leg.walk_root_scale = atom_fx(row.child_at(1));
                    leg.run_root_scale = atom_fx(row.child_at(2));
                }
            }
            (is_radius ? radius : root) = true;
        } else if (head.is_atom("leg-attachments")) {
            // (leg n asset (a.x a.y a.z selector b.x b.y b.z) x 14), the node's frame, port frame
            for (gsexp::Node leg : form.children()) {
                const int index = leg.is_list() ? atom_int(leg.child_at(1)) : -1;
                if (index < 0 || index >= leg_class_count) {
                    continue;
                }
                for (int k = 0; k < leg_attachment_count && 3 + k < static_cast<int>(leg.child_count()); ++k) {
                    const gsexp::Node row = leg.child_at(3 + k);
                    LegAttachment& out = data.legs[index].attachments[k];
                    out.a = {atom_fx(row.child_at(0)), atom_fx(row.child_at(1)), atom_fx(row.child_at(2))};
                    out.selector = atom_int(row.child_at(3));
                    out.b = {atom_fx(row.child_at(4)), atom_fx(row.child_at(5)), atom_fx(row.child_at(6))};
                }
            }
        }
    }
    if (!radius || !root || motions.size() < leg_class_count) {
        error = std::string(name) +
                " has no leg-radius or leg-root-motion: run pc_extract --only fighters";
        return false;
    }
    return true;
}

// a leg class's motion set (from the pack the manifest names): the root track's x and z curves
// by slot
bool load_leg_motion(BattleData& data, PackLibrary& packs, const AssetManifest& manifest,
                     int leg, int asset, std::string& error) {
    const auto entry = manifest.assets.find(asset);
    const Pack* pack =
        entry == manifest.assets.end() ? nullptr : pack_open(packs, entry->second.pack, error);
    MotionSet set;
    if (!pack || !load_motion_set(*pack, asset, set, error)) {
        error = "leg class " + std::to_string(leg) + " motions: " + error;
        return false;
    }
    for (int slot = 0; slot < motion_slot_count; ++slot) {
        const MotionClip* clip = motion_slot(set, slot);
        const int track = data.legs[leg].timing[slot].root_track;
        if (!clip || track < 0 || track >= static_cast<int>(clip->size())) {
            continue;
        }
        RootCurves& root = data.legs[leg].root[slot];
        root.x = curve_fx((*clip)[track].translate[0]);
        root.z = curve_fx((*clip)[track].translate[2]);
        root.present = true;
    }
    data.legs[leg].motion_loaded = true;
    return true;
}

// the cooked grid (floats, as read from data/) to Q16.16
bool stage_collision_from(StageCollision& grid, const CollisionGrid& in, std::string& error) {
    if (in.cells.size() != 32 * 4 * 32 || in.floor_cells.size() != 32 * 32) {
        error = "the cell arrays aren't 32 x 4 x 32 and 32 x 32";
        return false;
    }
    for (int k = 0; k < 3; ++k) {
        grid.cell[k] = float_fx(in.cell[k]);
    }
    grid.centre_original = {float_fx(in.centre_original[0]), float_fx(in.centre_original[1]),
                            float_fx(in.centre_original[2])};
    const auto triangles = [](const std::vector<CollisionTriangle>& from,
                              std::vector<StageTriangle>& out) {
        for (const CollisionTriangle& t : from) {
            const auto point = [](const float* p) {
                return FxVec3{float_fx(p[0]), float_fx(p[1]), float_fx(p[2])};
            };
            out.push_back({{point(t.v[0]), point(t.v[1]), point(t.v[2]), point(t.normal)},
                           t.type});
        }
    };
    triangles(in.triangles, grid.tris);
    triangles(in.floor_triangles, grid.floor_tris);
    grid.cells.assign(in.cells.begin(), in.cells.end());
    grid.refs.assign(in.refs.begin(), in.refs.end());
    grid.floor_cells.assign(in.floor_cells.begin(), in.floor_cells.end());
    grid.floor_refs.assign(in.floor_refs.begin(), in.floor_refs.end());
    // an extract before the surface bits were written has none, which changes no movement (no
    // shipped type gives 8 or 0x40000) but loses the contact decals' bit 0x2000
    bool any = false;
    for (int type = 0; type < 11; ++type) {
        grid.surface_bits[type] = in.surface_bits[type];
        any = any || in.surface_bits[type] != 0;
    }
    if (!any) {
        std::fprintf(stderr, "collision grid %d: no surface bits (run pc_extract --only "
                             "collision)\n",
                     in.asset);
    }
    grid.loaded = true;
    return true;
}

} // namespace

namespace {

// camera_rails.sexp (pc_extract --only scenes): (rails MODE (points (x y z)...) (zones (a b)...)),
// the battle's day mode only; none in the data leaves view 5 on the front view's eye
void load_camera_rails(CameraRails& out, const Pack& core, int selector) {
    out = CameraRails{};
    out.selector = selector;
    gsexp::ParseResult parsed;
    std::string error;
    if (!pack_sexp(core, "tables/camera_rails.sexp", parsed, error)) {
        return;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node form = parsed.root(i);
        if (!form.is_list() || !form.head().is_atom("rails") ||
            atom_int(form.child_at(1)) != selector) {
            continue;
        }
        const gsexp::FormView rails(form);
        for (gsexp::Node p : rails.find("points").children()) {
            if (p.is_list()) {
                out.points.push_back({float_fx(atom_float(p.child_at(0))),
                                      float_fx(atom_float(p.child_at(1))),
                                      float_fx(atom_float(p.child_at(2)))});
            }
        }
        int zone = 0;
        for (gsexp::Node z : rails.find("zones").children()) {
            if (z.is_list() && zone < 5) {
                out.zones[zone][0] = atom_int(z.child_at(0));
                out.zones[zone][1] = atom_int(z.child_at(1));
                ++zone;
            }
        }
    }
}

} // namespace

FighterBuild fighter_build_of(const BuildRecord& row) {
    FighterBuild b;
    b.body = row.body;
    b.leg = row.leg;
    for (int w = 0; w < 4; ++w) {
        b.weapons[w] = row.weapon[w];
    }
    b.colour = row.colour;
    b.sticker = row.sticker;
    return b;
}

bool load_battle_data(BattleData& data, PackLibrary& packs, int stage,
                      const std::vector<int>& builds, std::string& error,
                      const std::vector<BuildRecord>& extra) {
    // the tables from the core pack
    AssetManifest manifest;
    StageTables stages;
    std::vector<int> motions;
    if (!load_manifest(packs.core, manifest, error) ||
        !load_stage_tables(packs.core, stages, error) ||
        !load_part_records(data.parts, packs.core, error) ||
        !load_combat_tables(data.combat, packs.core, error) ||
        !load_leg_tables(data, packs.core, motions, error) ||
        !load_scene_walks(data.scene_walks, packs.core, error)) {
        return false;
    }
    // the ending's shots: only the ending reads them, so a data/ without them still plays
    if (!load_ending_shots(data.ending, packs.core, error)) {
        error.clear();
    }
    data.parts.builds.insert(data.parts.builds.end(), extra.begin(), extra.end());
    // the AI's tables and the stage's route graph (ai_nav_resource_select)
    if (!load_ai_data(data.ai, packs.core, packs, stage < 0 || stage > 7 ? 0 : stage, error)) {
        error = "AI: " + error;
        return false;
    }
    // the stage's grid from its pack, and its entry points
    const int selector = stage < 0 || stage > 7 ? 0 : stage;
    load_camera_rails(data.rails, packs.core, selector);
    const StageInfo& info = stages.stages[selector];
    const Pack* stage_pack = pack_open(packs, stage_pack_name(selector), error);
    CollisionGrid grid;
    if (!stage_pack || !load_collision_grid(*stage_pack, info.collision, grid, error) ||
        !stage_collision_from(data.stage, grid, error)) {
        error = "stage " + std::to_string(stage) + " collision: " + error;
        return false;
    }
    for (const StageEntryPoint& point : info.entry_points) {
        data.entries.push_back({{float_fx(point.position[0]), float_fx(point.position[1]),
                                 float_fx(point.position[2])},
                                angle_from_radians(float_fx(point.yaw))});
    }
    // the root motion of every leg class the builds use, from its leg pack
    for (int build : builds) {
        const std::vector<BuildRecord>& rows = data.parts.builds;
        const BuildRecord& row = rows[build < 0 || build >= static_cast<int>(rows.size()) ? 0 : build];
        const int leg_row = row.leg < 0 || row.leg >= static_cast<int>(data.parts.legs.size())
                                ? 0
                                : row.leg;
        const int leg = data.parts.legs[static_cast<std::size_t>(leg_row)].geometry;
        if (leg < 0 || leg >= leg_class_count || data.legs[leg].motion_loaded) {
            continue;
        }
        if (!load_leg_motion(data, packs, manifest, leg, motions[static_cast<std::size_t>(leg)],
                             error)) {
            return false;
        }
    }
    // what each build is posed from
    std::vector<FighterBuild> extra_rows;
    for (const BuildRecord& row : extra) {
        extra_rows.push_back(fighter_build_of(row));
    }
    return load_fighter_rigs(data, packs, manifest, builds, error, extra_rows);
}
