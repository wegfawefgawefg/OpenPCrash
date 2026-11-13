#pragma once
// The fight a bug report carries (docs/release.md, "Bug reports"): the world is a pure function of
// its inputs, so the fight's first world and every step's Inputs reproduce it exactly, a crash's
// step included. The record is the replay's own (battle_replay.hpp: the start world, the inputs,
// each step's fight hash), kept for every live fight; this file only reads it, never the world.
//
// The record as bytes: a head made once when the fight starts (the world as the first step found
// it, its constant tables' pointers left out and written as what rebuilds them: the battle's plan,
// the presentation's measured lines, each weapon's table row), then a tail written when a report
// is made: the steps' inputs so far (and the one being stepped, for a crash inside it), every
// 60th step's fight hash and, when it is known, the live world's hash at the end. The crash
// handler writes the same bytes without allocating (report_fight_write). The worker packs them
// with LZ4 (the inputs delta coded and turned into byte columns first), and pc_sim --replay FILE
// steps the fight again from them (src/sim_main.cpp).
#include "battle/part_records.hpp"
#include "world/inputs.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct World;
struct BattleReplay;
struct BattleWorldPlan;
struct PackLibrary;
struct StageData;
struct StoryTables;
struct RosterTables;

// the fight starts: its head made from the record's first world (battle_run_start, after
// replay_record_start); a fight not recorded clears it
void report_fight_begin(const BattleReplay& replay, const BattleWorldPlan& plan);
void report_fight_clear();
// before each live step: the input about to be stepped (a crash inside the step has it too)
void report_fight_step(const Inputs& inputs);
// the record as bytes now ("" when no fight was recorded), with the live world's hash at the end
std::string report_fight_bytes(const World* live);
// the same, from the crash handler: no allocation, no locks; put(data, size) writes
using FightPut = void (*)(void* to, const void* data, std::size_t size);
void report_fight_write(void* to, FightPut put);

// LZ4 over the record (the inputs delta coded and in byte columns first), and back
std::string report_fight_pack(const std::string& raw);
bool report_fight_unpack(const std::string& packed, std::string& raw, std::string& error);

// a record read back (pc_sim)
struct FightReplay {
    std::vector<int> rigs;
    int player_rig = 0;
    std::vector<BuildRecord> extra;
    int ranker_line_ticks[3] = {};
    std::vector<int> scene_text_scripts;
    std::vector<std::pair<int, int>> weapon_rows; // per fighter and weapon: table (0 arms, 1
                                                  // backs, -1 none) and row
    std::uint64_t start_hash = 0, start_fight_hash = 0;
    std::unique_ptr<World> start;
    std::vector<Inputs> inputs;
    std::vector<std::pair<int, std::uint64_t>> fight_hashes; // step index, the fight hash after it
    std::int64_t final_step = -1;
    std::uint64_t final_hash = 0;
};

bool report_fight_read(const std::string& raw, FightReplay& out, std::string& error);
// the start world's constant tables put back: the battle data from the plan (and the measured
// lines), the stage's, the story's and the roster's tables, each weapon's record
bool report_fight_attach(World& world, const FightReplay& replay, PackLibrary& packs,
                         const StageData& stage, const StoryTables* story,
                         const RosterTables* roster, std::string& error);
