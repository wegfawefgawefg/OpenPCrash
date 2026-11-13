#pragma once
// What the opponent queues, the drawn rosters, the job menu's checks and the battle's repair bill
// read (decomp docs/port/progression.md "Who you fight", battle.md "What the battle hands back",
// parts.md): the opponent entries, pilots and builds (rumble_opponent_entries,
// rumble_pilot_records, rumble_scoobee_loadouts), and of each part table the class, weight and
// price curves. Loaded once from data/tables/ like StoryTables; constant, not world state.
//
// The curves are evaluated in float as retail's Curve::value_at, rumble_curve_value
// and float_curve_value do: what they give (a destruction value, a price, a weight
// compared with a rule's limit) is stored as an integer or only compared, so the float math stays
// out of the step and gives retail's exact numbers.
#include "world/game_state.hpp"

#include <string>
#include <vector>

// a curve row (CurveF / CurveU / CurveS): at_mid and at_max are added to base
struct CurveRow {
    float base = 0, at_mid = 0, at_max = 0;
    int mid = 0, negative = 1;
    bool present = false;
};
// float_curve_value (and Curve::value_at's shape): the value at a tune -99..99
float curve_float(const CurveRow& curve, int tune);
// rumble_curve_value: unsigned fields, the result held to 0..1e8 and truncated
int curve_unsigned(const CurveRow& curve, int tune);

struct OpponentEntry {
    int pilot = 0, loadout = 0, ai_profile = 4, level = 0;
};

// what a part row adds to a build: the class it gives, its weight and price, camouflage
struct PartRow {
    int part_class = -1; // body: body_class; leg: geometry (the leg class); weapon: geometry
    CurveRow weight, price, durability, capacity; // capacity: legs
    int flat_weight = 0, flat_price = 0; // options
    int camo_frames = 0;                 // options
};

struct RosterTables {
    std::vector<OpponentEntry> opponents; // 1187
    std::vector<RosterPilot> pilots;      // 1060
    std::vector<RosterBuild> builds;      // 1190
    std::vector<std::string> build_names; // UTF-8
    std::vector<PartRow> bodies, legs, arms, backs, chips, options;
    std::vector<std::string> identity_names; // UTF-8, "" for nil
    std::vector<int> identity_sprites;
    std::vector<std::string> team_names; // by team + 1 (row 0 is team -1, "None")
    std::vector<std::string> tournament_names, rule_names, rule_subtitles;
};

bool load_roster_tables(RosterTables& tables, const std::string& data_dir, std::string& error);

// rumble_opponent_entry_at, rumble_scoobee_loadout_at, pilot_record_at: out of range is row 0
const OpponentEntry& opponent_entry(const RosterTables& tables, int id);
const RosterBuild& roster_build_row(const RosterTables& tables, int id);
const RosterPilot& pilot_row(const RosterTables& tables, int id);
// sv_body_record etc.: -1 (or less) none, past the end row 0
const PartRow* body_row(const RosterTables& tables, int id);
const PartRow* leg_row(const RosterTables& tables, int id);
const PartRow* weapon_row(const RosterTables& tables, int mount, int id); // arm for 0, 1
const PartRow* option_row(const RosterTables& tables, int id);

// Scoobee::body_class (-1 without a body), leg_class (-1 without legs),
// body_class_or_7, leg_kind_or_8
int build_body_class(const RosterTables& tables, const RosterBuild& build);
int build_leg_class(const RosterTables& tables, const RosterBuild& build);
int build_body_class_or_7(const RosterTables& tables, const RosterBuild& build);
int build_leg_kind_or_8(const RosterTables& tables, const RosterBuild& build);
// leg_kind_group: the leg class's kind 0..7
int leg_kind_group_of(int leg_class);
// chassis_loadout_weight in float: every fitted part at its tune, options flat
float build_weight_float(const RosterTables& tables, const RosterBuild& build);
// the options' camo_frames summed (the No O.C. rule)
int build_camo_frames(const RosterTables& tables, const RosterBuild& build);
// shop_item_price: kinds 0 body, 1 leg, 2 arm, 3 back, 4 chip, 5 option
int part_shop_price(const RosterTables& tables, int kind, int item, int tune);
