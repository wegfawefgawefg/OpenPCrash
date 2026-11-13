#pragma once
// The fighters' scenes' shared pieces: the scene timings and the helpers both halves use. Only
// battle_scenes.cpp (the entrance, the door, the dispatch and the door check) and
// battle_scene_ends.cpp (the death scene and the boss defeat) include it.
#include "battle/battle_flow.hpp"
#include "world/world.hpp"

#include <cstdint>

namespace scenes {

inline constexpr int ticks_ready = 90;    // "READY" from 90 ticks left (sprite 0xE80, menu sound 0)
inline constexpr int ticks_go = 30;       // "GO" from 30 (0xE7F, sound 1)
inline constexpr int ticks_music = 360;   // music 0x10 when the timer crosses frame_cadence * 6 * ticks
inline constexpr int ticks_gate = 480;    // the gate opens one second in
inline constexpr int ticks_walk_end = 180; // below it the entrance stops walking (an AI cuts to the end)
inline constexpr int intro_immune = 300;
inline constexpr int door_ticks = 240;      // scene_duration of the door scene
inline constexpr int door_close_ticks = 30; // gate_close 30 ticks before the door scene's end
inline constexpr int death_watch = 330;     // the death scene's first part
inline constexpr int death_killer_cam = 510;
inline constexpr int death_tail = 60;
inline constexpr int boss_ticks = 180;       // scene_boss_defeat_enter's timer
inline constexpr int first_ranker_ticks = 240;
inline constexpr int door_out_immune = 30;
// a dead AI retires past 241 steps (battle_flow.cpp); the door's exit sets it to 120 updates
inline constexpr int door_retire_steps = 241;
// the door walk: 0.2 an update toward the point, turning at most 0.10471976 rad an update (R8)
inline constexpr Fx door_walk_step = fx_f(0.1);
inline constexpr Angle door_turn_step = angle_from_radians(fx_f(0.05235988));
inline constexpr Angle door_turn_near = angle_from_radians(fx_f(0.1));
// stage_entry_point_find: within 5.97 across and 0..10.5 below an entry point
inline constexpr Fx door_reach = fx_f(5.97);
inline constexpr Fx door_height = fx_f(10.5);
// the clips: 0x1C idle, 0xC the death clip
inline constexpr int clip_idle = 0x1c;
inline constexpr int clip_death = 0xc;

std::int32_t angle_diff(Angle a, Angle b);
RosterEntry& entry_of(World& world, const Fighter& f);
bool crossed(Fx timer, int mark, Fx step);
void freeze(World& world, int slot);
// battle_pause_others and battle_resume_others: every other fighter parked
void pause_others(World& world, const Fighter& f);
void resume_others(World& world, const Fighter& f);

// battle_scene_ends.cpp
bool death_enter(World& world, Fighter& f);
void death_update(World& world, Fighter& f);
void death_exit(World& world, Fighter& f);
void boss_enter(World& world, Fighter& f);
void boss_update(World& world, Fighter& f);
void boss_exit(World& world, Fighter& f);

} // namespace scenes
