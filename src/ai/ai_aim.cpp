// Aiming, steering and evasion (docs/port/ai.md "Aiming", "Evasion"): ai_aim_at_target,
// ai_aim_at_offset, ai_steer_to_point, ai_evade_buttons and
// ai_dodge_shots. No aim error, no lead: the turn and pitch rates are the limit.
//
// Angles and body-space vectors are the original's: the port's frame has z and the angles about
// y and x negated (mech/fighter.hpp), and the axes mean the same in both, so the decomp's
// arithmetic runs on the original's numbers.
#include "ai/ai_internal.hpp"
#include "ai/ai_links.hpp"
#include "fx/mat.hpp"

namespace {

constexpr Fx half_pi = fx_f(1.5707964);
constexpr Fx pi = fx_f(3.1415927);
// actor_camera_input's pitch, 3 degrees an update at full stick
constexpr Fx pitch_rate = fx_f(0.05235988);
// a shot is dodged when it's within 8.5 updates of travel plus 1.5
constexpr Fx dodge_lead = fx_f(8.5);
constexpr Fx dodge_margin = fx_f(1.5);

// one update of full-stick turning: (video_rate_nominal / cadence) x turn_deg x pi / 180
Fx turn_per_update(const Ai& ai) {
    return angle_radians(ai.f.turn_step) * 2;
}

Fx toward(Fx y) {
    return y > Fx{} ? fx(-1) : fx(1);
}

} // namespace

FxVec3 ai_look_direction(const Ai& ai) {
    return normalized(ai.f.reticle_center_world - ai.f.position);
}

// decomp: ai_aim_at_target 0x5C17B
void ai_aim_at_target(Ai& ai, int allow) {
    AiController& c = ai.c;
    const HudSense* sense = ai_target_sense(ai);
    if (c.target < 0 || !sense) {
        return;
    }
    const Fx y = sense->yaw;
    const Fx size = fx_abs(y);
    const Fx leg_before = c.axis_leg_turn;
    const Fx twist_before = c.axis_twist;
    if (size >= half_pi) {
        c.axis_twist = c.axis_leg_turn = toward(y);
        press(ai, control_recentre);
    } else {
        const Fx dp = sense->pitch - camera_pitch_original(ai);
        const Fx r = turn_per_update(ai);
        if (size <= r) {
            // closes the yaw this update with the legs
            c.axis_leg_turn = fx_div_near(-y, r);
        } else if (r + r >= size) {
            // the twist takes the rest (over 1, clamped at the output), the legs turn full
            c.axis_twist = fx_div_near(-y, r);
            c.axis_leg_turn = y < Fx{} ? fx(1) : fx(-1);
        } else {
            c.axis_twist = c.axis_leg_turn = toward(y);
            press(ai, control_recentre);
        }
        if (!(allow & aim_hold_pitch)) {
            if (fx_abs(dp) <= pitch_rate) {
                c.axis_pitch = fx_div_near(-dp, pitch_rate);
            } else {
                c.axis_pitch = dp < Fx{} ? fx(1) : fx(-1);
            }
        }
    }
    if (!(allow & aim_twist)) {
        c.axis_twist = twist_before;
    }
    if (!(allow & aim_legs)) {
        c.axis_leg_turn = leg_before;
    }
}

// decomp: ai_aim_at_offset 0x5C409
void ai_aim_at_offset(Ai& ai) {
    AiController& c = ai.c;
    const HudSense* sense = ai_target_sense(ai);
    if (c.target < 0 || !sense) {
        return;
    }
    // facing directly away: the target's yaw less pi
    const Fx x = fx_clamp(sense->yaw - pi, -pi, pi);
    const Fx size = fx_abs(x);
    if (size >= half_pi) {
        c.axis_twist = c.axis_leg_turn = toward(x);
    } else {
        const Fx r = turn_per_update(ai);
        if (size <= r) {
            c.axis_leg_turn = fx_div_near(-x, r);
        } else if (r + r >= size) {
            c.axis_leg_turn = x < Fx{} ? fx(1) : fx(-1);
            c.axis_twist = fx_div_near(-x, r);
        } else {
            c.axis_twist = c.axis_leg_turn = toward(x);
            press(ai, control_recentre);
        }
    }
    press(ai, control_recentre);
}

// decomp: ai_steer_to_point 0x5C082
int ai_steer_to_point(Ai& ai, FxVec3 point) {
    AiController& c = ai.c;
    // the point from the fighter, in its body frame (the original's: x, and z ahead positive)
    const FxVec3 body = transform(ai.f.position - point, mat3_rotation_y(-ai.f.heading));
    const Fx x = body.x;
    const Fx z = -body.z;
    const Fx size = length(FxVec3{x, Fx{}, z});
    if (size <= fx_f(0.01)) {
        c.axis_leg_turn = Fx{};
        c.axis_forward = Fx{};
        return 0;
    }
    int result = 1;
    if (z < Fx{}) {
        // behind: turn full, and the forward below backs up while turning
        c.axis_leg_turn = x < Fx{} ? fx(-1) : fx(1);
        result = 2;
    } else {
        c.axis_leg_turn = fx_div_near(x, size);
    }
    c.axis_forward = fx_div_near(z, size);
    return result;
}

// decomp: ai_evade_buttons 0x5CD5D
void ai_evade_buttons(Ai& ai) {
    AiController& c = ai.c;
    if (!motion_jumping(ai) && !motion_falling(ai)) {
        press(ai, control_jump_held);
        if (ai.f.dodge_ready && ai_roll(ai, c.band_dodge_chance, 0xFF)) {
            // (int)(r / 32767 x 2) % 1 is always 0: always the left step, 0x20 (kept)
            ai_draw(ai);
            press(ai, control_step_left);
        } else if (ai_roll(ai, c.band_jump_chance, 0xFF)) {
            press(ai, control_jump_pressed);
        }
        return;
    }
    if (ai_roll(ai, c.band_dodge_chance, 0xFF)) {
        ai_draw(ai);
        press(ai, control_step_left);
        return;
    }
    if (ai_roll(ai, c.band_jump_chance, 0xFF)) {
        press(ai, control_jump_held | control_jump_pressed);
        return;
    }
    if (ai_roll(ai, c.band_button1_chance, 0xFF)) {
        press(ai, control_dash);
    }
}

// decomp: ai_dodge_shots 0x5D4B0
void ai_dodge_shots(Ai& ai) {
    AiController& c = ai.c;
    ShotThreats threats;
    c.use_camo = false;
    if (!world_shot_threats(ai.world, ai.slot, &threats)) {
        return;
    }
    const bool all_round = c.enraged || (c.move_flags & move_ranker_hold);
    const int level = ai_level(ai);
    const int cone_degrees = level + 15 > 60 ? 60 : level + 15;
    const Fx cone_cos = fx_cos(angle_from_degrees(fx(cone_degrees)));
    bool dodge = false;
    if (c.dodge_cooldown > 0) {
        --c.dodge_cooldown;
        return;
    }
    const FxVec3 aim = ai_look_direction(ai);
    // a shot coming at this fighter from in front, close enough for its speed: chance
    // 2 level x distance / 5, 245 all round
    const auto rolled = [&](const ThreatShot& shot, bool toward_test_always) {
        const FxVec3 dir = normalized(shot.velocity);
        const Fx speed = length(shot.velocity) * 2; // per tick to per update
        const FxVec3 offset = ai.f.position - shot.pos;
        const Fx distance = length(offset);
        const FxVec3 to_me = normalized(offset);
        if (dot(dir, to_me) < Fx{} && (toward_test_always || !all_round)) {
            return false;
        }
        if (!all_round && -cone_cos < dot(aim, to_me)) {
            return false;
        }
        if (fx_mul_near(speed, dodge_lead) + dodge_margin < distance) {
            return false;
        }
        int chance = static_cast<std::int16_t>(
            fx_mul_near(fx(level * 2), fx_div_near(distance, fx(5))).raw >> 16);
        if (all_round) {
            chance = 0xF5;
        }
        return chance > 0 && ai_roll(ai, chance, 0xFF);
    };
    for (int k = 0; k < threats.locked_on_me_count && !dodge; ++k) {
        if (c.camo_when_shot) {
            c.use_camo = true;
        }
        dodge = rolled(threats.locked_on_me[static_cast<std::size_t>(k)], false);
    }
    for (int k = 0; k < threats.nearby_count && !dodge; ++k) {
        const ThreatShot& shot = threats.nearby[static_cast<std::size_t>(k)];
        if (!(shot.dodge_rolled & (1 << ai.slot))) {
            dodge = rolled(shot, true);
        }
    }
    for (int k = 0; k < threats.melee_count && !dodge; ++k) {
        const ThreatShot& blade = threats.melee[static_cast<std::size_t>(k)];
        if (blade.dodge_rolled & (1 << ai.slot)) {
            continue;
        }
        // each AI considers each swing once
        shot_mark_dodge_rolled(ai.world, blade.shot, ai.slot);
        const FxVec3 dir = normalized(blade.pos - blade.prev_pos);
        const FxVec3 to_me = normalized(ai.f.position - blade.pos);
        if (dot(dir, to_me) < Fx{}) {
            continue;
        }
        if (!all_round && -cone_cos < dot(aim, to_me)) {
            continue;
        }
        const int chance = all_round ? 0xF5 : static_cast<std::int16_t>(level << 1);
        dodge = chance > 0 && ai_roll(ai, chance, 0xFF);
    }
    // beams, every 15th update: those whose firer has this fighter under its reticle
    if (c.age_frames % 15 == 0) {
        for (int k = 0; k < threats.beam_count && !dodge; ++k) {
            const ThreatShot& beam = threats.beams[static_cast<std::size_t>(k)];
            if (beam.owner < 0 || !ai.hud.reticle_on_me[static_cast<std::size_t>(beam.owner)]) {
                continue;
            }
            const FxVec3 offset =
                ai.f.position - ai.world.fighters[static_cast<std::size_t>(beam.owner)].position;
            const Fx distance = length(offset);
            if (!all_round && -cone_cos < dot(aim, normalized(offset))) {
                continue;
            }
            const int chance =
                distance > fx(30)
                    ? level + 0x3C
                    : static_cast<std::int16_t>(
                          (fx(level) + fx_mul_near(fx_div_near(distance, fx(30)), fx(60))).raw >>
                          16);
            dodge = chance > 0 && ai_roll(ai, chance, 0xFF);
        }
    }
    if (dodge) {
        if (ai_draw(ai) & 1) {
            press(ai, control_step_left);
            release(ai, control_step_right);
        } else {
            release(ai, control_step_left);
            press(ai, control_step_right);
        }
    }
}
