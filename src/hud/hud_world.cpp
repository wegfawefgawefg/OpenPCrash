// actor_sounds_update's lock alarm (decomp src/od/u_0933f0.c): a human with durability
// left counts lock_warning_timer down once an update; when it runs out it starts the alarm (bank 3
// cue 0x2A) if someone holds a lock mark on it and the alarm isn't playing, else stops a playing
// one. The read of locking_me is the HUD's of the last update, as retail's fighter task reads it.
#include "hud/hud_world.hpp"

#include "weapons/weapons.hpp"
#include "world/world.hpp"

void lock_warning_update(World& world, Fighter& f) {
    if (world.step % 2 != 0 || f.controller == -1) {
        return;
    }
    LockWarning& alarm = world.lock_warnings[static_cast<std::size_t>(f.slot)];
    if (f.combat.durability <= 0) {
        // actor_cloak_effects_release stops it with the fighter's other loops
        if (alarm.sounding) {
            alarm.sounding = false;
            combat_event(world, event_lock_warning, f.slot, 0, f.position);
        }
        return;
    }
    alarm.timer -= 1;
    if (alarm.timer >= 0) {
        return;
    }
    const FighterHud& hud = world.huds[static_cast<std::size_t>(f.slot)];
    bool started = false;
    for (int i = 0; i < fighter_slots; ++i) {
        if (hud.locking_me[static_cast<std::size_t>(i)] && !alarm.sounding) {
            alarm.sounding = true;
            alarm.timer = lock_warning_updates;
            combat_event(world, event_lock_warning, f.slot, 1, f.position);
            started = true;
            break;
        }
    }
    // the check stops a playing alarm even while the lock is still on (it restarts next update)
    if (!started && alarm.sounding) {
        alarm.sounding = false;
        combat_event(world, event_lock_warning, f.slot, 0, f.position);
    }
}
