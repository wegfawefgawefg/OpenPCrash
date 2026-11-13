#pragma once
// A fighter's own sounds (decomp src/od/u_0933f0.c, docs/port/audio.md "Who uses which bank"):
// actor_sounds_update with the legs by leg_sound_table (legs_sound_two,
// legs_sound_many, legs_sound_roller, legs_sound_hover) run on a footstep, fighter_leg_sound's
// dodges, actor_landing_sound, actor_contact_sound, the hum, the broken
// arm's crackle, fighter_turn_sound, the score and the low armour; and the motion code's
// own: the jump (actor_try_jump, sv_motion_state_4), the dive (actor_air_control)
// and the fall (actor_phase_fall_update). Retail plays them in each fighter's
// update; the port hears them after each step from the world before and after it, as the weapons'
// (combat_sounds.cpp). Presentation: it reads the world and never writes it. The camouflage's and
// the lock alarm's are the world's events (fighter_camo.cpp, lock_warning_update).
#include "world/fighter_slots.hpp"

#include <cstdint>

struct Audio;
struct Effects;
struct World;

// FighterSounds: one fighter's voices and clocks
struct FighterSoundSlot {
    bool seen = false;
    int hum = 0, crackle = 0, low_armour = 0; // steps to the next (actor_sounds_init's)
    int low_armour_voice = -1;
    bool low_armour_on = false;
    bool turning = false;
    // the legs' loops: cue_c (walking) and cue_14 (running) with their voices, the dodge's voice
    int walk_bank = -1, walk_cue = -1, walk_voice = -1;
    int run_bank = -1, run_cue = -1, run_voice = -1;
    int leg_voice = -1;
    int pitch = -500, pitch_goal = -500; // FighterSounds f_78, f_7a
};

struct FighterSounds {
    FighterSoundSlot slots[fighter_slots];
    std::uint32_t crt = 1; // rand_float_crt's draws for the two-legged step, off the world
};

// after each world step; effects for the arms' break state (Effects' weapon_state), null none
void fighter_sounds_step(FighterSounds& sounds, Audio& audio, const World& before,
                         const World& world, const Effects* effects);
// the battle's end: the loops stopped, everything forgotten
void fighter_sounds_stop(FighterSounds& sounds, Audio& audio);
