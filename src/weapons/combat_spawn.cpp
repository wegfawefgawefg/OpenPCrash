// A fighter's weapons and damage numbers at spawn (decomp src/hand/weapon_laser.c
// fighter_weapons_spawn, weapon_task_setup, blast_weapon_setup; src/hand/
// curve.c weapon_materialize_tuned_arm; src/hand/mech_part_assets.c build_durability,
// damage_options_apply) and the muzzle.
#include "mech/rig_data.hpp"
#include "weapons/weapons.hpp"
#include "world/world.hpp"

#include <algorithm>

namespace {

// the blast weapon: back row 0x24 "object blast only" at tune 0
constexpr int blast_weapon_row = 0x24;

// body_durability_floor (a human's) by body class; an AI's body floor is 0
// (body_durability_floor_ai)
// decomp: body_durability_floor 0x92D12
int body_floor(int body_class, bool ai) {
    constexpr int floors[7] = {6000, 4500, 7500, 9000, 9000, 10500, 100};
    return ai ? 0 : body_class >= 0 && body_class < 7 ? floors[body_class] : 100;
}
// leg_durability_floor (100) and leg_durability_floor_ai (2000, 600 for class 20)
// decomp: leg_durability_floor 0x92D8B, leg_durability_floor_ai 0x92F37
int leg_floor(int leg_class, bool ai) {
    return !ai ? 100 : leg_class == 20 ? 600 : 2000;
}

template <typename T> const T* row_at(const std::vector<T>& rows, int row) {
    if (row == -1 || rows.empty()) {
        return nullptr;
    }
    return row < 0 || row >= static_cast<int>(rows.size()) ? &rows[0] : &rows[row];
}

// half extents (lo + (hi - lo) x size / 999) pixels of a 640 x 480
// screen, in -1..1 units. Rows of D_00203B64 + 4 (the tracking box) and D_00203B98 (the lock box)
// decomp: reticle_box_size 0x4DAE9
void reticle_box_size(int kind, Fx size, bool lock_box, Fx out[2]) {
    constexpr int box_a[3][4] = {{40, 120, 40, 120}, {60, 180, 20, 60}, {20, 60, 60, 180}};
    constexpr int box_b[3][4] = {{70, 160, 70, 160}, {135, 210, 45, 70}, {45, 70, 135, 210}};
    const int* row =
        lock_box ? box_b[kind < 0 || kind > 2 ? 0 : kind] : box_a[kind < 0 || kind > 2 ? 0 : kind];
    const Fx t = fx_div_near(size, fx(999));
    out[0] = fx_div_near(fx(row[0]) + fx_mul_near(fx(row[1] - row[0]), t), fx(320));
    out[1] = fx_div_near(fx(row[2]) + fx_mul_near(fx(row[3] - row[2]), t), fx(240));
}

// the record's stats at the tune
// decomp: weapon_materialize_tuned_arm 0x92987
void materialize(Weapon& w, const WeaponRecord& r, int tune) {
    w.record = &r;
    w.weight = curve_value(r.weight, tune);
    w.elevation = angle_from_degrees(fx(r.elevation_deg));
    w.reload_frames = r.reload_frames;
    w.shot_life = r.shot_life;
    w.speed = r.speed;
    w.speed_max = r.speed_max;
    w.accel = r.accel;
    w.pellet_spread = angle_from_degrees(fx(r.pellet_spread_deg));
    w.turn_rate = angle_from_degrees(fx_max(curve_value(r.turn_deg, tune), r.turn_min_deg));
    w.shots = std::max(curve_int(r.shots, tune), r.shots_min);
    w.damage = std::max(curve_int(r.damage, tune), r.damage_min);
    w.blast_radius_end = curve_value(r.blast_radius_end, tune);
    w.blast_radius_start = fx(r.blast_radius_start);
    w.impact_frames = curve_value(r.impact_frames, tune);
    w.impact_damage = curve_value(r.impact_damage, tune);
    w.resource_max = std::max(curve_int(r.resource, tune), r.resource_min);
    w.recharge_frames = curve_value(r.recharge_frames, tune);
    w.stagger = curve_value(r.stagger, tune);
    w.knockback = curve_value(r.knockback, tune);
    w.break_chance = curve_value(r.break_chance, tune);
}

// weapon_task_setup's state: no volley, reload 0, a full magazine
void weapon_reset(Weapon& w, int id, int mount) {
    w.id = id;
    w.mount = mount;
    w.fitted = true;
    w.present = true;
    w.beam_shot = -1;
    w.reload_left = 0;
    w.lock_target = -1;
    w.volley_active = false;
    w.volley_index = 0;
    w.volley_timer = 0;
    w.muzzle = 0;
    w.resource = fx(w.resource_max);
}

} // namespace

void combat_spawn(Fighter& f, const BattleData& data, const BuildRecord& build) {
    FighterCombat& c = f.combat;
    c = FighterCombat{};
    const CombatTables& t = data.combat;
    const bool ai = f.controller == -1;

    // build_durability: body and legs at their tunes raised to the floors, plus options
    int body_class = 0;
    if (const BodyRecord* body = row_at(data.parts.bodies, build.body)) {
        body_class = body->body_class;
    }
    int total = 0;
    if (const StatCurve* curve = row_at(t.body_durability, build.body)) {
        total += std::max(curve_int(*curve, build.body_tune), body_floor(body_class, ai));
    }
    if (const StatCurve* curve = row_at(t.leg_durability, build.leg)) {
        total += std::max(curve_int(*curve, build.leg_tune), leg_floor(f.leg_class, ai));
    }
    for (int option : build.option) {
        if (const OptionCombat* o = row_at(t.options, option)) {
            total += o->durability;
        }
    }
    c.durability = c.durability_max = total;
    c.alive = true;

    // the chip, then damage_options_apply: crit and break resist summed with the options, the
    // laser cut summed and clamped 0..100 and the shield clamped 0..100, both as fractions
    int laser = 0;
    Fx shield;
    if (const ChipRecord* chip = row_at(t.chips, build.chip)) {
        const int tune = build.chip_tune;
        c.reticle_kind_a = chip->reticle_kind_a;
        c.reticle_size_a = curve_value(chip->reticle_size_a, tune);
        c.reticle_speed = chip->reticle_speed;
        c.reticle_kind_b = chip->reticle_kind_b;
        c.reticle_size_b = curve_value(chip->reticle_size_b, tune);
        c.lock_frames = chip->lock_frames;
        c.crit_chance = curve_value(chip->crit_chance, tune);
        shield = curve_value(chip->shield_chance, tune);
        c.break_resist = curve_value(chip->break_resist, tune);
    }
    int camo_frames = 0, camo_recharge = 0;
    for (int option : build.option) {
        if (const OptionCombat* o = row_at(t.options, option)) {
            c.crit_chance += o->crit_chance;
            c.break_resist += o->break_resist;
            laser += o->laser_reduction;
            camo_frames += o->camo_frames;
            camo_recharge += o->camo_recharge;
        }
    }
    // actor_init's camo_init from the options' sums; the chip's camo_distortion
    // (chip_shader_param) for the drawn look
    camo_init(f.camo, camo_frames, camo_recharge);
    if (const ChipRecord* chip = row_at(t.chips, build.chip)) {
        f.camo.chip_shader_param = curve_int(chip->camo_distortion, build.chip_tune);
    }
    c.laser_reduction = fx_ratio(std::clamp(laser, 0, 100), 100);
    c.shield_chance = fx_clamp(shield, Fx{}, fx(100)) / 100;
    reticle_box_size(c.reticle_kind_a, c.reticle_size_a, false, c.hud.box_a);
    reticle_box_size(c.reticle_kind_b, c.reticle_size_b, true, c.hud.box_b);

    // fighter_weapons_spawn: the blast weapon, then each mount
    if (const WeaponRecord* blast = row_at(t.backs, blast_weapon_row)) {
        materialize(c.blast_weapon, *blast, 0);
        weapon_reset(c.blast_weapon, blast_weapon_row, 0);
    }
    for (int mount = 0; mount < 4; ++mount) {
        Weapon& w = c.weapons[mount];
        const WeaponRecord* record = mount_weapon_record(t, mount, build.weapon[mount]);
        if (!record) {
            w = Weapon{};
            w.mount = mount;
            continue; // the task is paused
        }
        materialize(w, *record, build.weapon_tune[mount]);
        weapon_reset(w, build.weapon[mount], mount);
        if (const FighterRig* rig = rig_of_build(data, f.build)) {
            weapon_clip_init(w, rig->blocks[mount]);
        }
        c.triggers[mount].ranged = record->behavior != 6;
        c.mount_free_aim[mount] = record->lock_rule == 1;
        if (record->lock_rule == 2 && mount >= 2) {
            c.hud.mount_lock_max[mount - 2] = w.shots;
        }
        // arm_melee_clips_set: geometry 7 swings with clips 0x19 / 0x1A
        if (mount < 2) {
            c.melee_clip[mount] = record->geometry == 7 ? 0x19 + mount : 0x17 + mount;
        }
        weapon_reach_update(w);
    }

    // the damage state as the first settle leaves it
    c.damage.center = f.position + FxVec3{Fx{}, f.radius, Fx{}};
    c.damage.sphere_radius = fx_mul_near(f.radius, fx_f(0.7));
    c.damage.facing = turned_by({Fx{}, Fx{}, fx(1)}, f.heading); // (sin h, 0, cos h)
    c.aim_point = c.damage.center;
}

Weapon& weapon_at(World& world, int ref) {
    FighterCombat& c = world.fighters[static_cast<std::size_t>(ref / 5)].combat;
    return ref % 5 == blast_mount ? c.blast_weapon : c.weapons[ref % 5];
}
const Weapon& weapon_at(const World& world, int ref) {
    const FighterCombat& c = world.fighters[static_cast<std::size_t>(ref / 5)].combat;
    return ref % 5 == blast_mount ? c.blast_weapon : c.weapons[ref % 5];
}

void combat_event(World& world, CombatEventKind kind, int slot, int value, FxVec3 at, int effect,
                  FxVec3 dir, unsigned flags, int surface) {
    CombatWorld& c = world.combat;
    if (c.event_count >= combat_event_max) {
        return;
    }
    c.events[static_cast<std::size_t>(c.event_count++)] = {kind, world.step, slot, value, effect,
                                                           at,   dir,        flags, surface};
}

namespace {

// the fitted weapon's block muzzles (weapon_muzzle_count is their count)
const std::vector<RigMuzzle>* mount_muzzles(const World& world, const Fighter& f, int mount) {
    const FighterRig* rig = world.battle ? rig_of_build(*world.battle, f.build) : nullptr;
    return rig && mount >= 0 && mount < 4 ? &rig->muzzles[mount] : nullptr;
}

} // namespace

bool fighter_muzzle(const World& world, const Fighter& f, int mount, int barrel, FxMat4* out) {
    const std::vector<RigMuzzle>* muzzles = mount_muzzles(world, f, mount);
    if (!muzzles || barrel < 0 || barrel >= static_cast<int>(muzzles->size())) {
        return false;
    }
    const RigMuzzle& muzzle = (*muzzles)[static_cast<std::size_t>(barrel)];
    if (muzzle.node < 0 || muzzle.node >= f.pose.weapon_node_count[mount]) {
        return false;
    }
    const FxMat4& node = f.pose.weapon_nodes[mount][muzzle.node];
    // the point under its node, then turned pi about y (weapon_muzzle_matrix). The pose holds the
    // original's matrices mirrored, whose z row is the original's z negated in the port frame;
    // the turn negates the original's z too, so the shot's direction (a muzzle's z here, as
    // shots take it) is the node's z row as it stands, and its y the node's y
    *out = mat4_from(mat3_from_dir_up(node.z, node.y), transform_coord(muzzle.point, node));
    return true;
}

// decomp: weapon_muzzle_matrix 0x98B37
bool weapon_muzzle_at(const World& world, const Fighter& f, const Weapon& w, FxMat4* out) {
    FxMat4 m;
    if (!fighter_muzzle(world, f, w.mount, w.muzzle, &m)) {
        return false;
    }
    // a back weapon with lock rule 0 or 1 re-aims at this step's reticle point, up from the
    // muzzle's y; the arms leave along the posed barrel
    const WeaponRecord& r = *w.record;
    if (w.mount >= 2 && (r.lock_rule == 0 || r.lock_rule == 1)) {
        const FxVec3 aim = r.lock_rule == 1 ? f.combat.reticle_track_world : f.reticle_center_world;
        const FxVec3 to_aim = aim - m.w;
        if (to_aim != FxVec3{}) {
            m = mat4_from(mat3_from_dir_up(to_aim, m.y), m.w);
        }
    }
    // the stage between the chest and the muzzle stops the shot (melee excepted)
    const FxVec3 chest = f.position + FxVec3{Fx{}, f.radius, Fx{}};
    FxVec3 point;
    if (r.behavior != 6 && collision_segment_hit(world.battle->stage, chest, m.w, &point, false)) {
        return false;
    }
    *out = m;
    return true;
}

// decomp: weapon_muzzle_matrix 0x98B37
bool weapon_muzzle_matrix(World& world, Fighter& f, Weapon& w, FxMat4* out, bool flash,
                          bool step_barrel) {
    // weapon_muzzle_count: no barrels, no shot; the barrel steps before it is read, blocked or not
    const std::vector<RigMuzzle>* muzzles = mount_muzzles(world, f, w.mount);
    const int barrels = muzzles ? static_cast<int>(muzzles->size()) : 0;
    if (barrels <= 0) {
        return false;
    }
    if (step_barrel) {
        w.muzzle = (w.muzzle + 1) % barrels;
    }
    FxMat4 m;
    if (!weapon_muzzle_at(world, f, w, &m)) {
        return false;
    }
    if (flash) {
        combat_event(world, event_muzzle, f.slot, w.mount, m.w, w.muzzle, m.z);
    }
    *out = m;
    return true;
}
