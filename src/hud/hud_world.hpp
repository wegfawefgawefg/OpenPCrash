#pragma once
// The HUD state the world keeps besides the senses (battle/fighter_hud.hpp) and the tracking point
// and locks (weapons/combat.hpp LockHud): the lock alarm's timer (FighterSounds.lock_warning_timer,
// actor_sounds_update). It starts and stops the alarm through combat events, which the HUD
// plays (src/hud).

struct World;
struct Fighter;

// frame_cadence x 0.3: 9 updates between checks
inline constexpr int lock_warning_updates = 9;

struct LockWarning {
    int timer = lock_warning_updates; // updates to the next check
    bool sounding = false;            // the alarm cue is playing (lock_warning.cue != -1)
};

// a human's alarm, from its fighter task: on even steps with retail's update counts (R11), so it
// stops and restarts every 11 updates while a lock stays on, as retail (docs/bugs.md)
void lock_warning_update(World& world, Fighter& f);
