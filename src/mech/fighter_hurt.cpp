// The motion states that come with weapons and damage (decomp src/hand/u_088880.c): the stagger
// sv_motion_state_7, the melee swing sv_motion_state_9 with melee_home_step,
// and the dead sv_motion_state_10. Phases step by `ticks`, 1 at 60 Hz (auto).
#include "mech/fighter.hpp"
#include "mech/fighter_rates.hpp"

namespace {

// the unguided swing's step forward, 0.25 x ticks an update: 0.25 a step (auto)
constexpr Fx swing_step = fx_f(0.25);
// the homing turn, at most pi/16 (human) or 0.13744467 (AI) an update toward a target within
// 90 degrees; R8: half a step (sixty.md "Melee")
constexpr Angle home_turn_human = angle_f_radians(0.19634955 / 2.0);
constexpr Angle home_turn_ai = angle_f_radians(0.13744467 / 2.0);
// the climb, (aim height - tip y) x ticks / 10 an update, a smoothing of 0.2 an update; R7:
// 1 - sqrt(0.8) a step (sixty.md "Melee": ticks / 10 alone would be 0.1, 19% a pair)
constexpr Fx climb_gain = fx_f(0.10557280900008414);

// the lunge's length (the tip's flat distance to the target) and climb,
// and the heading turned toward the target. *length 0 when it is behind
// decomp: melee_home_step 0x911C5
void melee_home_step(Fighter& f, Fx* length_out, Fx* climb) {
    const FighterCombat& c = f.combat;
    if (!c.melee_target_known || !c.melee_tip_known) {
        return;
    }
    *climb = fx_clamp(fx_mul_near(c.melee_target_stored.y - c.melee_tip.y, climb_gain),
                      -f.jump_speed, f.jump_speed);
    FxVec3 v = c.melee_tip - c.melee_target_point;
    v.y = Fx{};
    *length_out = length(v);
    // the heading that faces the target from the tip: forward is (sin h, 0, cos h) here
    const Angle toward = angle_atan2(-v.x, -v.z);
    const Angle off = toward - f.heading;
    if (angle_within(off, angle_quarter)) {
        f.heading += angle_clamp(off, f.controller != -1 ? home_turn_human : home_turn_ai);
        return;
    }
    *length_out = Fx{};
}

// the root track's move this step, into the velocity: x and z on the ground, all three in the air
void root_velocity(Fighter& f, const BattleData& data) {
    const FxVec3 out = turned_by(motion_root_delta(f, data), f.heading);
    f.velocity = f.grounded ? FxVec3{out.x, f.velocity.y, out.z} : out;
}

} // namespace

void motion_state_stagger(Fighter& f, const BattleData& data) {
    f.motion_phase_prev = f.motion_phase;
    f.motion_phase += fx(1);
    if (f.motion_phase >= f.combat.stagger_end) {
        motion_set_animation(f, data, slot_idle, state_idle, motion_whole, Fx{});
    }
    root_velocity(f, data);
}

void motion_state_melee(Fighter& f, const BattleData& data) {
    FighterCombat& c = f.combat;
    f.motion_phase_prev = f.motion_phase;
    f.motion_phase += fx(1);
    Fx forward;
    Fx climb;
    if (f.motion_mode == motion_lead_in || f.motion_mode == motion_loop) {
        c.melee_active = true;
        if (c.melee_connected) {
            melee_home_step(f, &forward, &climb);
            // vy = climb + g, which the integration's gravity takes back off
            f.velocity.y = climb + gravity_60;
        } else {
            forward = swing_step;
        }
        if (f.motion_phase >= f.timing.loop_end) {
            f.motion_mode = motion_lead_out;
        }
    } else {
        c.melee_active = false;
        if (f.motion_phase >= f.timing.end) {
            f.at_speed = false;
            if (f.grounded) {
                motion_set_animation(f, data, slot_idle, state_idle, motion_whole, Fx{});
            } else {
                motion_set_animation(f, data, slot_fall, state_fall, motion_loop,
                                     data.legs[f.leg_class].timing[slot_fall].loop_start);
            }
        }
    }
    // capped at lunge_speed_max (run_rate x 2 x ticks an update: per step already, R8)
    forward = fx_min(forward, f.lunge_speed_max);
    const FxVec3 out = turned_by({Fx{}, Fx{}, forward}, f.heading);
    f.velocity = {out.x, f.velocity.y, out.z};
}

void motion_state_dead(Fighter& f, const BattleData& data) {
    f.motion_phase_prev = f.motion_phase;
    f.motion_phase += fx(1);
    if (f.motion_phase > f.timing.end) {
        f.motion_phase = f.timing.end;
        // defeat_frames counts updates in retail (a dead AI retires past 120); here steps, so the
        // reader tests > 241 (sixty.md R12 E)
        f.combat.defeat_frames += 1;
    }
    const FxVec3 out = turned_by(motion_root_delta(f, data), f.heading);
    f.velocity = {out.x, f.velocity.y, out.z};
}
