#pragma once
// The part records a fighter's numbers come from (decomp include/tables.h, docs/port/parts.md),
// read from data/tables/*.sexp (the tables family, docs/tables.md) into Q16.16 at load, and the
// builds (rumble_scoobee_loadouts). Only the fields the world uses so far: weights and the leg's
// movement stats. Prices and the other large curves stay out (they pass 32768).
#include "fx/fx.hpp"

#include <string>
#include <vector>

// CurveF/CurveU/CurveS: base, at_mid and at_max (offsets added to base), mid, negative
struct StatCurve {
    Fx base, at_mid, at_max;
    int mid = 0;
    int negative = -1;
};

// Curve::value_at, float_curve_value: the stat at a tune -99..99, clamped >= 0
Fx curve_value(const StatCurve& curve, int tune);

struct BodyRecord {
    int body_class = 0;
    StatCurve weight; // +0x18, hundredths of a ton
};

struct LegRecord {
    int geometry = 0;           // +0x08 the leg class 0..20
    StatCurve speed;            // +0x1C km/h
    StatCurve jump;             // +0x2C units
    StatCurve turn_deg;         // +0x3C degrees per tick
    StatCurve weight;           // +0x4C
    StatCurve capacity;         // +0x5C
    StatCurve landing;          // +0x6C percent
    StatCurve knockback_resist; // +0x7C percent
    StatCurve dodge_ground;     // +0x9C units per tick
    StatCurve dodge_air;        // +0xAC units per tick
    Fx slide_decay;             // +0xBC per update, not tuned
};

// what the world reads of a weapon or chip record so far
struct WeightRecord {
    StatCurve weight;
};

struct OptionRecord {
    Fx weight; // +0x0C, flat
};

// BuildRecord (0x3C): every part a row of its table, -1 none, tunes -99..99
struct BuildRecord {
    int body = -1, body_tune = 0;
    int leg = -1, leg_tune = 0;
    int chip = -1, chip_tune = 0;
    int weapon[4] = {-1, -1, -1, -1}; // arm A, arm B, back A, back B
    int weapon_tune[4] = {};
    int colour = 0, sticker = 205;
    int option[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};

struct PartRecords {
    std::vector<BodyRecord> bodies;
    std::vector<LegRecord> legs;
    std::vector<WeightRecord> arms, backs, chips;
    std::vector<OptionRecord> options;
    std::vector<BuildRecord> builds;
};

struct Pack;
// from the core pack's tables/*.sexp
bool load_part_records(PartRecords& records, const Pack& core, std::string& error);

// chassis_loadout_weight: every fitted part at its tune, options flat
Fx build_weight(const PartRecords& records, const BuildRecord& build);
// roster_base_parts_weight: body and legs at tune 0
Fx build_base_weight(const PartRecords& records, const BuildRecord& build);
