#pragma once
// The fighter step's 60 Hz constants that more than one file uses. Each says the original value,
// where it is, and the rule of docs/sixty.md that converts it (R1 velocity half, R2 acceleration
// quarter, R5 angle half, R6 factor sqrt, R7 smoothing 1 - sqrt(1 - a), R12 counter patterns).
#include "fx/angle.hpp"
#include "fx/fx.hpp"

// g30 = 9.8 / (video_rate_nominal / 2)^2 x Battle.gravity_scale 2.5 = 0.0272222 per update squared
// (sv_motion_integrate, battle_task_begin). Gravity doesn't convert itself: it is
// written with the nominal 60, not the cadence
inline constexpr Fx gravity_30 = fx_f(9.8 / 900.0 * 2.5);
// R2: 9.8 / 3600 x 2.5 = 0.00680556 per step squared
inline constexpr Fx gravity_60 = fx_f(9.8 / 3600.0 * 2.5);
// Battle.boost_gravity_scale 6.0: the dive's jump_step is -6 g (actor_air_control); R2
inline constexpr Fx dive_gravity_60 = fx_f(-6.0 * 9.8 / 3600.0 * 2.5);

// velocity and push caps, length 5 per update (sv_motion_integrate); R14: 2.5, on the
// stored velocity and on the displacement
inline constexpr Fx velocity_cap_60 = fx_f(2.5);

// airborne_time past which a fighter off the ground falls, 20 ticks (sv_grounded_state_update);
// R3, counted 1 a step
inline constexpr Fx fall_after_ticks = fx(20);

// the skid ends under 0.2 a update (actor_slide_step); R13 scales it with the start value
inline constexpr Fx skid_stop_30 = fx_f(0.2);

// motion slots (motion_set_animation's clip numbers; the leg set's slots)
inline constexpr int slot_walk = 0;
inline constexpr int slot_walk_back = 1;
inline constexpr int slot_run = 2;
inline constexpr int slot_run_reverse = 5; // run clips + 3 reversed
inline constexpr int slot_turn_right = 8;   // strafe-turn and turn in place (8 / 9)
inline constexpr int slot_turn_left = 9;
inline constexpr int slot_jump = 10;
inline constexpr int slot_fall = 11;
inline constexpr int slot_dodge_right = 13; // ground, to body -x (the right); +2 in the air
inline constexpr int slot_dodge_left = 14;
inline constexpr int slot_skid = 27;
inline constexpr int slot_idle = 28;

// motion states (Actor.motion_state, sv_motion_dispatch)
enum MotionState {
    state_walk = 0,
    state_walk_back = 1,
    state_run = 2,
    state_run_reverse = 3,
    state_jump = 4,
    state_fall = 5,
    state_dodge = 6,
    state_stagger = 7,
    state_melee = 9,
    state_dead = 10,
    state_idle = 11,
    state_turn = 12,
    state_skid = 13,
    state_strafe_turn = 14,
};

// motion modes (Actor.motion_mode): where the clip is between its loop marks
enum MotionMode {
    motion_whole = 0,    // no loop marks used: the clip runs to its end and starts again
    motion_lead_in = 1,  // before loop_start
    motion_loop = 2,     // between loop_start and loop_end
    motion_lead_out = 3, // after loop_end, to the end
};

// motion event bits (ActorEffects.motion_events): the world raises the run's two, presentation
// (effects/fighter_effects.cpp) reads them all off the state's changes
inline constexpr int motion_event_hover = 0x1; // a hover moving
inline constexpr int motion_event_jump = 0x2;  // the jump fire
inline constexpr int motion_event_dive = 0x4;  // the down vernier
inline constexpr int motion_event_jump_dust = 0x8;
inline constexpr int motion_event_vernier_left = 0x10; // a dodge's verniers, by side
inline constexpr int motion_event_vernier_right = 0x20;
inline constexpr int motion_event_verniers = 0x30;
inline constexpr int motion_event_dodge = 0x40;
inline constexpr int motion_event_laser = 0x80;
inline constexpr int motion_event_run_start = 0x100; // actor_run_start
inline constexpr int motion_event_run_stop = 0x200;  // a run let go or reversed
inline constexpr int motion_event_foot_0 = 0x400;    // << foot: a foot stepped
inline constexpr int motion_event_runs = motion_event_run_start | motion_event_run_stop;
