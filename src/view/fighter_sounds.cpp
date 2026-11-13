#include "view/fighter_sounds.hpp"

#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "battle/battle_data.hpp"
#include "effects/effects.hpp"
#include "mech/fighter_rates.hpp"
#include "view/footsteps.hpp"
#include "world/world.hpp"

namespace {

// actor_sounds_update's clocks: the hum's tests `timer-- < 0`, so it sounds every T + 2 calls,
// T = frame_cadence x 5 = 150 (152 updates); the broken arm's crackle and the low armour take one
// off first and test `timer < 0`, every T + 1 (crackle T = 150, 151 updates; low armour T = 0x20,
// 33 updates). R4 at a call a step: the hum T' = 2 (T + 2) - 2 = 302 (304 steps apart), the
// others T' = 2 (T + 1) - 1, 301 and 65 (302 and 66 steps)
constexpr int hum_steps = 302;
constexpr int crackle_steps = 301;
constexpr int low_armour_steps = 65;
// the dodge slots (motion_set_animation's clips 0xD..0x10): ground left, right, air left, right
constexpr int slot_dodge_air_right = 15, slot_dodge_air_left = 16;

struct Cue {
    int bank = -1, cue = -1;
};

bool human(const Fighter& f) {
    return f.controller != -1;
}

int play_at(Audio& audio, Cue c, FxVec3 at, unsigned flags = 0) {
    if (c.bank < 0) {
        return -1;
    }
    const float p[3] = {fx_float(at.x), fx_float(at.y), fx_float(at.z)};
    return play_sound(audio, c.bank, c.cue, sound_positioned | flags, p);
}

void move_voice(Audio& audio, int bank, int cue, int voice, FxVec3 at) {
    if (bank < 0 || voice < 0) {
        return;
    }
    const float p[3] = {fx_float(at.x), fx_float(at.y), fx_float(at.z)};
    set_sound_position(audio, bank, cue, voice, p);
}

bool walking(int state) {
    return state == state_walk || state == state_walk_back;
}
bool running(int state) {
    return state == state_run || state == state_run_reverse;
}

// leg_sound_table's rows by leg_kind: the leg update and the plain landing's cue (kinds 7..9
// none). Retail's humans read its bank-1 rows too; the port gives them the bank-3 copy at
// 0x2497D0 (docs/bugs.md)
enum class Legs { none, two, many, roller, hover };
Legs legs_of(int kind) {
    switch (kind) {
    case 0:
        return Legs::two;
    case 1:
    case 2:
        return Legs::many;
    case 3:
    case 4:
    case 5:
    case 6:
        return Legs::roller;
    case 7:
        return Legs::hover;
    default:
        return Legs::none;
    }
}
Cue landing_cue(const Fighter& f) {
    const Legs legs = legs_of(f.leg_kind);
    const int bank = f.controller != -1 ? 3 : 1;
    return legs == Legs::two      ? Cue{bank, 8}
           : legs == Legs::many   ? Cue{bank, 9}
           : legs == Legs::roller ? Cue{bank, 10}
                                  : Cue{};
}

// leg_update's step cue (legs_sound_two: a CRT roll of three, all the same cue; legs_sound_many)
void step_cue(FighterSounds& s, Audio& audio, const Fighter& f, int cue) {
    if (!walking(f.motion_state) && !running(f.motion_state)) {
        return;
    }
    if (cue == 2) {
        s.crt = s.crt * 0x343FDu + 0x269EC3u; // rand_float_crt(3.0): step_cues_* are one cue
    }
    play_at(audio, human(f) ? Cue{3, cue} : Cue{1, cue}, f.position);
}

// legs_sound_roller: walking, a skate's 2 each step or the wheels' 1 looped; running, 0 once
// then 1 looped. The pitch rides at -500 (its glide never moves: docs/bugs.md)
void roller(FighterSoundSlot& s, Audio& audio, const Fighter& f) {
    const int bank = human(f) ? 3 : 1;
    if (walking(f.motion_state)) {
        if (f.leg_kind == 3) {
            play_at(audio, Cue{bank, 2}, f.position);
            s.pitch_goal = -500;
            return;
        }
        if (s.walk_cue == -1) {
            s.walk_voice = play_at(audio, Cue{bank, 1}, f.position, sound_loop);
            s.walk_bank = bank;
            s.walk_cue = 1;
            s.pitch_goal = -500;
        } else {
            move_voice(audio, s.walk_bank, s.walk_cue, s.walk_voice, f.position);
        }
        set_sound_pitch(audio, s.walk_bank, s.walk_cue, s.walk_voice, s.pitch);
    } else if (running(f.motion_state)) {
        if (s.run_cue == -1) {
            s.run_voice = play_at(audio, Cue{bank, 0}, f.position);
            s.run_bank = bank;
            s.run_cue = 0;
        } else if (s.run_cue == 0) {
            s.run_voice = play_at(audio, Cue{bank, 1}, f.position, sound_loop);
            s.run_cue = 1;
            set_sound_pitch(audio, s.run_bank, s.run_cue, s.run_voice, s.pitch);
            s.pitch_goal = 0x7FF;
        } else {
            move_voice(audio, s.run_bank, s.run_cue, s.run_voice, f.position);
            set_sound_pitch(audio, s.run_bank, s.run_cue, s.run_voice, s.pitch);
        }
    } else if (s.run_cue != -1 && s.pitch != s.pitch_goal) {
        set_sound_pitch(audio, s.run_bank, s.run_cue, s.run_voice, s.pitch);
    } else if (s.run_cue != -1) {
        stop_sound(audio, s.run_bank, s.run_cue);
        s.run_cue = -1;
    }
    // the glide: retail adds 0x2BC while over the goal and stays put under it, so from -500 it
    // never moves (kept)
}

// legs_sound_hover: walking or running, 4 once, then 5 looped
void hover(FighterSoundSlot& s, Audio& audio, const Fighter& f) {
    const int bank = human(f) ? 3 : 1;
    if (walking(f.motion_state)) {
        if (s.walk_cue == -1) {
            s.walk_voice = play_at(audio, Cue{bank, 4}, f.position);
            s.walk_bank = bank;
            s.walk_cue = 4;
        } else if (s.walk_cue == 4) {
            s.walk_voice = play_at(audio, Cue{bank, 5}, f.position, sound_loop);
            s.walk_cue = 5;
        } else {
            move_voice(audio, bank, 5, s.walk_voice, f.position);
        }
    } else if (running(f.motion_state)) {
        if (s.run_cue == -1) {
            play_at(audio, Cue{bank, 4}, f.position);
            s.run_bank = bank;
            s.run_cue = 4;
        } else if (s.run_cue == 4) {
            s.run_voice = play_at(audio, Cue{bank, 5}, f.position, sound_loop);
            s.run_cue = 5;
        } else {
            move_voice(audio, s.run_bank, s.run_cue, s.run_voice, f.position);
        }
    }
}

// fighter_leg_sound: a dodge's slide sound when it starts (motion_slot_b, the clip set this
// update), moved with the fighter while its clip plays: the ground's 7, the air's 3/0x13 (an AI
// none)
void dodge(FighterSoundSlot& s, Audio& audio, const Fighter& was, const Fighter& f) {
    const int slot = f.motion_slot_a;
    const bool ground = slot == slot_dodge_right || slot == slot_dodge_left;
    const bool air = slot == slot_dodge_air_right || slot == slot_dodge_air_left;
    if (!ground && !air) {
        return;
    }
    const Cue c = air        ? (human(f) ? Cue{bank_battle_player, se_dodge_air} : Cue{})
                  : human(f) ? Cue{bank_battle_player, se_dodge_ground}
                             : Cue{bank_battle, se_dodge_ground_ai};
    const bool set = slot != was.motion_slot_a || f.motion_state != was.motion_state;
    if (set) {
        s.leg_voice = play_at(audio, c, f.position);
    } else {
        move_voice(audio, c.bank, c.cue, s.leg_voice, f.position);
    }
}

// the motion code's sounds: the jump from a run (actor_try_jump) and at lift-off
// (sv_motion_state_4), a human's dive (actor_air_control) and its fall (actor_phase_fall_update:
// the fall clip set this update)
void motion(Audio& audio, const Fighter& was, const Fighter& f) {
    const Cue jump = human(f) ? Cue{bank_battle_player, se_jump} : Cue{bank_battle, se_jump_ai};
    if (f.motion_state == state_jump && was.motion_state != state_jump &&
        running(was.motion_state)) {
        play_at(audio, jump, f.position);
    }
    if (f.motion_state == state_jump && f.motion_mode == motion_loop &&
        (was.motion_state != state_jump || was.motion_mode == motion_lead_in)) {
        play_at(audio, jump, f.position);
    }
    if (!human(f)) {
        return;
    }
    if (f.dive_used && !was.dive_used) {
        play_at(audio, Cue{bank_battle_player, se_dive}, f.position);
    }
    if (f.motion_state == state_fall && f.motion_slot_a == slot_fall &&
        (was.motion_state != state_fall || was.motion_slot_a != slot_fall)) {
        play_at(audio, Cue{bank_battle_player, se_fall_start}, f.position);
    }
}

// the block actor_sounds_update runs for a fighter with a controller, alive, outside the ending
void cockpit(FighterSoundSlot& s, Audio& audio, const World& world, const Fighter& was,
             const Fighter& f, const Effects* effects) {
    // the hum, 2D
    if (s.hum-- < 0) {
        play_sound(audio, bank_battle, se_cockpit_hum);
        s.hum = hum_steps;
    }
    // a broken arm's crackle (weapon_state 4: its break's arcs done)
    const FighterEffects* fx = effects ? &effects->fighters[f.slot] : nullptr;
    if (fx && (fx->weapon_state[0] == 4 || fx->weapon_state[1] == 4)) {
        if (--s.crackle < 0) {
            play_at(audio, Cue{bank_battle_player, se_weapon_broken_alarm}, f.position);
            s.crackle = crackle_steps;
        }
    }
    // the twist's start and stop (fighter_turn_sound: |stick| over 0.1 within 45 degrees)
    const Fx twist = f.control.control_axis[axis_twist];
    const bool twisting = fx_abs(twist) > fx_f(0.1);
    const bool in_reach = fx_abs(angle_radians(f.aim_yaw)) < fx_f(0.7853982);
    if (!s.turning && twisting && in_reach) {
        play_at(audio, Cue{bank_battle_player, se_turn_start}, f.position);
        s.turning = true;
    } else if (s.turning && (fx_abs(twist) < fx_f(0.1) || !in_reach)) {
        play_at(audio, Cue{bank_battle_player, se_turn_stop}, f.position);
        s.turning = false;
    }
    // the score, 2D, once a voice (sound_start_flag4), in a live fight
    if (world.battle_phase == 0 && was.combat.score != f.combat.score) {
        play_sound(audio, bank_battle, se_money_tick, sound_once);
    }
    // the low armour alarm under a quarter, followed while it plays
    if (f.combat.durability < f.combat.durability_max / 4) {
        if (--s.low_armour < 0) {
            s.low_armour_voice =
                play_at(audio, Cue{bank_battle_player, se_low_armour_alarm}, f.position);
            s.low_armour_on = true;
            s.low_armour = low_armour_steps;
        } else if (s.low_armour_on) {
            move_voice(audio, bank_battle_player, se_low_armour_alarm, s.low_armour_voice,
                       f.position);
        }
    } else if (s.low_armour_on) {
        stop_sound(audio, bank_battle_player, se_low_armour_alarm);
        s.low_armour_on = false;
    }
}

// decomp: actor_sounds_init 0x933F0
void slot_init(FighterSoundSlot& s) {
    s = FighterSoundSlot{};
    s.seen = true;
    s.hum = hum_steps;
    s.crackle = crackle_steps;
    s.low_armour = low_armour_steps;
}

// actor_cloak_effects_release's part here: cue_c and cue_14 (every voice of their cues,
// sound_stop) and the low armour
// decomp: actor_cloak_effects_release 0x93A31
void release(FighterSoundSlot& s, Audio& audio) {
    if (s.walk_cue != -1) {
        stop_sound(audio, s.walk_bank, s.walk_cue);
        s.walk_cue = -1;
    }
    if (s.run_cue != -1) {
        stop_sound(audio, s.run_bank, s.run_cue);
        s.run_cue = -1;
    }
    if (s.low_armour_on) {
        stop_sound(audio, bank_battle_player, se_low_armour_alarm);
        s.low_armour_on = false;
    }
}

void slot_stop(FighterSoundSlot& s, Audio& audio) {
    if (s.walk_cue != -1) {
        stop_sound_voice(audio, s.walk_bank, s.walk_cue, s.walk_voice);
    }
    if (s.run_cue != -1) {
        stop_sound_voice(audio, s.run_bank, s.run_cue, s.run_voice);
    }
    if (s.low_armour_on) {
        stop_sound(audio, bank_battle_player, se_low_armour_alarm);
    }
    s = FighterSoundSlot{};
}

} // namespace

void fighter_sounds_step(FighterSounds& sounds, Audio& audio, const World& before,
                         const World& world, const Effects* effects) {
    if (!world.battle) {
        return;
    }
    // per-update sounds go on the even steps (docs/sixty.md R11): the step just taken
    const bool even = (before.step & 1) == 0;
    for (int slot = 0; slot < fighter_slots; ++slot) {
        FighterSoundSlot& s = sounds.slots[slot];
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        const Fighter& was = before.fighters[static_cast<std::size_t>(slot)];
        if (!f.present || world.battle_state.slots[slot].parked) {
            if (s.seen) {
                slot_stop(s, audio);
            }
            continue;
        }
        // a fighter spawned in the slot (actor_sounds_init)
        if (!s.seen || !was.present || f.age_frames < was.age_frames) {
            slot_stop(s, audio);
            slot_init(s);
        }
        // the legs on a footstep
        if (fighter_footsteps(world, was, f) != 0) {
            switch (legs_of(f.leg_kind)) {
            case Legs::two:
                step_cue(sounds, audio, f, 2);
                break;
            case Legs::many:
                step_cue(sounds, audio, f, 3);
                break;
            case Legs::roller:
                roller(s, audio, f);
                break;
            case Legs::hover:
                hover(s, audio, f);
                break;
            case Legs::none:
                break;
            }
        }
        dodge(s, audio, was, f);
        // actor_landing_sound: bank 3's 0x11 and 0x12 on the two contact kinds, else the legs'
        if (f.landed) {
            if (f.contact_flags & surface_landing_a) {
                play_at(audio, Cue{bank_battle_player, se_landing_surface_a}, f.position);
            } else if (f.contact_flags & surface_landing_b) {
                play_at(audio, Cue{bank_battle_player, se_landing_surface_b}, f.position);
            } else {
                play_at(audio, landing_cue(f), f.position);
            }
        }
        // actor_contact_sound: touching a type 8 surface (contact 0x2000, the stages' edge
        // walls; nothing for a fighter), twice, once an update
        if (even && (f.contact_flags & surface_edge_wall)) {
            play_at(audio, Cue{bank_battle, se_surface_splash}, f.position);
            play_at(audio, Cue{bank_battle, se_surface_splash}, f.position);
        }
        motion(audio, was, f);
        if (human(f) && !world.stage.setup.ending && f.combat.durability > 0) {
            cockpit(s, audio, world, was, f, effects);
        } else if (human(f) && !world.stage.setup.ending) {
            // actor_cloak_effects_release: the wreck's legs' loops and the low armour alarm
            // stopped (the lock alarm's stop is the world's, lock_warning_update)
            release(s, audio);
        }
        // the loops stop with the gait they belong to
        if (s.walk_cue != -1 && !walking(f.motion_state)) {
            stop_sound_voice(audio, s.walk_bank, s.walk_cue, s.walk_voice);
            s.walk_cue = -1;
        }
        if (s.run_cue != -1 && !running(f.motion_state)) {
            stop_sound_voice(audio, s.run_bank, s.run_cue, s.run_voice);
            s.run_cue = -1;
        }
    }
}

void fighter_sounds_stop(FighterSounds& sounds, Audio& audio) {
    for (FighterSoundSlot& s : sounds.slots) {
        slot_stop(s, audio);
    }
    sounds = FighterSounds{};
}
