#pragma once
// Optical camouflage (O.C.) in the world: the block at Actor.camo_state (+0x3494) and camo_update,
// camo_init, camo_enter_state_7, camo_gauge_fraction and
// actor_camo_sound (decomp src/od/u_074d30.c; docs/port/gameplay.md "Camouflage",
// parts.md "Options"). Every timer is in ticks, counted 2 an update in retail and 1 a step here
// (docs/sixty.md "Camouflage").
#include "fx/fx.hpp"

struct World;
struct Fighter;

// the states: 0 ready, 1 just pressed and 2 fading in, 3 the hold, 4 hidden, 5 just ending and
// 6 fading out, 7 just broken and 8 the broken tail, 9 the tail, 10 recharging, 11 none fitted.
// 1, 5 and 7 last one step (one update in retail). 1..4 hide from missiles, locks and the AI;
// 1..8 draw cloaked
enum CamoState : int {
    camo_ready = 0,
    camo_pressed = 1,
    camo_fading_in = 2,
    camo_holding = 3,
    camo_hidden = 4,
    camo_ending = 5,
    camo_fading_out = 6,
    camo_broken = 7,
    camo_broken_tail = 8,
    camo_tail = 9,
    camo_recharging = 10,
    camo_none = 11,
};

// The press always runs the 60-tick fade in and the 20-tick hold before the time left is compared
// with the fade out's 60, so any positive options total hides for at least 80 ticks: a lone
// Pocket Watch (6) is an 80-tick cloak every 160 ticks. Retail as read; UNKNOWN until checked
// against the running original (docs/bugs.md, the Pocket Watch row). The floor is these two
// (camo_update's 60.0f and 20.0f, 0x42700000 and 0x41A00000)
inline constexpr int camo_fade_ticks = 60;
inline constexpr int camo_hold_ticks = 20;
// the broken and the ended tails (camo_enter_state_7, state 9): 20 ticks
inline constexpr int camo_tail_ticks = 20;
// beginner mode re-arms 120 ticks after the hidden state (beginner_camo_wait)
inline constexpr int camo_beginner_wait_ticks = 120;

struct Camo {
    int state = camo_none;
    Fx timer;        // the phase timer, ticks (+4)
    Fx left;         // time left (+8)
    Fx recharge;     // recharge left (+0xC)
    Fx max;          // the options' camo_frames summed (+0x10)
    Fx recharge_max; // the options' camo_recharge summed, may be negative (+0x14)
    bool beginner = false; // Actor.beginner_mode: a human's button config, not in the lessons
    Fx beginner_wait;      // Actor.beginner_camo_wait, ticks
    // the chip's camo_distortion (Actor.chip_shader_param): / 100 is the refraction shader's k
    // a human outside VS draws with (presentation's, src/render/camo_draw.cpp)
    int chip_shader_param = 25;
};

// camo_init from the options' sums (actor_init): state 11 when the total is 0 or less
void camo_init(Camo& camo, int frames, int recharge);
// camo_update, in the fighter's step after the pose and the weapons' placement, before
// actor_settle_damage (combat_fighter_late)
void camo_update(World& world, Fighter& f);
// camo_enter_state_7: damage or death cuts a cloak to its 20-tick tail
bool camo_break(Camo& camo);
// actor_camo_sound from actor_sounds_update: a human with durability left hears its
// cloak start (3/0xC), end (3/0x10) and break (3/0xD twice). After the settle
void camo_sound(World& world, const Fighter& f);

// decomp: camo_is_state_1_to_4 0x74DAA
inline bool camo_hides(const Camo& camo) {
    return camo.state >= camo_pressed && camo.state <= camo_hidden;
}
// decomp: camo_is_state_1_to_8 0x74DD5
inline bool camo_drawn_cloaked(const Camo& camo) {
    return camo.state >= camo_pressed && camo.state <= camo_broken_tail;
}
