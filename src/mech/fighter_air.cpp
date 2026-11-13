// The air half of the motion state machine (decomp src/hand/u_088880.c): the jump with its
// thrust and dive, the fall and its landing, the step dodge, air control, and the skid. Most of
// what doesn't convert itself at 60 Hz is here; docs/sixty.md "Jumping and the air", "Dodging,
// skidding, stagger" and the flagged "Jump", "Air push", "Landing" and "Decays" sections.
#include "mech/fighter.hpp"
#include "mech/fighter_rates.hpp"

#include "fx/mat.hpp"

namespace {

constexpr Fx stick_dead = fx_f(0.01);
// lift-off when motion_phase >= 10, the phase starting at 0 and stepped before the test in the
// same update (sv_motion_state_4): R12 A, 2 * ceil(10 / 2) - 1 = 9 (sixty.md "Jump")
constexpr Fx crouch_ticks = fx(9);
// thrust ease vy = 0.9 vy + 0.1 jump_speed an update (D_001B5AF4 0.9, D_001B5B70 0.1,
// actor_air_control); R6/R7: sqrt(0.9) and 1 - sqrt(0.9)
constexpr Fx thrust_keep = fx_f(0.9486832980505138);
constexpr Fx thrust_take = fx_f(0.0513167019494862);
// air forward push: stick x 1/256 x ticks added to the velocity every update, an acceleration;
// R2: stick x 0.5/256 a step, not the ticks-converted 1/256 (sixty.md "Air push")
constexpr Fx air_push = fx_f(0.5 / 256.0);
// air drag x and z x 0.985 an update (actor_air_control); R6: sqrt(0.985), inside R10's dv
constexpr Fx air_drag = fx_f(0.99247166);
// the dodge slide's impulse x 0.99 an update (sv_motion_state_6 mode 2); R13: sqrt(0.99)
// a step, and the impulse started at 2r / (1 + r) of itself so each pair moves an update's worth
constexpr Fx dodge_decay = fx_f(0.99498744);
constexpr Fx dodge_start = fx_f(0.99748744);
// the ground dodge's ease-out, impulse x ((motion_end - phase) x 0.05 + 0.05), sampled half a tick
// later at 60 Hz so a pair averages what retail's even-tick samples give (sixty.md "Decays")
constexpr Fx ease_step = fx_f(0.05);
constexpr Fx ease_half_tick = fx_f(0.5);
// the air dodge's ease-out, x and z x 0.9 an update; R6: sqrt(0.9), inside R10's dv
constexpr Fx dodge_air_ease = fx_f(0.9486832980505138);
// dodge intangibility, Battle.dodge_intangible_frames, 12.5 (0x41480000, battle_task_begin),
// set in motion and counted down by ticks in actor_settle_damage the same update:
// retail is intangible while it is over 0, 10.5 .. 0.5, six shot passes, 12 ticks. Kept at 12.5
// and counted 1 a step it is 11.5 .. 0.5: 12 steps (docs/sixty.md)
constexpr Fx dodge_intangible_ticks = fx_f(12.5);
// the dodge's slide starts once the phase reaches the clip's loop_start (1): retail reaches it on
// the dodge's own update and moves from the next one, tick 2; at 60 Hz the slide starts at the
// same tick when the threshold is rounded up to an even tick, 2 ceil(S / 2) (docs/sixty.md)
Fx even_ticks(Fx threshold) {
    const std::int32_t whole = (threshold.raw + fx_one_raw - 1) >> fx_shift;
    return fx((whole + 1) / 2 * 2);
}
// actor_land: D_0017CB5C 2.0 (a two-radius drop), 105 off the clip end, clamp 0.05..1
constexpr Fx land_least = fx_f(0.05);
constexpr Fx land_clip_lead = fx(105);

// actor_air_control, with the jump recipe of sixty.md: the thrust budget kept in retail
// units and charged on the first step of each pair (pairs counted from the lift-off step), the
// same quarter step on both steps of the pair; every change it makes to the velocity is a force
// (R10's dv)
// decomp: actor_air_control 0x9052F
void fighter_air_control(Fighter& f, std::int64_t step) {
    const FxVec3 before = f.velocity;
    const std::uint32_t buttons = f.control.control_buttons;
    if (f.motion_state == state_jump) {
        if (buttons & control_jump_held) {
            if (!f.jump_cut) {
                const bool pair_first = (step - f.liftoff_step) % 2 == 0;
                // a pair's second step thrusts when its first did (retail's one update)
                const bool thrust = pair_first ? f.jump_thrust_left_30 > Fx{} : f.pair_thrust;
                if (pair_first) {
                    f.pair_thrust = thrust;
                }
                if (thrust) {
                    f.velocity.y = fx_mul_near(thrust_keep, f.velocity.y) +
                                   fx_mul_near(thrust_take, f.jump_speed);
                    if (pair_first) {
                        // jump_step = g cancels gravity while the budget lasts; the last
                        // partial update gets g - left (retail's, kept)
                        Fx step_30;
                        if (gravity_30 < f.jump_thrust_left_30) {
                            step_30 = gravity_30;
                            f.jump_thrust_left_30 -= gravity_30;
                        } else {
                            step_30 = gravity_30 - f.jump_thrust_left_30;
                            f.jump_thrust_left_30 = Fx{};
                        }
                        f.pair_jump_step = fx_raw(static_cast<std::int32_t>(
                            div_round(step_30.raw, 4))); // R2
                    }
                    f.jump_step = f.pair_jump_step;
                }
            } else if (!f.dive_used) {
                // jump_speed - vy + (jump_thrust - left) > jump_speed, every term per step (R1)
                const Fx spent = fx_raw(static_cast<std::int32_t>(
                    div_round((f.jump_thrust_30 - f.jump_thrust_left_30).raw, 2)));
                if (f.jump_speed - f.velocity.y + spent > f.jump_speed) {
                    f.dive_used = true;
                    f.at_speed = false;
                }
            }
        }
    } else if ((buttons & control_jump_pressed) && !f.dive_used) {
        f.at_speed = false;
        f.dive_used = true;
    }
    // the dive: -6 g every step after it (Battle.boost_gravity_scale 6.0), R2
    if (f.dive_used) {
        f.jump_step = dive_gravity_60;
    }
    f.velocity.y += f.jump_step;

    // steering: the heading at air_turn_deg a tick; with dash the velocity turns a third of it
    const Fx turn = axis(f, axis_leg_turn);
    if (fx_abs(turn) > stick_dead) {
        const Angle angle = -angle_scaled(f.air_turn_step, turn);
        f.heading += angle;
        if (buttons & control_dash) {
            f.velocity = transform(f.velocity, mat3_rotation_y(angle_scaled(angle, fx_ratio(1, 3))));
        }
    }
    // the forward push along the heading
    const Fx forward = axis(f, axis_forward);
    if (fx_abs(forward) > stick_dead) {
        f.velocity += turned_by({Fx{}, Fx{}, fx(1)}, f.heading) * fx_mul_near(forward, air_push);
    }
    f.velocity.x = fx_mul_trunc(f.velocity.x, air_drag);
    f.velocity.z = fx_mul_trunc(f.velocity.z, air_drag);
    f.force_dv += f.velocity - before;
}

// the recovery clip's start from the landing speed past a two-radius drop's,
// in retail's per-update units (2 x the step's velocity) and g30 (sixty.md "Landing")
// decomp: actor_land 0x90AFD
void fighter_land(Fighter& f) {
    const Fx drop_speed = fx_sqrt(fx_mul_near(f.radius * 4, gravity_30)); // sqrt(2 x 2r x g)
    Fx bounce = -(f.velocity_prev.y * 2 + drop_speed) * 2;
    bounce = fx_mul_div(bounce, fx(100) - f.leg_landing, fx(100));
    bounce = fx_clamp(bounce, land_least, fx(1));
    const Fx recovery = fx_mul_near(f.timing.end - land_clip_lead, bounce);
    f.motion_phase = f.timing.end - recovery;
    f.motion_phase_prev = f.motion_phase;
    f.skid_velocity = {fx_mul_near(f.velocity_prev.x, f.skid_start), Fx{},
                       fx_mul_near(f.velocity_prev.z, f.skid_start)};
    f.takeoff_velocity = {};
    f.motion_mode = motion_lead_out;
    f.pose_blend_steps = 0;
    f.landed = true;
    f.airborne = false;
}

} // namespace

// decomp: sv_motion_state_4 0x8E9DB
void motion_state_jump(Fighter& f, const BattleData& data, std::int64_t step) {
    f.position_prev = f.position;
    f.motion_phase_prev = f.motion_phase;
    if (!(f.control.control_buttons & control_jump_held)) {
        f.jump_cut = true;
    }
    // taken before lift-off: no air control on the lift-off step
    const bool in_air = !f.grounded;
    if (f.motion_mode == motion_lead_in) {
        f.motion_phase += fx(1);
        if (f.motion_phase >= crouch_ticks) {
            // lift-off: last step's velocity (a running jump keeps its run), y floored at 0, plus
            // jump_speed, half of retail's (sixty.md "Jump": not re-derived with g60)
            f.velocity = f.takeoff_velocity;
            f.grounded = false;
            f.velocity.y = fx_max(f.velocity.y, Fx{}) + f.jump_speed;
            f.jump_thrust_left_30 = f.jump_thrust_30;
            f.motion_mode = motion_loop;
            f.liftoff_step = step;
            // the lift-off step and the next one stand for retail's lift-off update
            f.air_control_hold = 1;
        }
    } else if (f.motion_mode == motion_loop) {
        f.motion_phase += fx(1);
        if (f.motion_phase >= f.timing.loop_end) {
            f.motion_phase = f.timing.loop_start + f.motion_phase - f.timing.loop_end;
        }
        if (f.velocity.y <= Fx{}) {
            f.motion_mode = motion_lead_out;
            f.motion_phase = f.timing.loop_end;
            f.motion_phase_prev = f.timing.loop_end;
            motion_set_animation(f, data, slot_fall, state_fall, motion_lead_in, f.motion_phase);
        }
    } else {
        f.motion_phase += fx(1);
        if (f.motion_phase >= f.timing.end) {
            f.at_speed = true;
            motion_set_animation(f, data, slot_fall, state_fall, motion_lead_in, f.motion_phase);
        }
    }
    if (in_air) {
        if (f.air_control_hold > 0) {
            f.air_control_hold -= 1;
        } else {
            fighter_air_control(f, step);
        }
    }
}

// decomp: sv_motion_state_5 0x8ECBE
void motion_state_fall(Fighter& f, const BattleData& data, std::int64_t step) {
    f.position_prev = f.position;
    f.motion_phase_prev = f.motion_phase;
    if (!(f.control.control_buttons & control_jump_held)) {
        f.jump_cut = true;
    }
    f.motion_phase += fx(1);
    if (f.motion_mode == motion_lead_in) {
        if (f.motion_phase >= f.timing.loop_start) {
            f.motion_mode = motion_loop;
        }
        if (f.grounded) {
            fighter_land(f);
        }
    } else if (f.motion_mode == motion_loop) {
        if (f.motion_phase >= f.timing.loop_end) {
            f.motion_phase = f.timing.loop_start + f.motion_phase - f.timing.loop_end;
        }
        if (f.grounded) {
            fighter_land(f);
        }
    } else {
        // the landing's recovery, skidding on when it came down at speed
        if (f.at_speed) {
            fighter_slide_step(f);
        }
        if (f.motion_phase >= f.timing.end) {
            motion_set_animation(f, data, slot_idle, state_idle, motion_whole, Fx{});
            f.position_prev = f.position;
            f.skid_velocity = {};
        }
    }
    if (!f.grounded) {
        fighter_air_control(f, step);
    }
}

// decomp: sv_motion_state_6 0x8EEF9
void motion_state_dodge(Fighter& f, const BattleData& data, std::int64_t step) {
    f.position_prev = f.position;
    f.motion_phase_prev = f.motion_phase;
    fighter_turn_by_stick(f);
    f.motion_phase += fx(1);
    if (f.motion_mode == motion_lead_in) {
        if (f.motion_phase >= even_ticks(f.timing.loop_start)) {
            f.motion_mode = motion_loop;
            f.dodge_sliding = true;
        }
    } else if (f.motion_mode == motion_loop) {
        f.dodge_impulse.x = fx_mul_trunc(f.dodge_impulse.x, dodge_decay);
        f.dodge_impulse.z = fx_mul_trunc(f.dodge_impulse.z, dodge_decay);
        const FxVec3 world = turned_by(f.dodge_impulse, f.heading);
        f.velocity = {world.x, f.velocity.y, world.z};
        if (f.motion_phase >= f.timing.loop_end) {
            f.motion_mode = motion_lead_out;
            f.dodge_sliding = false;
        }
    } else if (f.motion_phase >= f.timing.end) {
        if (f.grounded) {
            motion_set_animation(f, data, slot_idle, state_idle, motion_whole, Fx{});
        } else {
            motion_set_animation(f, data, slot_fall, state_fall, motion_loop,
                                 data.legs[f.leg_class].timing[slot_fall].loop_start);
        }
    } else if (f.grounded) {
        const Fx ease =
            fx_mul_near(f.timing.end - f.motion_phase - ease_half_tick, ease_step) + ease_step;
        const FxVec3 world = turned_by(f.dodge_impulse * ease, f.heading);
        f.velocity = {world.x, f.velocity.y, world.z};
    } else {
        const FxVec3 before = f.velocity;
        f.velocity.x = fx_mul_trunc(f.velocity.x, dodge_air_ease);
        f.velocity.z = fx_mul_trunc(f.velocity.z, dodge_air_ease);
        f.force_dv += f.velocity - before;
    }
    if (!f.grounded) {
        fighter_air_control(f, step);
    }
}

// last step's flat velocity in body space plus the dodge speed sideways
// (both per step: auto), started at R13's scale
// decomp: actor_try_dodge 0x8D90C
bool fighter_try_dodge(Fighter& f, const BattleData& data) {
    const std::uint32_t buttons = f.control.control_buttons;
    if (!f.dodge_ready || !(buttons & (control_step_right | control_step_left)) ||
        !state_in(f.motion_state,
                  {state_idle, state_skid, state_turn, state_walk, state_walk_back, state_run,
                   state_run_reverse, state_jump, state_fall, state_strafe_turn})) {
        return false;
    }
    const FxVec3 flat_v{f.velocity_prev.x, Fx{}, f.velocity_prev.z};
    f.dodge_impulse = turned_by(flat_v, -f.heading);
    int clip = 0;
    if (buttons & control_step_right) {
        clip = f.grounded ? slot_dodge_right : slot_dodge_right + 2;
        f.dodge_impulse.x -= f.grounded ? f.dodge_speed_ground : f.dodge_speed_air;
    } else {
        clip = f.grounded ? slot_dodge_left : slot_dodge_left + 2;
        f.dodge_impulse.x += f.grounded ? f.dodge_speed_ground : f.dodge_speed_air;
    }
    f.dodge_impulse = {fx_mul_near(f.dodge_impulse.x, dodge_start), f.dodge_impulse.y,
                       fx_mul_near(f.dodge_impulse.z, dodge_start)};
    motion_set_animation(f, data, clip, state_dodge, motion_lead_in, Fx{});
    f.intangible_frames = dodge_intangible_ticks;
    f.skid_velocity = {};
    f.at_speed = false;
    return true;
}

// the skid decays by the leg's factor and adds to the ground velocity
// until it's under its stop (R13: sqrt of the factor a step, start and stop scaled at spawn)
// decomp: actor_slide_step 0x90CA1
void fighter_slide_step(Fighter& f) {
    if (f.grounded && !shorter_than(f.skid_velocity, f.skid_stop)) {
        // actor_turn_by_stick_slide: twice the turn while skidding
        const Fx stick = fx_clamp(axis(f, axis_leg_turn), fx(-1), fx(1));
        f.heading_prev = f.heading;
        f.heading -= angle_scaled(f.turn_step, stick * 2);
        f.skid_velocity = {fx_mul_trunc(f.skid_velocity.x, f.slide_decay), Fx{},
                           fx_mul_trunc(f.skid_velocity.z, f.slide_decay)};
        f.skidding = true;
        f.velocity += f.skid_velocity;
        return;
    }
    f.skid_velocity = {};
    f.at_speed = false;
}
