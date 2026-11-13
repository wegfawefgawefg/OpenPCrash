#pragma once
// The world's hash (docs/multiplayer.md "Desyncs", item 1): field by field, never raw bytes, so
// padding and pointers don't count (the BattleData, the stage's and the story's tables are left
// out: the gameplay data hash covers them). world_hash covers every field of the World;
// world_fight_hash leaves out what only a live tournament fight writes (battle phase 0): the
// story, the garages, the profile, the rumble contexts, the session button configs, the battle
// phase and the battle's result. The replay checks the fight hash against the live fight step by
// step (src/battle_replay.cpp); online play compares world_hash between machines, and --hash N
// prints it for the crossplay check (docs/numbers.md "Determinism").
#include <cstdint>
#include <string>

struct World;

std::uint64_t world_hash(const World& world);
std::uint64_t world_fight_hash(const World& world);
// "hash STEP WORLD FIGHT" (the two hashes) when every > 0 and the step is a multiple of it
// (--hash N)
void world_hash_print(const World& world, int every);
// the named parts of the world (fighters[2].combat, stage.craft, story...) whose hashes differ
// between two worlds, space separated: where a desync or a replay first parts
std::string world_hash_diff(const World& a, const World& b);
// "part STEP NAME HEX" for each of those parts, to compare two machines' runs line by line
void world_hash_parts_print(const World& world);
