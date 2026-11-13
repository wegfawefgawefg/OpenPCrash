#pragma once
// Who enters a battle and where (decomp docs/port/battle.md "Who comes in where";
// src/hand/loadout_assets.c fighter_spawn, battle_actor_position_resolve,
// scene_gate_entry_start): a slot's roster entry (world.rumble[battle_state.rumble]) made a
// fighter at its gate, its map start or a random gate, with its AI and its entrance scene.
#include "battle/part_records.hpp"
#include "fx/vec.hpp"
#include "world/game_state.hpp"

struct World;
struct RosterBuild;

// a roster entry's Scoobee as a part record row
BuildRecord build_record_of(const RosterBuild& build);
// the slot's build taken from its roster entry (BattleSlot.build) before it spawns; the
// presentation loads its mech and fills the rest-pose measures
void battle_slot_prepare(World& world, int slot);
// actor_task_update: fighter_spawn (the place, actor_init, the AI, the scene) and
// fighter_weapons_spawn. The gameplay RNG draws in retail's order: the gate roll, then ai_enter's
void battle_spawn(World& world, int slot);
// battle_actor_position_resolve: the place and heading, and the gate (-1 none)
int battle_position_resolve(World& world, int slot, FxVec3& position, Angle& heading);
// camera_view_save, from battle_destroy: a fight in battle phase 0 outside VS whose roster
// entry 0 is player 1 keeps slot 0's camera view (GameOptions::camera_view) for the next fight
void battle_camera_view_save(World& world);
