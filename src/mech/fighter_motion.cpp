// The ground half of the motion state machine (decomp src/hand/u_088880.c): the dispatch, the
// grounded update with its walk/run input and jump, and the walk, run, idle, turn, skid and
// strafe-turn handlers. Rates are per tick and step by `ticks`, which is 1 at 60 Hz, so they
// convert themselves (docs/sixty.md "Ground movement, turning, input", auto); the rows that don't
// say so beside them.
#include "mech/fighter.hpp"
#include "mech/fighter_rates.hpp"

#include "fx/mat.hpp"

namespace {

// stick thresholds (actor_ground_input and the handlers)
constexpr Fx stick_dead = fx_f(0.01);
constexpr Fx stick_run = fx_f(0.7);
constexpr Fx stick_walk_floor = fx_f(0.5); // the walk clip's least playback (sv_motion_state_0_1)
// walk to run and back: the stick past (or under) 0.7 while motion_time > 30, motion_time +=
// ticks after the test: retail switches at 32 ticks; > 30 at 60 Hz would switch at 31, so the
// test is > 31 (sixty.md R12 F)
constexpr Fx run_switch_ticks = fx(31);
// run_lean += (stick - lean) / 16 an update (actor_run_clip_select); R7: 1 - sqrt(15/16)
constexpr Fx run_lean_follow = fx_f(0.031754163);
constexpr Fx run_lean_switch = fx_f(0.3);
// the turn clip's playback, 0.25 x ticks x |stick| (sv_motion_state_12): auto
constexpr Fx turn_clip_rate = fx_f(0.25);
// idle bark after 900 ticks (sv_motion_state_11): auto
constexpr Fx idle_bark_ticks = fx(900);

// a ground speed's ramp in over the lead-in and out after the loop (the original divides the
// lead-out by loop_end, not by its length: kept)
Fx ramped(const Fighter& f, Fx speed) {
    if (f.motion_phase < f.timing.loop_start) {
        return fx_mul_div(speed, f.motion_phase, f.timing.loop_start);
    }
    if (f.motion_phase < f.timing.loop_end) {
        return speed;
    }
    return fx_mul_div(speed, f.timing.end - f.motion_phase, f.timing.loop_end);
}

// the local move turned by the heading into x and z, y kept
void set_ground_velocity(Fighter& f, FxVec3 local) {
    const FxVec3 world = turned_by(local, f.heading);
    f.velocity = {world.x, f.velocity.y, world.z};
}

void wrap_loop(Fighter& f) {
    if (f.motion_phase >= f.timing.loop_end) {
        f.motion_phase = f.timing.loop_start + f.motion_phase - f.timing.loop_end;
    }
}

// decomp: actor_run_clip_select 0x8F558
void fighter_run_clip_select(Fighter& f, const BattleData& data) {
    bool restart = f.motion_phase == Fx{};
    const int kind = f.leg_kind;
    const bool rollers = kind == 3 || kind == 4 || kind == 5 || kind == 6 || f.leg_class == 4;
    if (!rollers || f.leg_class == 20) {
        if (restart) {
            if (f.reverse) {
                motion_set_animation(f, data, slot_run_reverse, state_run_reverse, motion_lead_in,
                                     Fx{});
            } else {
                motion_set_animation(f, data, slot_run, state_run, motion_lead_in, Fx{});
            }
        }
        return;
    }
    // the lean clips 3 and 4 (+3 reversed) by the smoothed turn stick
    const auto clip_of = [](Fx lean) {
        return lean >= run_lean_switch ? 3 : lean <= -run_lean_switch ? 4 : 2;
    };
    const int before = clip_of(f.run_lean);
    f.run_lean += fx_mul_near(axis(f, axis_leg_turn) - f.run_lean, run_lean_follow);
    const int clip = clip_of(f.run_lean);
    restart = restart || clip != before;
    f.run_lean = fx_clamp(f.run_lean, fx(-1), fx(1));
    if (restart) {
        if (f.reverse) {
            motion_set_animation(f, data, clip + 3, state_run_reverse, motion_lead_in,
                                 f.motion_phase);
        } else {
            motion_set_animation(f, data, clip, state_run, motion_lead_in, f.motion_phase);
        }
    }
}

// decomp: actor_run_start 0x904C1
void fighter_run_start(Fighter& f, const BattleData& data) {
    f.motion_phase = Fx{};
    f.run_lean = Fx{};
    f.reverse = axis(f, axis_forward) < -stick_dead;
    f.motion_events |= motion_event_run_start;
    fighter_run_clip_select(f, data);
}

// decomp: actor_ground_input 0x8FE9E
void fighter_ground_input(Fighter& f, const BattleData& data) {
    if (f.motion_state == state_jump || f.motion_state == state_fall) {
        return;
    }
    const Fx forward = axis(f, axis_forward);
    const Fx size = fx_abs(forward);
    const bool walking = f.motion_state == state_walk || f.motion_state == state_walk_back;
    const bool running = f.motion_state == state_run || f.motion_state == state_run_reverse;
    const bool letting_go = (!f.reverse && forward < stick_dead) || (f.reverse && forward > -stick_dead);
    const auto stop_clip = [&f]() {
        f.motion_mode = motion_lead_out;
        f.motion_phase = f.timing.end;
        f.motion_phase_prev = f.motion_phase;
    };
    if (f.control.control_buttons & control_dash) {
        if (f.motion_state == state_idle || f.motion_state == state_skid || walking) {
            if (fx_abs(axis(f, axis_leg_turn)) > stick_dead || size > stick_dead) {
                fighter_run_start(f, data);
                return;
            }
            if (walking) {
                stop_clip();
            }
        } else if ((!f.reverse && forward < -stick_run) || (f.reverse && forward > stick_run)) {
            stop_clip();
            f.motion_events |= motion_event_run_stop;
        }
        return;
    }
    // letting go: the walk stops, the run skids on with last step's velocity
    if (walking && letting_go) {
        f.motion_time = Fx{};
        f.skid_velocity = {fx_mul_near(f.velocity_prev.x, f.skid_start), Fx{},
                           fx_mul_near(f.velocity_prev.z, f.skid_start)};
        stop_clip();
        return;
    }
    if (running && f.motion_mode != motion_lead_out && letting_go) {
        f.motion_time = Fx{};
        f.skid_velocity = {fx_mul_near(f.velocity_prev.x, f.skid_start), Fx{},
                           fx_mul_near(f.velocity_prev.z, f.skid_start)};
        stop_clip();
        f.pose_blend_steps = 0;
        f.motion_events |= motion_event_run_stop;
    }
    if (walking) {
        if (size > stick_run) {
            if (f.motion_time > run_switch_ticks) {
                fighter_run_start(f, data);
                return;
            }
            f.motion_time += fx(1);
        } else if (f.motion_time > Fx{}) {
            f.motion_time -= fx(1);
        }
        return;
    }
    if (running) {
        if (size < stick_run) {
            if (f.motion_time > run_switch_ticks) {
                const int clip = f.motion_state == state_run_reverse ? slot_walk_back : slot_walk;
                motion_set_animation(f, data, clip, state_walk, motion_loop,
                                     data.legs[f.leg_class].timing[clip].loop_start);
                return;
            }
            f.motion_time += fx(1);
        } else if (f.motion_time > Fx{}) {
            f.motion_time -= fx(1);
        }
        return;
    }
    if (f.motion_state == state_idle || f.motion_state == state_skid) {
        if (forward > stick_dead) {
            motion_set_animation(f, data, slot_walk, state_walk, motion_lead_in, Fx{});
            f.reverse = false;
        } else if (forward < -stick_dead) {
            motion_set_animation(f, data, slot_walk_back, state_walk_back, motion_lead_in, Fx{});
            f.reverse = true;
        }
    }
}

// a press on the ground starts the jump clip; from a run it starts at the
// clip's loop_start and so leaves at once (no crouch)
// decomp: actor_try_jump 0x8D77A
bool fighter_try_jump(Fighter& f, const BattleData& data) {
    if (!(f.control.control_buttons & control_jump_pressed) ||
        !state_in(f.motion_state, {state_idle, state_skid, state_turn, state_walk,
                                   state_walk_back, state_run, state_run_reverse,
                                   state_strafe_turn})) {
        return false;
    }
    const bool running = f.motion_state == state_run || f.motion_state == state_run_reverse;
    const Fx phase = running ? data.legs[f.leg_class].timing[slot_jump].loop_start : Fx{};
    motion_set_animation(f, data, slot_jump, state_jump, motion_lead_in, phase);
    f.takeoff_velocity = f.velocity_prev;
    f.jump_cut = false;
    f.airborne = true;
    return true;
}

// Standard only
// decomp: actor_strafe_turn_start 0x8DBEA
void fighter_strafe_turn_start(Fighter& f, const BattleData& data) {
    const Fx twist = axis(f, axis_twist);
    if (twist >= stick_dead) {
        motion_set_animation(f, data, slot_turn_right, state_strafe_turn, motion_lead_in, Fx{});
    } else if (twist <= -stick_dead) {
        motion_set_animation(f, data, slot_turn_left, state_strafe_turn, motion_lead_in, Fx{});
    }
}

// decomp: sv_grounded_state_update 0x88DD3
void grounded_state_update(Fighter& f, const BattleData& data) {
    if (state_in(f.motion_state, {state_dead, state_dodge, state_stagger, state_melee})) {
        return;
    }
    if (f.grounded) {
        f.airborne_time = Fx{};
        f.dodge_ready = true;
        f.dive_used = false;
        f.airborne = false;
        fighter_ground_input(f, data);
        fighter_try_jump(f, data);
        if ((f.control_type & control_type_standard) &&
            (f.motion_state == state_idle || f.motion_state == state_turn)) {
            fighter_strafe_turn_start(f, data);
        }
    } else {
        f.airborne = true;
        f.airborne_time += fx(1);
        if (f.airborne_time > fall_after_ticks &&
            !state_in(f.motion_state, {state_jump, state_fall, state_dodge, state_stagger})) {
            motion_set_animation(f, data, slot_fall, state_fall, motion_loop,
                                 data.legs[f.leg_class].timing[slot_fall].loop_start);
            f.takeoff_velocity = f.velocity_prev;
        }
    }
    fighter_try_dodge(f, data);
}

// walking, forward or back
// decomp: sv_motion_state_0_1 0x8E636
void motion_state_walk(Fighter& f, const BattleData& data) {
    const Fx forward = axis(f, axis_forward);
    f.position_prev = f.position;
    f.motion_phase_prev = f.motion_phase;
    fighter_turn_by_stick(f);
    if (f.motion_mode == motion_lead_in || f.motion_mode == motion_loop) {
        const Fx lean = fx_max(fx_min(fx_abs(forward), fx(1)), stick_walk_floor);
        f.motion_phase += f.walk_root_motion ? fx_mul_near(f.walk_rate, lean) : lean;
        if (f.motion_phase >= f.timing.loop_start) {
            f.motion_mode = motion_loop;
            f.at_speed = true;
        }
        wrap_loop(f);
    } else {
        f.motion_phase += f.walk_root_motion ? f.walk_rate : fx(1);
        if (f.motion_phase >= f.timing.end) {
            motion_set_animation(f, data, slot_idle, state_idle, motion_whole, Fx{});
            return;
        }
    }
    if (f.walk_root_motion) {
        set_ground_velocity(f, motion_root_delta(f, data));
        return;
    }
    // forward is -z in the original's body, +z mirrored
    const Fx speed = forward < Fx{} ? -f.walk_rate : f.walk_rate;
    set_ground_velocity(f, {Fx{}, Fx{}, ramped(f, speed)});
}

// running, forward or reversed
// decomp: sv_motion_state_2_3 0x8F25A
void motion_state_run(Fighter& f, const BattleData& data) {
    f.position_prev = f.position;
    f.motion_phase_prev = f.motion_phase;
    fighter_turn_by_stick(f);
    if (f.motion_mode == motion_lead_in || f.motion_mode == motion_loop) {
        f.motion_phase += f.run_root_motion ? f.run_rate : fx(1);
        fighter_run_clip_select(f, data);
        if (f.motion_phase >= f.timing.loop_start) {
            f.motion_mode = motion_loop;
            f.at_speed = true;
        }
        wrap_loop(f);
    } else if (f.motion_mode == motion_lead_out) {
        if (f.at_speed) {
            motion_set_animation(f, data, slot_skid, state_skid, motion_lead_in, Fx{});
        } else {
            motion_set_animation(f, data, slot_idle, state_idle, motion_whole, Fx{});
        }
        return;
    }
    if (f.run_root_motion) {
        set_ground_velocity(f, motion_root_delta(f, data));
        return;
    }
    const Fx speed = f.reverse ? -f.run_rate : f.run_rate;
    set_ground_velocity(f, {Fx{}, Fx{}, ramped(f, speed)});
}

// standing; turning in place starts the turn clip
// decomp: sv_motion_state_11 0x8E400
void motion_state_idle(Fighter& f, const BattleData& data) {
    const Angle before = f.heading;
    f.motion_phase_prev = f.motion_phase;
    fighter_turn_by_stick(f);
    if (f.at_speed) {
        motion_set_animation(f, data, slot_skid, state_skid, motion_lead_in, Fx{});
    } else if (f.camera.auto_turning || f.heading != before) {
        // the original's heading rose (a right turn) is the port's falling
        const bool right = angle_signed(f.heading - before) <= 0;
        motion_set_animation(f, data, right ? slot_turn_right : slot_turn_left, state_turn,
                             motion_lead_in, Fx{});
    }
    f.motion_phase += fx(1);
    if (f.motion_phase >= f.timing.end) {
        f.motion_phase = Fx{};
        f.motion_phase_prev = Fx{};
        f.motion_mode = motion_whole;
    }
    // the idle bark's clock, while nothing is held on the fighter's pad (func_000DF1C0)
    if (!f.pad_held) {
        f.motion_time += fx(1);
        if (f.motion_time >= idle_bark_ticks) {
            f.motion_time = Fx{};
            fighter_bark(f, bark_idle);
        }
    }
    set_ground_velocity(f, motion_root_delta(f, data));
}

// turning on the spot
// decomp: sv_motion_state_12 0x8E022
void motion_state_turn(Fighter& f, const BattleData& data) {
    const Angle before = f.heading;
    f.motion_phase_prev = f.motion_phase;
    fighter_turn_by_stick(f);
    if (axis(f, axis_forward) > stick_dead) {
        motion_set_animation(f, data, slot_walk, state_walk, motion_lead_in, Fx{});
        f.reverse = false;
        return;
    }
    if (axis(f, axis_forward) < -stick_dead) {
        motion_set_animation(f, data, slot_walk_back, state_walk_back, motion_lead_in, Fx{});
        f.reverse = true;
        return;
    }
    if (f.motion_mode == motion_lead_in || f.motion_mode == motion_loop) {
        if (!f.camera.auto_turning && f.heading == before) {
            motion_set_animation(f, data, slot_idle, state_idle, motion_whole, Fx{});
            return;
        }
        f.motion_phase += f.camera.auto_turning
                              ? turn_clip_rate
                              : fx_mul_near(fx_abs(axis(f, axis_leg_turn)), turn_clip_rate);
        wrap_loop(f);
    }
}

// the skid after a run
// decomp: sv_motion_state_13 0x8E336
void motion_state_skid(Fighter& f, const BattleData& data) {
    f.position_prev = f.position;
    f.motion_phase_prev = f.motion_phase;
    fighter_slide_step(f);
    f.motion_phase += fx(1);
    wrap_loop(f);
    if (!f.at_speed) {
        motion_set_animation(f, data, slot_idle, state_idle, motion_whole, Fx{});
    }
}

// the strafe turn (Standard)
// decomp: sv_motion_state_14 0x8F78E
void motion_state_strafe_turn(Fighter& f, const BattleData& data) {
    f.position_prev = f.position;
    f.motion_phase_prev = f.motion_phase;
    fighter_turn_by_stick(f);
    if (axis(f, axis_forward) > stick_dead) {
        motion_set_animation(f, data, slot_walk, state_walk, motion_lead_in, Fx{});
        f.reverse = false;
        return;
    }
    if (axis(f, axis_forward) < -stick_dead) {
        motion_set_animation(f, data, slot_walk_back, state_walk_back, motion_lead_in, Fx{});
        f.reverse = true;
        return;
    }
    if (f.motion_mode == motion_lead_in || f.motion_mode == motion_loop) {
        f.motion_phase += fx_abs(axis(f, axis_twist));
        wrap_loop(f);
        if (fx_abs(axis(f, axis_twist)) < stick_dead) {
            motion_set_animation(f, data, slot_idle, state_idle, motion_whole, Fx{});
        }
    }
}

// actor_strafe_step (Standard): the walk rate sideways, added to the rebuilt ground
// velocity (an assignment's part, not a force: outside R10's dv)
// decomp: actor_strafe_step 0x8DE5E
void fighter_strafe_step(Fighter& f) {
    if (!(f.control_type & control_type_standard) || !f.grounded) {
        return;
    }
    const Fx twist = fx_clamp(axis(f, axis_twist), fx(-1), fx(1));
    if (fx_abs(twist) < stick_dead) {
        return;
    }
    if (!state_in(f.motion_state,
                  {state_jump, state_fall, state_dodge, state_stagger, state_dead, state_melee})) {
        // actor_walk_rate 0x763E6: the root-motion walk rate back in units a tick
        const Fx walk = f.walk_root_motion ? fx_mul_near(f.walk_rate, f.walk_scale) : f.walk_rate;
        f.velocity += turned_by({-fx_mul_near(twist, walk), Fx{}, Fx{}}, f.heading);
    }
    f.skid_velocity = {};
    f.at_speed = false;
}

} // namespace

// decomp: motion_set_animation 0x8DD32
void motion_set_animation(Fighter& f, const BattleData& data, int clip, int state, int mode,
                          Fx phase) {
    f.timing = data.legs[f.leg_class].timing[clip];
    f.motion_state = state;
    f.motion_mode = mode;
    f.motion_slot_prev = f.motion_slot_a;
    f.motion_slot_a = clip;
    f.velocity = f.velocity_prev;
    f.motion_phase_prev = phase;
    f.motion_phase = phase;
    f.motion_time = Fx{};
    f.pose_blend_steps = 0;
}

// decomp: actor_turn_by_stick 0x8E1D7
void fighter_turn_by_stick(Fighter& f) {
    // the stick clamped to 1
    const Fx stick = fx_clamp(axis(f, axis_leg_turn), fx(-1), fx(1));
    f.heading_prev = f.heading;
    f.heading -= angle_scaled(f.turn_step, stick);
}

void motion_dispatch(Fighter& f, const BattleData& data, std::int64_t step) {
    grounded_state_update(f, data);
    switch (f.motion_state) {
    case state_idle:
        motion_state_idle(f, data);
        break;
    case state_turn:
        motion_state_turn(f, data);
        break;
    case state_skid:
        motion_state_skid(f, data);
        break;
    case state_walk:
    case state_walk_back:
        motion_state_walk(f, data);
        break;
    case state_jump:
        motion_state_jump(f, data, step);
        break;
    case state_fall:
        motion_state_fall(f, data, step);
        break;
    case state_dodge:
        motion_state_dodge(f, data, step);
        break;
    case state_run:
    case state_run_reverse:
        motion_state_run(f, data);
        break;
    case state_strafe_turn:
        motion_state_strafe_turn(f, data);
        break;
    case state_stagger:
        motion_state_stagger(f, data);
        break;
    case state_melee:
        motion_state_melee(f, data);
        break;
    case state_dead:
        motion_state_dead(f, data);
        break;
    default:
        break;
    }
    fighter_strafe_step(f);
    // on the ground the velocity is laid along the floor under the feet, its flat length kept
    if (f.grounded) {
        FxVec3 normal;
        if (collision_segment_normal_mid(data.stage, f.position + FxVec3{Fx{}, fx(1), Fx{}},
                                         f.position - FxVec3{Fx{}, fx(1), Fx{}}, &normal)) {
            const FxVec3 flat_v{f.velocity.x, Fx{}, f.velocity.z};
            const FxVec3 along = cross(normal, cross(flat_v, normal));
            FxVec3 out = scaled_to(along, length(flat_v));
            out.y += f.velocity.y;
            f.velocity = out;
        }
    }
}
