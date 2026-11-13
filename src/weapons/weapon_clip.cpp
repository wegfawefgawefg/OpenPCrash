// The weapon clip: a weapon model's own animation (rotating barrels, recoil, opening pods) from
// its block's resource 1, played by the pull (decomp src/hand/weapon_laser.c weapon_anim_init,
// weapon_anim_update, weapon_anim_request, weapon_anim_switch,
// weapon_anim_step; src/hand/motion_sample.c anim_player_advance,
// anim_player_sample, model_node_write_transform). It writes the model's node
// values, which the pose places and the muzzles hang from, so it is gameplay and runs in the step.
//
// 60 Hz (docs/sixty.md "Weapon clips"): the player's rate is video_rate_nominal / frame_cadence,
// 1 a step (R3), and anim_turn counts by that rate to 90 ticks (R3). Retail samples every second
// tick; the port every tick, and a clip whose end is odd ends on its last frame without retail's
// step back (end - rate).
#include "mech/fighter.hpp"
#include "mech/rig_data.hpp"
#include "weapons/weapons.hpp"
#include "world/world.hpp"

namespace {

// AnimPlayer.rate: video_rate_nominal / frame_cadence, 2 an update; 1 a step
constexpr Fx clip_rate = fx(1);
// weapon_anim_update: an open weapon closes after anim_turn reaches 90 (ticks: it gains the rate)
constexpr Fx clip_hold = fx(90);

// a radian angle through the sampler's 16 bits: (short)(r x 65536 / 2 pi), truncated toward 0
// and wrapped, back to radians (the port's angle keeps the 16 bits)
Angle angle_16(Fx radians) {
    // units of a 2^32 turn, unwrapped, then truncated to a 2^16 turn's units
    const std::int64_t units = shift_round(radians.raw * fx_radians_to_units_q16, 16);
    const std::int64_t q = units / 65536; // C's truncation toward zero
    return {static_cast<std::uint32_t>(static_cast<std::uint16_t>(q)) << 16};
}

// anim_player_advance: past the end back to the start (a flag), then the rate on; a step past
// the end is taken back to end - rate with no flag
bool clip_advance(Weapon& w) {
    bool looped = false;
    if (w.clip_time >= w.clip_end) {
        w.clip_time = w.clip_start;
        looped = true;
    }
    w.clip_time += clip_rate;
    if (w.clip_end < w.clip_time) {
        w.clip_time = w.clip_end - clip_rate;
        looped = false;
    }
    return looped;
}

// weapon_anim_step: every node but the root that has a channel takes its flagged values at the
// clip's time (held at the set's end), then the player advances
bool clip_step(Weapon& w, const RigWeaponBlock& block, FxVec3* translate, Angle (*euler)[3],
               int count) {
    if (block.clip) {
        const Fx at = w.clip_time > block.end ? block.end : w.clip_time;
        for (int node = 1; node < count; ++node) {
            // anim_player_find_node: the first channel for the node
            const RigClipChannel* channel = nullptr;
            for (const RigClipChannel& c : block.channels) {
                if (c.node == node) {
                    channel = &c;
                    break;
                }
            }
            if (!channel) {
                continue;
            }
            Fx* place[3] = {&translate[node].x, &translate[node].y, &translate[node].z};
            for (int k = 0; k < 3; ++k) {
                if (channel->flags & (0x80u >> k)) {
                    *place[k] = fx_curve_value(channel->translate[k], at);
                }
                if (channel->flags & (0x400u >> k)) {
                    euler[node][k] = angle_16(fx_curve_value(channel->rotate[k], at));
                }
            }
        }
    }
    return clip_advance(w);
}

// weapon_anim_switch: the state asked for, and the range it plays
void clip_switch(Weapon& w, const RigWeaponBlock& block) {
    w.clip_state = w.clip_next;
    switch (w.clip_state) {
    case 0:
        w.clip_start = Fx{};
        w.clip_end = block.open;
        w.clip_time = w.clip_start;
        w.clip_next = 0;
        break;
    case 1: // opening
        w.clip_start = Fx{};
        w.clip_end = block.open;
        w.clip_time = w.clip_start;
        w.clip_next = 3;
        break;
    case 2: // a shot while open
        w.clip_start = block.open;
        w.clip_end = block.close;
        w.clip_time = w.clip_start;
        w.clip_next = 3;
        break;
    case 3: // open
        w.clip_next = 0;
        break;
    case 4: // closing: to the clip's end (anim_frames, the end anim_player_start found)
        w.clip_start = block.close;
        w.clip_end = block.end;
        w.clip_time = w.clip_start;
        w.clip_next = 0;
        break;
    default:
        break;
    }
}

} // namespace

void weapon_clip_init(Weapon& w, const RigWeaponBlock& block) {
    // anim_player_start: the whole clip; an unlocked one keeps that range for good
    w.clip_time = w.clip_start = Fx{};
    w.clip_end = block.end;
    w.clip_state = w.clip_next = 0;
    w.clip_turn = Fx{};
}

// decomp: weapon_anim_request 0x9B2DB
void weapon_anim_request(Weapon& w, const RigWeaponBlock& block, int want) {
    if (want == w.clip_state) {
        return;
    }
    if (!block.locked) {
        if (want == 2) {
            w.clip_next = 2;
        }
        return;
    }
    switch (w.clip_state) {
    case 0:
        if (want == 1 || want == 2) {
            w.clip_next = 1;
        }
        break;
    case 1:
        if (want == 4) {
            w.clip_next = 4;
            w.clip_time = w.clip_start; // anim_player_rewind
        }
        break;
    case 2:
        if (want == 4) {
            w.clip_next = 4;
        }
        break;
    case 3:
        if (want == 2 || want == 4) {
            w.clip_next = want;
        }
        break;
    case 4:
        if (want == 1 || want == 2) {
            w.clip_next = 1;
        } else if (want == 0) {
            w.clip_next = 0;
        }
        break;
    default:
        break;
    }
}

// decomp: weapon_reload_pulse 0x9B2C8
void weapon_reload_pulse(const World& world, const Fighter& f, Weapon& w) {
    const FighterRig* rig = world.battle ? rig_of_build(*world.battle, f.build) : nullptr;
    if (rig && w.mount >= 0 && w.mount < 4) {
        weapon_anim_request(w, rig->blocks[w.mount], 2);
    }
}

// decomp: weapon_anim_update 0x9B08B
void weapon_anim_update(Weapon& w, const RigWeaponBlock& block, bool human, FxVec3* translate,
                        Angle (*euler)[3], int count) {
    if (!block.locked) {
        // a pull plays the whole clip once
        if (w.clip_next == 2) {
            w.clip_state = 2;
            w.clip_next = 0;
            w.clip_time = w.clip_start;
        }
        if (w.clip_state == 2 && clip_step(w, block, translate, euler, count)) {
            w.clip_state = 0;
        }
        return;
    }
    // a pull opens the weapon (a human's only: an AI's stays shut, docs/bugs.md); one while open
    // plays the shot; an open weapon closes after anim_turn reaches 90
    if (w.clip_state == 0 && w.clip_next) {
        if (human && w.clip_next == 1) {
            clip_switch(w, block); // and the opening sound (bank 3 cue 0x15)
        }
    } else if (w.clip_state == 3 && (w.clip_next == 2 || w.clip_next == 4)) {
        clip_switch(w, block);
    }
    if (w.clip_state == 0) {
        return;
    }
    if (w.clip_state == 3) {
        w.clip_turn += clip_rate;
        if (w.clip_turn < clip_hold) {
            return;
        }
        weapon_anim_request(w, block, 4);
        clip_switch(w, block);
        w.clip_turn = Fx{};
    }
    if (clip_step(w, block, translate, euler, count)) {
        w.clip_turn = Fx{};
        clip_switch(w, block);
    }
}
