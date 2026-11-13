#include "view/combat_sounds.hpp"

#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "mech/fighter_rates.hpp"
#include "weapons/weapons.hpp"
#include "world/world.hpp"

#include <initializer_list>

namespace {

// weapon_laser_controller's sound_timer: frame_cadence x 9 updates, nine seconds (60 a second
// in the port's steps)
constexpr int beam_restart_steps = 60 * 9;

struct Cue {
    int bank = -1, cue = -1;
};

void to_floats(FxVec3 v, float out[3]) {
    out[0] = fx_float(v.x);
    out[1] = fx_float(v.y);
    out[2] = fx_float(v.z);
}

bool human(const Fighter& f) {
    return f.controller != -1;
}

// sound_play_at (a human's) and sound_start_3d (an AI's) alike: flag 2, at the point
int play_at(Audio& audio, Cue c, FxVec3 at, unsigned flags = 0) {
    if (c.bank < 0) {
        return -1;
    }
    float p[3];
    to_floats(at, p);
    return play_sound(audio, c.bank, c.cue, sound_positioned | flags, p);
}

bool any_of(int value, std::initializer_list<int> list) {
    for (int v : list) {
        if (v == value) {
            return true;
        }
    }
    return false;
}

// the fire sound by the weapon's geometry and mount
// decomp: weapon_flash_model_39e 0x99CA6
Cue flash_39e_cue(const Weapon& w, bool player) {
    const int g = w.record->geometry;
    if (w.mount == 2 || w.mount == 3) {
        if (any_of(g, {1, 0xB, 0x15, 0x1F})) {
            return player ? Cue{bank_battle_player, se_back_cannon_fire}
                          : Cue{bank_battle, se_back_cannon_fire_ai};
        }
        if (any_of(g, {6, 0x10, 0x1A, 0x22})) {
            return player ? Cue{bank_battle_player, se_back_heavy_cannon_fire}
                          : Cue{bank_battle, se_back_heavy_cannon_fire_ai};
        }
        return player ? Cue{bank_battle_player, se_gun_fire} : Cue{bank_battle_old, se_gun_fire_ai};
    }
    if (any_of(g, {8, 0x1D})) {
        return player ? Cue{bank_battle_player, se_gatling_fire}
                      : Cue{bank_battle, se_gatling_fire_ai};
    }
    if (g == 0x20) {
        return player ? Cue{bank_battle_player, se_railgun_fire}
                      : Cue{bank_battle, se_railgun_fire_ai};
    }
    return player ? Cue{bank_battle_player, se_gun_fire} : Cue{bank_battle_old, se_gun_fire_ai};
}

// weapon_muzzle_effects' callback (WeaponRecord.muzzle_fx) and its sound; the casing after a
// pull of a gun (weapon_fire_controller's weapon_eject_casing, a human's only)
void fire(CombatSounds& s, Audio& audio, const World& world, const CombatEvent& e) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(e.slot)];
    const Weapon& w = f.combat.weapons[e.value];
    if (!w.record) {
        return;
    }
    const bool player = human(f);
    int& voice = s.weapons[e.slot][e.value].fire_voice;
    switch (w.record->muzzle_fx) {
    case 0:
        voice = play_at(audio, flash_39e_cue(w, player), e.position);
        break;
    case 1: // weapon_flash_model_39f: a buffer still playing is left alone
        voice = play_at(audio,
                        player ? Cue{bank_battle_player, se_gun_fire}
                               : Cue{bank_battle_old, se_gun_fire_ai},
                        e.position, sound_once);
        break;
    case 2: // weapon_flash_model_3f3: the grenades' 3/0x18, the rockets' 3/0x30
        if (w.record->behavior == 3) {
            voice = play_at(audio,
                            player ? Cue{bank_battle_player, se_grenade_launch}
                                   : Cue{bank_battle, se_grenade_launch_ai},
                            e.position);
        } else {
            voice = play_at(audio,
                            player ? Cue{bank_battle_player, se_missile_launch}
                                   : Cue{bank_battle_old, se_missile_launch_ai},
                            e.position);
        }
        break;
    case 3: // weapon_fire_sound_18 (no record has it)
        voice = play_at(audio,
                        player ? Cue{bank_battle_player, se_grenade_launch}
                               : Cue{bank_battle, se_grenade_launch_ai},
                        e.position);
        break;
    case 4: // weapon_fire_sound_17
        voice = play_at(audio,
                        player ? Cue{bank_battle_player, se_red_lotus_fire}
                               : Cue{bank_battle, se_red_lotus_fire_ai},
                        e.position);
        break;
    default: // 5 the laser's (its beam sound below), 6 none
        break;
    }
    if (player && w.record->behavior <= 3) {
        // rand_float_crt(1.0) < 0.5: the CRT's rand over 32768
        s.crt = s.crt * 0x343FDu + 0x269EC3u;
        const bool first = static_cast<float>((s.crt >> 16) & 0x7FFF) / 32768.0f < 0.5f;
        play_at(audio, Cue{3, first ? 0x2D : 0x2E}, e.position, sound_once);
    }
}

// weapon_trigger_ready: the trigger held on a fitted, loaded weapon of the living
bool trigger_ready(const Fighter& f, const Weapon& w) {
    return f.combat.triggers[w.mount].fire != 0 && w.fitted && w.resource >= fx(1) &&
           f.combat.alive;
}

// weapon_laser_controller's sound: the beam's cue from the muzzle while it fires, again
// every nine seconds, stopped when the trigger lets go (unless another beam of the fighter's
// still burns); laser_sound_stop stops every voice of the cue
// decomp: weapon_laser_controller 0x9B97E
void beam(WeaponSound& ws, Audio& audio, const Fighter& f, const Weapon& w, bool fired,
          FxVec3 muzzle) {
    if (fired) {
        if (!ws.beam) {
            const Cue c = human(f) ? Cue{bank_battle_player, se_laser_beam}
                                   : Cue{bank_battle_old, se_laser_beam_ai};
            ws.beam_voice = play_at(audio, c, muzzle);
            ws.beam = true;
            ws.beam_bank = c.bank;
            ws.beam_cue = c.cue;
            ws.beam_timer = beam_restart_steps;
        } else if (--ws.beam_timer < 0) {
            stop_sound(audio, ws.beam_bank, ws.beam_cue);
            ws.beam = false;
        } else {
            float p[3];
            to_floats(muzzle, p);
            set_sound_position(audio, ws.beam_bank, ws.beam_cue, ws.beam_voice, p);
        }
        return;
    }
    if (!ws.beam || trigger_ready(f, w)) {
        return; // held but the muzzle blocked: as it was
    }
    bool other = false;
    for (const Weapon& o : f.combat.weapons) {
        other = other ||
                (o.record && o.record->behavior == 5 && trigger_ready(f, o) && o.reload_left > 0);
    }
    if (!other) {
        stop_sound(audio, ws.beam_bank, ws.beam_cue);
    }
    ws.beam = false;
}

// weapon_melee_controller's sound: the swing's whoosh when its phase reaches 15 (the
// update before: [15 - ticks, 15), one step's tick here), by the blade's geometry; a human's
// 3/0x1F cut when the swing ends
// decomp: weapon_melee_controller 0x9C2B7
void blade(Audio& audio, const Fighter& f, const Weapon& w, const Weapon& was, const World& world) {
    const bool player = human(f);
    if (w.beam_shot < 0) {
        if (was.beam_shot >= 0 && player) {
            stop_sound(audio, bank_battle_player, se_drill_spin);
        }
        return;
    }
    const Shot& shot = world.combat.shots[static_cast<std::size_t>(w.beam_shot)];
    if (shot.state != 1 || f.motion_state != state_melee || f.motion_phase < fx(14) ||
        f.motion_phase >= fx(15)) {
        return;
    }
    const int g = w.record->geometry;
    Cue c;
    if (any_of(g, {5, 0xF, 0x17})) {
        c = player ? Cue{bank_battle_player, se_drill_spin} : Cue{};
    } else if (g == 6) {
        c = player ? Cue{bank_battle_player, se_pile_bunker} : Cue{bank_battle, se_pile_bunker_ai};
    } else if (any_of(g, {7, 0x10, 0x19})) {
        c = player ? Cue{bank_battle_player, se_blade_swing} : Cue{bank_battle, se_blade_swing_ai};
    } else {
        c = player ? Cue{bank_battle_player, se_punch_swing} : Cue{bank_battle, se_punch_swing_ai};
    }
    play_at(audio, c, f.position);
}

// battle_shots_update's sounds: a pass that met something (event_shot_hit) by the weapon's
// delivery: the cannons' 1/0xE, else 1/0x2E (flag 4; none for a beam), a rocket's 1/0xF
void shot_hit(Audio& audio, const World& world, const CombatEvent& e) {
    const Weapon& w = weapon_at(world, weapon_ref(e.slot, e.value));
    if (!w.record) {
        return;
    }
    if (w.record->delivery == 0) {
        if (any_of(w.record->geometry, {6, 0x10, 0x1A, 0x22})) {
            play_at(audio, Cue{bank_battle, se_heavy_cannon_hit}, e.position);
        } else if (w.record->behavior != 5) {
            play_at(audio, Cue{bank_battle, se_bullet_hit}, e.position, sound_once);
        }
    } else if (w.record->delivery == 2) {
        play_at(audio, Cue{bank_battle, se_missile_explosion}, e.position);
    }
}

// an AI rocket's fire sound (bank 2 cue 2) moved with its shot while it flies
void rockets(CombatSounds& s, Audio& audio, const World& world) {
    for (int i = 0; i < shot_pool_size; ++i) {
        const Shot& now = world.combat.shots[static_cast<std::size_t>(i)];
        if (now.state != 1 || now.weapon < 0 || now.mover == mover_none) {
            continue;
        }
        const Weapon& w = weapon_at(world, now.weapon);
        const int slot = ref_slot(now.weapon);
        const int mount = now.weapon % 5;
        if (!w.record || w.record->delivery != 2 || mount >= 4 ||
            human(world.fighters[static_cast<std::size_t>(slot)])) {
            continue;
        }
        const int voice = s.weapons[slot][mount].fire_voice;
        if (voice >= 0) {
            float p[3];
            to_floats(now.pos, p);
            set_sound_position(audio, bank_battle_old, se_missile_launch_ai, voice, p);
        }
    }
}

} // namespace

void combat_sounds_step(CombatSounds& sounds, Audio& audio, const World& before,
                        const World& world) {
    if (!world.battle) {
        return;
    }
    // the step's events in their order: fire, the melee hit, the wreck, the shots' and beams' hits
    bool fired[fighter_slots][4] = {};
    FxVec3 muzzle[fighter_slots][4];
    for (int i = 0; i < world.combat.event_count; ++i) {
        const CombatEvent& e = world.combat.events[static_cast<std::size_t>(i)];
        if (e.slot < 0 || e.slot >= fighter_slots) {
            continue;
        }
        const Fighter& f = world.fighters[static_cast<std::size_t>(e.slot)];
        if (e.kind == event_muzzle && e.value >= 0 && e.value < 4) {
            fired[e.slot][e.value] = true;
            muzzle[e.slot][e.value] = e.position;
            fire(sounds, audio, world, e);
        } else if (e.kind == event_impact && e.value == 4) {
            // impact_profile_dispatch case 4, at the shot
            play_at(audio,
                    human(f) ? Cue{bank_battle_player, se_melee_hit}
                             : Cue{bank_battle, se_melee_hit_ai},
                    e.position);
        } else if (e.kind == event_destroyed) {
            // actor_settle_damage at durability 0: the blast at the wreck
            play_at(audio, Cue{bank_battle, se_explosion_large}, e.position);
        } else if (e.kind == event_shot_hit) {
            shot_hit(audio, world, e);
        } else if (e.kind == event_beam_hit) {
            // shot_hit_fighters and impact_volume_scan: the beam's hit, at it
            play_at(audio, Cue{bank_battle, se_beam_hit}, e.position);
        }
    }
    // each fighter's laser beam and blade
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        const Fighter& was = before.fighters[static_cast<std::size_t>(slot)];
        for (int mount = 0; mount < 4; ++mount) {
            const Weapon& w = f.combat.weapons[mount];
            if (!w.record) {
                continue;
            }
            if (w.record->behavior == 5) {
                beam(sounds.weapons[slot][mount], audio, f, w, fired[slot][mount],
                     muzzle[slot][mount]);
            } else if (w.record->behavior == 6) {
                blade(audio, f, w, was.combat.weapons[mount], world);
            }
        }
    }
    rockets(sounds, audio, world);
}

void combat_sounds_stop(CombatSounds& sounds, Audio& audio) {
    for (auto& mounts : sounds.weapons) {
        for (WeaponSound& ws : mounts) {
            if (ws.beam) {
                stop_sound(audio, ws.beam_bank, ws.beam_cue);
            }
        }
    }
    sounds = CombatSounds{};
}
