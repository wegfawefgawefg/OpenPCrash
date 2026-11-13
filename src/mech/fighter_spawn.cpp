// A fighter made from a build (fighter_spawn, actor_init, the performance pass
// chassis_apply_performance) and its step (fighter_task_update, actor_step).
// docs/port/parts.md, "Legs and the performance pass".
#include "fx/mat.hpp"
#include "mech/fighter.hpp"
#include "mech/fighter_rates.hpp"
#include "weapons/weapons.hpp"

namespace {

// the leg classes by kind (parts.md)
// decomp: leg_kind_group 0x8234B
int leg_kind_group(int leg_class) {
    switch (leg_class) {
    case 0:
    case 5:
    case 11:
        return 0; // two legs
    case 1:
    case 6:
    case 12:
    case 17:
        return 1; // four legs
    case 7:
        return 2; // six legs
    case 2:
    case 13:
        return 3; // two rollers
    case 8:
    case 20:
        return 4; // three rollers
    case 3:
    case 14:
        return 5; // four rollers
    case 9:
    case 15:
        return 6; // six rollers
    default:
        return 7; // hovers: 4, 10, 16, 18, 19
    }
}

template <typename T> const T* record_at(const std::vector<T>& rows, int row) {
    if (row == -1 || rows.empty()) {
        return nullptr;
    }
    return row < 0 || row >= static_cast<int>(rows.size()) ? &rows[0] : &rows[row];
}

// chassis_weight_baseline_selected / _fixed: sqrt(base / weight), less up to 10%
// for the leg tune, and over capacity the cliff (capacity / weight)^10
// decomp: chassis_weight_baseline_selected 0x7C0C8
Fx weight_factor(Fx base, Fx weight, Fx capacity, int leg_tune) {
    if (weight == Fx{}) {
        return fx(1);
    }
    Fx factor = fx_mul_near(fx_sqrt(fx_div_near(base, weight)), fx(1) - fx_ratio(leg_tune, 990));
    if (capacity < weight) {
        factor = fx_mul_near(fx_pow_int(fx_div_near(capacity, weight), 10), factor);
    }
    return factor;
}

// chassis_apply_performance with chassis_leg_bounded_values. Every rate is per
// tick and stays as it is at 60 Hz (sixty.md R9); the jump's are per update and kept so
// decomp: chassis_apply_performance 0x759B6, chassis_leg_bounded_values 0x7C2CE
void apply_performance(Fighter& f, const BattleData& data, const BuildRecord& build,
                       const LegRecord& leg) {
    const PartRecords& parts = data.parts;
    const int tune = build.leg_tune;
    f.weight = build_weight(parts, build);
    const Fx capacity = curve_value(leg.capacity, tune);
    const Fx base = build_base_weight(parts, build);
    // the jump's base is body row 0 and leg row 0 (HLBody, HL2Leg) at tune 0
    const Fx base_fixed =
        curve_value(parts.bodies[0].weight, 0) + curve_value(parts.legs[0].weight, 0);
    const Fx selected = weight_factor(base, f.weight, capacity, tune);
    const Fx fixed = weight_factor(base_fixed, f.weight, capacity, tune);

    const Fx speed = fx_mul_near(curve_value(leg.speed, tune), selected);
    const Fx walk =
        fx_clamp(fx_raw(static_cast<std::int32_t>(div_round(speed.raw, 2))), fx(10), fx(1000));
    const Fx run = fx_clamp(speed, fx(20), fx(2000));
    f.jump_height = fx_clamp(fx_mul_near(curve_value(leg.jump, tune), fixed), fx_f(2.5), fx(100));
    const Fx turn_deg =
        fx_clamp(fx_mul_near(curve_value(leg.turn_deg, tune), selected), fx_f(0.1), fx(90));
    f.dodge_speed_ground =
        fx_clamp(fx_mul_near(curve_value(leg.dodge_ground, tune), selected), fx_f(0.1), fx(30));
    f.dodge_speed_air =
        fx_clamp(fx_mul_near(curve_value(leg.dodge_air, tune), selected), fx_f(0.1), fx(30));

    // km/h to units a tick: / 216 (x 0.0046296297); with root motion, the clip's playback rate
    const LegClassData& leg_class = data.legs[f.leg_class];
    f.walk_rate = fx_div_near(walk, fx(216));
    f.run_rate = fx_div_near(run, fx(216));
    f.walk_scale = leg_class.walk_root_scale;
    f.walk_root_motion = leg_class.walk_root_scale != Fx{};
    f.run_root_motion = leg_class.run_root_scale != Fx{};
    if (f.walk_root_motion) {
        f.walk_rate = fx_div_near(f.walk_rate, leg_class.walk_root_scale);
    }
    if (f.run_root_motion) {
        f.run_rate = fx_div_near(f.run_rate, leg_class.run_root_scale);
    }
    // the melee lunge's cap: run_rate x 2 x ticks an update (1.4 for an AI); R8: per step half
    f.lunge_speed_max = fx_mul_near(f.run_rate, f.controller != -1 ? fx(2) : fx_f(1.4));

    // jump_speed = sqrt(2 h g) x 0.4 + g and jump_thrust = h x 0.84 / jump_speed x g with retail's
    // g30; the step's jump_speed is half of it, not re-derived with g60 (sixty.md "Jump")
    f.jump_speed_30 =
        fx_mul_near(fx_sqrt(fx_mul_near(f.jump_height * 2, gravity_30)), fx_f(0.4)) + gravity_30;
    f.jump_thrust_30 = fx_mul_near(
        fx_div_near(fx_mul_near(f.jump_height, fx_f(0.84)), f.jump_speed_30), gravity_30);
    f.jump_speed = fx_raw(static_cast<std::int32_t>(div_round(f.jump_speed_30.raw, 2)));
    f.turn_step = angle_from_degrees(turn_deg);
    f.air_turn_step = f.turn_step;

    f.leg_landing = curve_value(leg.landing, tune);
    f.leg_knockback_resist = curve_value(leg.knockback_resist, tune);
    // the skid's x leg_bc an update (actor_slide_step); R13: r = sqrt(leg_bc) a step, the
    // start value scaled by 2r / (1 + r) so each pair moves an update's worth, the stop with it
    f.slide_decay = fx_sqrt(leg.slide_decay);
    f.skid_start = fx_div_near(f.slide_decay * 2, fx(1) + f.slide_decay);
    f.skid_stop = fx_mul_near(skid_stop_30, f.skid_start) / 2;
}

} // namespace

FxVec3 turned_by(FxVec3 local, Angle heading) {
    return transform(local, mat3_rotation_y(heading));
}

// decomp: fighter_spawn 0x806F8, actor_init 0x80DC1
void fighter_spawn(Fighter& fighter, const BattleData& data, int slot, int controller, int build,
                   const EntryPoint& entry) {
    const PartRecords& parts = data.parts;
    const BuildRecord& row =
        parts.builds[build < 0 || build >= static_cast<int>(parts.builds.size()) ? 0 : build];
    fighter_spawn_record(fighter, data, slot, controller, row, build, entry);
}

void fighter_spawn_record(Fighter& fighter, const BattleData& data, int slot, int controller,
                          const BuildRecord& row, int build, const EntryPoint& entry) {
    Fighter& f = fighter;
    f = Fighter{};
    f.present = true;
    f.slot = slot;
    f.controller = controller;
    f.build = build;
    // the default button config and control type 0: the AI's. A human's are its seat's, which
    // battle_spawn sets after this (actor_init reads the seat's PlayerConfig)
    std::array<int, 8> keys{};
    for (int k = 0; k < 8; ++k) {
        keys[k] = default_key_block[k];
    }
    f.bindings = control_bindings_from_keys(keys);
    f.control_type = 0;

    const PartRecords& parts = data.parts;
    const LegRecord* leg = record_at(parts.legs, row.leg);
    const BodyRecord* body = record_at(parts.bodies, row.body);
    f.leg_class = leg && leg->geometry >= 0 && leg->geometry < leg_class_count ? leg->geometry : 0;
    f.body_class = body ? body->body_class : 0;
    f.leg_kind = leg_kind_group(f.leg_class);
    f.radius = data.legs[f.leg_class].radius;
    if (leg) {
        apply_performance(f, data, row, *leg);
    }

    // actor_init: standing at the entry point, idle, not yet on the ground
    f.position = entry.position;
    f.position_prev = entry.position;
    f.heading = entry.heading;
    f.motion_state = state_idle;
    f.motion_slot_a = slot_idle;
    f.timing = data.legs[f.leg_class].timing[slot_idle];
    // camera_init: the view on the heading, the eye at the head (position + 1 up)
    f.camera.view_yaw = f.heading;
    f.camera.eye = f.position + FxVec3{Fx{}, fx(1), Fx{}};
    f.camera.target = f.camera.eye;
    // the weapons and damage numbers (fighter_weapons_spawn and the rest, src/weapons)
    combat_spawn(f, data, row);
    // the pose (fighter_assets_load poses it once), so the first step has its head and mounts
    fighter_pose_init(f, data);
}

// decomp: fighter_task_update 0x7546B, actor_step 0x756F3
void fighter_step(Fighter& f, const BattleData& data, const PadRecord& pad, const MouseAim& aim,
                  std::int64_t step, bool blocked, StageWorld* pickups, bool stand,
                  bool single_view) {
    // input: a human's pad through the route (the AI and replays will write the record directly)
    if (f.controller >= 0) {
        control_route(f.control, pad, aim, f.bindings, {f.control_type, f.airborne, blocked},
                      f.flick);
    }
    f.pad_held = f.controller >= 0 && (pad.b.cur != 0 || pad.stick[0].dirs.cur != 0 ||
                                       pad.stick[1].dirs.cur != 0 || aim.twist != Fx{} ||
                                       aim.pitch != Fx{});

    // free play: actor_input_restrict, before anything reads the record
    if (f.restrict_input) {
        for (int k = 0; k < 4; ++k) {
            if (f.axis_zero[k]) {
                f.control.control_axis[k] = Fx{};
            }
        }
        f.control.control_buttons &= f.control_allow;
    }

    // actor_step's order: camera input and update, triggers, frame reset, the motion state
    // machine, the phases, the turn rate, integrate and collide, the pose
    camera_view_input(f, single_view);
    fighter_camera_input(f);
    fighter_camera_update(f, data, pickups);
    fighter_triggers_update(f);
    // actor_frame_reset
    f.motion_slot_prev = -1;
    f.skidding = false;
    f.landed = false;
    f.jump_step = Fx{};
    f.force_dv = {};
    f.motion_events = 0; // the view has read the last step's
    // a lesson's one-way rule on an axis, from its scene's update (battle_tutorial.cpp):
    // 1 forward held to 0.7 either way, 2 the legs turn only one way, 3 only the other, 4 the twist
    // only one way, 5 only the other
    Fx* axes = f.control.control_axis;
    switch (f.tutorial_clamp) {
    case 1:
        axes[axis_forward] = axes[axis_forward] < fx_f(-0.7)  ? fx_f(-0.7)
                             : axes[axis_forward] > fx_f(0.7) ? fx_f(0.7)
                                                               : axes[axis_forward];
        break;
    case 2:
        if (axes[axis_leg_turn] <= Fx{}) {
            axes[axis_leg_turn] = Fx{};
        }
        break;
    case 3:
        if (axes[axis_leg_turn] >= Fx{}) {
            axes[axis_leg_turn] = Fx{};
        }
        break;
    case 4:
        if (axes[axis_twist] <= Fx{}) {
            axes[axis_twist] = Fx{};
        }
        break;
    case 5:
        if (axes[axis_twist] >= Fx{}) {
            axes[axis_twist] = Fx{};
        }
        break;
    default:
        break;
    }
    f.tutorial_clamp = 0;
    if (!f.scene_motion) {
        motion_dispatch(f, data, step);
    }
    fighter_phases_update(f);
    // actor_turn_rate_update: 1/8 toward pi/8 x stick running, pi/16 x stick walking,
    // jumping, falling or turning, 0 otherwise; R7: 0.064586 / 0.935414. The pose's lean only
    if (f.motion_state != state_dead) {
        Fx target{};
        const Fx stick = f.control.control_axis[axis_leg_turn];
        if (f.motion_state == state_run || f.motion_state == state_run_reverse) {
            target = fx_mul_near(fx_f(0.39269909), stick); // D_001B5AF0
        } else if (f.motion_state == state_walk || f.motion_state == state_walk_back ||
                   f.motion_state == state_jump || f.motion_state == state_fall ||
                   f.motion_state == state_turn) {
            target = fx_mul_near(fx_f(0.19634955), stick); // D_001B5B4C
        }
        f.turn_rate =
            fx_mul_near(target, fx_f(0.064585994)) + fx_mul_near(f.turn_rate, fx_f(0.935414006));
    }
    contact_route(f, data, pickups);
    // actor_pose_build, the back weapons aimed (actor_back_weapons_place: launch_pitch), the arm
    // weapons on their mounts, the wheels, and the attachment points
    fighter_pose_step(f, data, stand);
    // actor_settle_damage (which counts intangibility down) follows in step_world: it
    // reads the other fighters (src/weapons/weapons.hpp)
    f.age_frames += 1;
}
