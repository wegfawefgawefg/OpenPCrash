#pragma once
// The front end's fight as battle_task_begin builds it in the world, in its order (decomp
// docs/port/battle.md "The first update"): for a demo battle the context refilled and six
// opponents drawn, every build that may fight, the battle's data, the battle state, each present
// roster entry spawned in slot order (the human at its gate, the opponents at theirs, with their
// entrance scenes and AIs), then the stage with the tournament's crate weights. The game's battle
// session (battle_run.cpp) and pc_sim (sim_main.cpp) both build their fight with it, so the two
// step the same world. No drawing here: the session loads each fighter's look through the hook.
#include "battle/part_records.hpp"
#include "world/app_states.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct World;
struct BattleData;
struct PackLibrary;
struct RosterTables;
struct StoryTables;
struct StageData;

// runs for each slot from 1 just before that fighter spawns, with the build it is drawn as
using SpawnHook = std::function<bool(int slot, int build, std::string& error)>;

struct BattleWorldSetup {
    int menu_state = app_battle; // the AppState fighting it (FrontEnd::state)
    int phase = 0;            // Campaign::battle_phase: 0, free play's 4, the ending's 3
    bool demo = false;        // the result screen's or the attract demo: phase 1, AIs alone
    int demo_tournament = -1; // the attract demo's tournament; -1 today's
    int rental_build = -1;    // Campaign::rental_build
};

// the builds the fight needs, settled before anything loads
struct BattleWorldPlan {
    std::vector<int> rigs;          // every build that may fight today, sorted
    int player_rig = 0;             // the row the player's entry is posed and drawn as
    std::vector<BuildRecord> extra; // the garage's machine, after the table's rows
};

// a demo's roster drawn (result_screen_setup, demo_battle_enter), then the plan
BattleWorldPlan battle_world_plan(World& world, const BattleWorldSetup& setup,
                                  const RosterTables& roster, const StoryTables& story);
// the battle's data, into World::battle; null with why on failure (the caller may still write
// the parts measured from presentation: BattleData::ranker_line_ticks)
std::shared_ptr<BattleData> battle_world_data(World& world, const BattleWorldPlan& plan,
                                              PackLibrary& packs, std::string& error);
// the battle state, every spawn and the stage
bool battle_world_spawn(World& world, const BattleWorldSetup& setup, const RosterTables& roster,
                        const StageData& stage, const SpawnHook& look, std::string& error);
