#pragma once
// The part record fields weapons, damage and the lock-on read (decomp include/tables.h
// WeaponRecord, ChipRecord, OptionRecord; docs/port/parts.md "Field tables"), from the core pack's
// tables/*.sexp into Q16.16 once per battle. The world RNG is world/world_random.hpp.
// The movement fields stay in battle/part_records.hpp.
#include "battle/part_records.hpp"

#include <array>
#include <string>
#include <vector>

// WeaponRecord (0xC8), arm and back alike. The extract names some fields by the archive's
// guesses; the decomp name is beside each
struct WeaponRecord {
    int geometry = -1;
    StatCurve weight;
    int behavior = 0;           // 0 gun, 1 missile, 2 pellets, 3 grenade, 5 laser, 6 melee
    int delivery = 0;           // 0 straight, 1 lob, 2 rocket
    int lock_rule = 0;          // 0 centre, 1 tracking point, 2 lock-on, 4 barrel
    int impact = 0;             // on a hit: 1 effect, 2 blast, 3, 4 melee, 5 laser
    int impact_alt = 0;         // when the life runs out (or the craft or a crate is hit)
    int impact_fx = 0;          // row of impact_fx.sexp
    int muzzle_fx = 6;          // the flash (src/effects): 0, 1 models, 2 flame, 5 laser, 6 none
    int shot_model = 255;       // the shot look table's row, 255 none (gun smoke by it)
    int elevation_deg = 0;      // +0x24
    int reload_frames = 0;      // refire_frames, ticks
    int shot_life = 0;          // ticks
    Fx speed, speed_max, accel; // shot_speed, beam_length, beam_rate: per tick
    int pellet_spread_deg = 0;  // spread_deg
    Fx turn_min_deg;
    StatCurve turn_deg;
    int shots_min = 0;
    StatCurve shots;
    int damage_min = 0;
    StatCurve damage;
    StatCurve blast_radius_end; // +0x64 (f_64)
    int blast_radius_start = 0; // +0x6C (f_6c)
    StatCurve impact_frames, impact_damage;
    int resource_min = 0;
    StatCurve resource, recharge_frames, stagger, knockback, break_chance;
};

// ChipRecord (0x7C): the boxes, the lock time, crits and the shield
struct ChipRecord {
    int reticle_kind_a = 0;   // lock_kind_a
    StatCurve reticle_size_a; // lock_range_a
    int reticle_speed = 0;    // lock_rate
    int reticle_kind_b = 0;   // lock_kind_b
    StatCurve reticle_size_b; // lock_range_b
    int lock_frames = 0;      // lock_time, ticks
    StatCurve camo_distortion; // +0x34 (the extract's lock_hit): / 100 the camouflage shader's k
    StatCurve crit_chance, shield_chance, break_resist;
};

// OptionRecord (0x2C), the battle fields
struct OptionCombat {
    int durability = 0;
    Fx crit_chance, break_resist;
    int laser_reduction = 0;
    int camo_frames = 0, camo_recharge = 0; // +0x22, +0x24 ticks, summed (camo_init)
};

struct CombatTables {
    std::vector<WeaponRecord> arms, backs;
    std::vector<ChipRecord> chips;
    std::vector<OptionCombat> options;
    std::vector<StatCurve> body_durability, leg_durability; // CurveU +0x08, +0x0C
};

struct Pack;
bool load_combat_tables(CombatTables& tables, const Pack& core, std::string& error);

// mount_weapon_record: the arm table for mounts 0 and 1, the back table for 2 and 3; -1
// none, a row out of range reads row 0
const WeaponRecord* mount_weapon_record(const CombatTables& tables, int mount, int row);
// Curve::value_at / rumble_curve_value truncated to an int, worked in 64 bits:
// durability and prices pass Q16.16's 32768
int curve_int(const StatCurve& curve, int tune);
