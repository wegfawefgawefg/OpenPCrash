// Optical camouflage (decomp src/od/u_074d30.c camo_update and friends,
// src/od/u_0933f0.c actor_camo_sound). Retail counts every timer down by `ticks` (2) an
// update, the same update a phase sets it; here by 1 a step (docs/sixty.md "Camouflage", R3).
#include "mech/fighter_camo.hpp"

#include "audio/audio.hpp"
#include "world/app_states.hpp"
#include "world/world.hpp"

#include <cstdint>

namespace {

constexpr int camo_sound_on = 0xC, camo_sound_off = 0x10, camo_sound_break = 0xD; // bank 3

// state 4 ends once `left - 60 <= 0`. Retail tests it every second tick with left already 2 down
// in the press update; with left 1 down a step, R12 A on the hidden window gives the same hidden
// ticks as retail with 61 for an even total and 60 for an odd one (docs/sixty.md "Camouflage")
Fx hidden_end_left(const Camo& camo) {
    const bool even = (camo.max.raw >> 16) % 2 == 0;
    return fx(camo_fade_ticks + (even ? 1 : 0));
}

// the recharge after a fade: (max - left) / max x recharge max, ticks
Fx recharge_after(const Camo& camo) {
    return fx_mul_div(camo.max - camo.left, camo.recharge_max, camo.max);
}

void count_down(Fx& t) {
    if (t > Fx{}) {
        t -= fx(1);
        if (t < Fx{}) {
            t = Fx{};
        }
    }
}

} // namespace

// decomp: camo_init 0x74D30
void camo_init(Camo& camo, int frames, int recharge) {
    camo = Camo{};
    camo.max = fx(frames);
    camo.recharge_max = fx(recharge);
    if (frames <= 0) {
        camo.state = camo_none;
    } else {
        camo.state = camo_ready;
    }
}

// decomp: camo_enter_state_7 0x753C4
bool camo_break(Camo& camo) {
    switch (camo.state) {
    case camo_pressed:
    case camo_fading_in:
    case camo_ending:
    case camo_fading_out:
        // the fade's phase carried over as the same share of the 20-tick tail
        camo.state = camo_broken;
        camo.timer = fx_mul_div(camo.timer, fx(camo_tail_ticks), fx(camo_fade_ticks));
        return true;
    case camo_holding:
    case camo_hidden:
        camo.state = camo_broken;
        camo.timer = fx(camo_tail_ticks);
        return true;
    default:
        return false;
    }
}

// decomp: camo_update 0x74E00
void camo_update(World& world, Fighter& f) {
    Camo& camo = f.camo;
    if (camo.state == camo_none) {
        return;
    }
    // the one-step states move on
    if (camo.state == camo_pressed) {
        camo.state = camo_fading_in;
    } else if (camo.state == camo_ending) {
        camo.state = camo_fading_out;
    } else if (camo.state == camo_broken) {
        camo.state = camo_broken_tail;
    }

    // the press (X), or beginner mode's held press outside a scene
    const bool in_scene = world.battle_state.slots[f.slot].scene.state != 0;
    const bool press = (f.control.control_buttons & control_camouflage) ||
                       (camo.beginner && !in_scene);
    if (f.combat.alive && press) {
        if (camo.state == camo_ready) {
            if (camo.beginner && camo.beginner_wait > Fx{}) {
                camo.beginner_wait -= fx(1);
            } else {
                camo.state = camo_pressed;
                camo.timer = fx(camo_fade_ticks);
                camo.left = camo.max;
                fighter_bark(f, bark_camo_on);
            }
        } else if (camo.state == camo_hidden) {
            if (camo.beginner) {
                camo.beginner_wait = fx(camo_beginner_wait_ticks);
            } else {
                camo.state = camo_ending;
                camo.timer = fx(camo_fade_ticks);
            }
        }
    }

    count_down(camo.timer);
    count_down(camo.left);
    count_down(camo.recharge);

    switch (camo.state) {
    case camo_pressed:
    case camo_fading_in:
        if (camo.timer <= Fx{}) {
            camo.state = camo_holding;
            camo.timer = fx(camo_hold_ticks);
        }
        break;
    case camo_holding:
        if (camo.timer <= Fx{}) {
            camo.state = camo_hidden;
            camo.timer = Fx{};
        }
        break;
    case camo_hidden:
        // a quarter left: left / max <= 0.25 < (left + ticks) / max
        if (std::int64_t{camo.left.raw} * 4 <= camo.max.raw &&
            (std::int64_t{camo.left.raw} + fx_one_raw) * 4 > camo.max.raw) {
            fighter_bark(f, bark_camo_ending); // the time left crossing a quarter
        }
        if (camo.left <= hidden_end_left(camo)) {
            camo.state = camo_ending;
            camo.timer = fx(camo_fade_ticks);
        }
        break;
    case camo_ending:
    case camo_fading_out:
    case camo_broken:
    case camo_broken_tail:
        if (camo.timer <= Fx{}) {
            camo.state = camo_tail;
            camo.timer = fx(camo_tail_ticks);
            camo.recharge = recharge_after(camo);
            camo.left = Fx{};
        }
        break;
    case camo_tail:
        if (camo.timer <= Fx{}) {
            camo.state = camo_recharging;
            camo.timer = Fx{};
        }
        break;
    case camo_recharging:
        if (camo.recharge <= Fx{}) {
            camo.state = camo_ready;
            camo.recharge = Fx{};
        }
        break;
    default:
        break;
    }
}

// decomp: actor_camo_sound 0x93BDD
void camo_sound(World& world, const Fighter& f) {
    if (f.controller == -1 || world.battle_state.menu_state == app_ending_data ||
        f.combat.durability <= 0) {
        return;
    }
    switch (f.camo.state) {
    case camo_pressed:
        stage_event(world.stage, StageEventKind::Sound, f.position, bank_battle_player,
                    camo_sound_on);
        break;
    case camo_ending:
        stage_event(world.stage, StageEventKind::Sound, f.position, bank_battle_player,
                    camo_sound_off);
        break;
    case camo_broken:
        // played twice, as retail
        stage_event(world.stage, StageEventKind::Sound, f.position, bank_battle_player,
                    camo_sound_break);
        stage_event(world.stage, StageEventKind::Sound, f.position, bank_battle_player,
                    camo_sound_break);
        break;
    default:
        break;
    }
}
