#pragma once
// The stage viewer's fight in the world alone (--build, --opponents, --demo, --ai-level,
// --ai-reach, --intro): the battle's data, the roster, and every fighter spawned through the
// battle entry (src/battle/battle_spawn.hpp), slot 0 first. The game's viewer
// (viewer_battle.cpp) and pc_sim (sim_main.cpp) both build their fight with it, so the two step
// the same world. No drawing here: `before_spawn`, when given, runs for each slot from 1 just
// before that fighter spawns (the viewer loads its look there).
#include <functional>
#include <string>

struct World;
struct Options;
struct PackLibrary;

using SpawnHook = std::function<bool(int slot, int build, std::string& error)>;

bool viewer_world_start(World& world, const Options& options, PackLibrary& packs,
                        const SpawnHook& before_spawn, std::string& error);
// the stage's part, once the fighters are in: battle_stage_init with the world RNG (battle_task_begin
// makes the stage task after the fighters, so their gate and AI draws come first)
struct StageData;
void viewer_stage_start(World& world, const Options& options, const StageData& stage);
