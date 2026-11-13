#pragma once
// The fight's weapon sounds (decomp docs/port/audio.md "Who uses which bank"): the fire sound of
// each muzzle callback (weapon_muzzle_effects's weapon_flash_model_39e/39f/3f3,
// weapon_fire_sound_17/18), the casings (weapon_eject_casing), the laser's beam
// (weapon_laser_controller), the melee swing (weapon_melee_controller), a shot's end and a
// rocket's flight (battle_shots_update), the melee hit (impact_profile_dispatch case 4)
// and the wreck (actor_settle_damage). Retail plays them from inside its weapon and shot updates;
// the port hears them after each step from world.combat.events and the shot pool, the world
// before the step against the world after it. Presentation: it reads the world and never
// writes it. Bank 3 for a fighter with a controller (a human), bank 1 or 2 for an AI.
#include "world/fighter_slots.hpp"

#include <cstdint>

struct Audio;
struct World;

// a weapon's sound as its Weapon keeps it (sound_cue, sound_id, sound_timer)
struct WeaponSound {
    int fire_voice = -1; // the voice its last fire sound took (a rocket's moves with its shot)
    bool beam = false;   // the laser's beam sound playing (beam_bank, beam_cue)
    int beam_bank = -1, beam_cue = -1, beam_voice = -1;
    int beam_timer = 0; // steps to its restart
};

struct CombatSounds {
    WeaponSound weapons[fighter_slots][4];
    std::uint32_t crt = 1; // rand_float_crt's coin for the casings, off the world
};

// after each world step: the step's weapon sounds
void combat_sounds_step(CombatSounds& sounds, Audio& audio, const World& before,
                        const World& world);
// the battle's end: the beams' sounds stopped, everything forgotten
void combat_sounds_stop(CombatSounds& sounds, Audio& audio);
