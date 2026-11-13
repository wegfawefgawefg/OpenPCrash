#pragma once
// What the AI reads and never changes, loaded once per battle into BattleData: retail's AI tables
// (data/tables/ai.sexp from src/data/roster.c, core pack), the weapon records' behaviour and lock
// rule, the opponents and pilots the debug roster draws from, and the stage's route graph (an
// ENPT file, data/nav, in the stage's pack; docs/port/ai.md "Data" and "Routes").
#include "fx/vec.hpp"

#include <array>
#include <string>
#include <vector>

inline constexpr int ai_prob_rows = 42;
inline constexpr int ai_profiles = 12;

struct AiTables {
    // ai_transition_probs_normal / _alternate (battle_phase 1): 5 chances of 255
    std::array<std::array<int, 5>, ai_prob_rows> probs_normal{}, probs_alternate{};
    // ai_selection_profiles: [profile][why][durability band] -> a probs row
    std::array<std::array<std::array<int, 5>, 5>, ai_profiles> selection{};
    // ai_stat_bands: by level / 20, {button 1, jump, dodge} chances of 255
    std::array<std::array<int, 3>, 20> stat_bands{};
    // ai_transition_result_map: the state each column leads to
    std::array<int, 5> result_map{};
    // ai_level_timing_profiles by level + 128, ticks; row 256 stands for what state 6
    // reads past the last row (every row's column 6 is the next row's column 0)
    std::array<std::array<int, 6>, 257> timing{};
    // ai_search_move_weights, up to its -1
    std::vector<int> search_weights;
};

// the two WeaponRecord bytes the AI reads: +0x1C behaviour (0..4 fire, 5 laser, 6 melee), +0x1E
// the lock rule
struct AiWeaponRecord {
    int behavior = 0;
    int lock_rule = 0;
};

// OpponentEntry (rumble_opponent_entries) and PilotRecord.team
struct OpponentRow {
    int pilot = 0, loadout = 0, profile = 4, level = 0;
};

// An ENPT node: its id byte (+1, what AiRoute.node keeps), kind (+2), links as node indices and
// its spline: points[0] the start, then control, control, end for each segment, so segment i is
// points[3i .. 3i + 3] (a record of 0x24 bytes and the next record's first point)
struct NavNode {
    int id = 0;
    int kind = 0;
    std::vector<int> links;
    int segments = 0;
    std::vector<FxVec3> points;
};
struct NavGraph {
    std::vector<NavNode> nodes;
};

struct AiData {
    AiTables tables;
    std::vector<AiWeaponRecord> arms, backs;
    std::vector<OpponentRow> opponents;
    std::vector<int> pilot_teams;
    NavGraph nav;
};

struct Pack;
struct PackLibrary;
// the tables from the core pack, the graph from the stage selector's pack
bool load_ai_data(AiData& data, const Pack& core, PackLibrary& packs, int stage,
                  std::string& error);
// the stage's route graph alone (the job map's walkers, ai_nav_resource_select)
bool load_nav_graph(NavGraph& nav, PackLibrary& packs, int stage, std::string& error);
