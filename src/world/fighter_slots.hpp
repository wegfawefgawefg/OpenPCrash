#pragma once
// The fighter slots: the battle task's six children (battle_task_begin). Every per-slot
// array in the world is this long. A build may set PHANTOMCRASH_FIGHTER_SLOTS (pc_sim's
// 12-fighter measurement, docs/multiplayer.md "Measured"); the game is retail's 6, which the save
// format's rosters also are.
#ifndef PHANTOMCRASH_FIGHTER_SLOTS
#define PHANTOMCRASH_FIGHTER_SLOTS 6
#endif

inline constexpr int fighter_slots = PHANTOMCRASH_FIGHTER_SLOTS;
static_assert(fighter_slots >= 6 && fighter_slots <= 16, "Shot::dodge_rolled holds 16 slots");
