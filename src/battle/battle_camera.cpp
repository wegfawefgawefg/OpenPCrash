// The battle camera of a fighter (decomp src/od/u_07d2b0.c): the stick's pitch and the upper
// body's twist (actor_camera_input), the view following them (camera_yaw_follow,
// camera_pitch_follow), then the view's eye and target (camera_views.cpp: the six views
// and the d-pad's switch). It is in the world because the camera is the aim: shots go to the
// point under the reticle.
#include "mech/fighter.hpp"
#include "mech/fighter_rates.hpp"

#include "fx/mat.hpp"

#include <cstdlib>

namespace {

// recentre: recenter_frames 8, -1 an update, pitch and aim_yaw = start x frames / 8; R4: 16 steps,
// x frames / 16 (sixty.md "Camera and aim")
constexpr int recentre_steps = 16;
// pitch_stick = 0.1 |stick| + 0.9 pitch_stick an update; R7: 0.051317 / 0.948683
constexpr Fx pitch_stick_take = fx_f(0.0513167019494862);
constexpr Fx pitch_stick_keep = fx_f(0.9486832980505138);
// the pitch limit, pi/4 + pitch_stick x pi/8 (0.7853982, 0.3926991): none of it per update
constexpr Fx pitch_limit_base = fx_f(0.7853982);
constexpr Fx pitch_limit_stick = fx_f(0.3926991);
// pitch -= stick x 0.05235988 (3 degrees) an update; R5: half a step. The input layer's mouse aim
// turns its angle into axis units with the same constant (docs/input.md)
constexpr Angle pitch_step = angle_f_radians(0.05235988 / 2.0);
// aim_yaw within +-pi/4 of the legs; the view's pitch follows a pitch held within +-pi/4
constexpr Angle quarter_pi = angle_f_radians(0.7853982);
// view yaw and pitch close 0.3 of the gap an update; R7: 1 - sqrt(0.7)
constexpr Fx view_follow = fx_f(0.16333997346592444);
// beginner mode's pitch springs: pitch = 0.94 pitch + 0.06 target an update; R7: sqrt(0.94) kept.
// The 0.06 radians it snaps the target within stays a distance
constexpr Fx beginner_pitch_keep = fx_f(0.9695359714832659);
constexpr Fx beginner_pitch_take = fx_f(1.0 - 0.9695359714832659);
constexpr Fx beginner_pitch_near = fx_f(0.06);
constexpr Fx beginner_pitch_most = fx_f(0.62831855); // pi/5
// the quick turn: a quarter turn queued past half stick, turned at 2 x turn_deg x 0.8 an update
// (R5: turn_step x 0.8 a step), the opposite stick locking it out 8 updates (R4: 16 steps)
constexpr Fx quick_turn_stick = fx_f(0.5);
constexpr Fx quick_turn_rate = fx_f(0.8);
constexpr int quick_turn_lock_steps = 16;

// actor_camera_input's beginner mode (Actor.beginner_mode): the pitch springs back to level when
// the stick lets go, a reversal recentres it, and in Preferred controls a flick of the twist
// stick turns the legs a quarter turn. Retail's pitch target is -axis; the port's pitch is the
// mirrored one, so its target is +axis and every test below is retail's mirrored (the same code)
void beginner_camera_input(Fighter& f) {
    FighterCamera& camera = f.camera;
    if (camera.recenter_frames > 0) {
        camera.recenter_frames -= 1;
        camera.pitch = angle_scaled(camera.recenter_pitch,
                                    fx_ratio(camera.recenter_frames, recentre_steps));
        camera.pitch_stick = Fx{};
    } else {
        const Fx target = axis(f, axis_pitch);
        Fx& held = camera.pitch_stick;
        if (held > Fx{}) {
            if (target > held) {
                held = target;
            } else if (target < Fx{}) {
                camera.recenter_frames = recentre_steps;
                camera.recenter_pitch = camera.pitch;
            }
        } else if (held < Fx{}) {
            if (target < held) {
                held = target;
            } else if (target > Fx{}) {
                camera.recenter_frames = recentre_steps;
                camera.recenter_pitch = camera.pitch;
            }
        } else if (target != Fx{}) {
            held = target;
        }
        held = fx_clamp(held, -beginner_pitch_most, beginner_pitch_most);
        const Fx pitch = fx_mul_near(angle_radians(camera.pitch), beginner_pitch_keep) +
                         fx_mul_near(held, beginner_pitch_take);
        camera.pitch = angle_from_radians(pitch);
        if (fx_abs(held - pitch) < beginner_pitch_near) {
            held = target;
        }
    }
    // the quick turn: locked out, nothing turns and the turn left is dropped
    if (camera.quick_turn_lock > 0) {
        camera.auto_turning = false;
        camera.quick_turn_lock -= 1;
        camera.quick_turn_left = Angle{};
        return;
    }
    if (f.control_type & control_type_standard) {
        return;
    }
    const Fx twist = axis(f, axis_twist);
    const std::int32_t left = angle_signed(camera.quick_turn_left);
    if (left == 0) {
        // retail queues +pi/2 for a positive stick; mirrored here
        camera.auto_turning = false;
        if (twist > quick_turn_stick) {
            camera.quick_turn_left = Angle{0u - angle_quarter_raw};
        } else if (twist < -quick_turn_stick) {
            camera.quick_turn_left = Angle{angle_quarter_raw};
        }
        return;
    }
    if (axis(f, axis_leg_turn) != Fx{}) {
        camera.quick_turn_left = Angle{};
    }
    // the stick thrown the other way locks the turn out
    if ((left < 0 && twist < -quick_turn_stick) || (left > 0 && twist > quick_turn_stick)) {
        camera.quick_turn_lock = quick_turn_lock_steps;
    }
    camera.auto_turning = true;
    const std::int32_t most = std::abs(angle_signed(angle_scaled(f.turn_step, quick_turn_rate)));
    const std::int32_t rest = angle_signed(camera.quick_turn_left);
    const std::int32_t step = rest < -most ? -most : rest > most ? most : rest;
    f.heading += Angle{static_cast<std::uint32_t>(step)};
    camera.quick_turn_left = Angle{static_cast<std::uint32_t>(rest - step)};
}

} // namespace

// decomp: actor_camera_input 0x7D2B0
void fighter_camera_input(Fighter& f) {
    FighterCamera& camera = f.camera;
    if (f.control.control_buttons & control_recentre) {
        camera.recenter_pitch = camera.pitch;
        camera.recenter_frames = recentre_steps;
    }
    if (f.camo.beginner) {
        beginner_camera_input(f);
        return;
    }
    if (camera.recenter_frames > 0) {
        const Angle aim_before = f.aim_yaw;
        camera.recenter_frames -= 1;
        const Fx share = fx_ratio(camera.recenter_frames, recentre_steps);
        camera.pitch = angle_scaled(camera.recenter_pitch, share);
        f.aim_yaw = angle_scaled(f.aim_yaw_recenter, share);
        camera.auto_turning = true;
        camera.auto_turn_delta = aim_before - f.aim_yaw;
        f.heading += camera.auto_turn_delta;
        return;
    }
    const Fx pitch_axis = axis(f, axis_pitch);
    camera.pitch_stick = fx_mul_near(fx_abs(pitch_axis), pitch_stick_take) +
                         fx_mul_near(camera.pitch_stick, pitch_stick_keep);
    const Angle limit =
        angle_from_radians(fx_mul_near(camera.pitch_stick, pitch_limit_stick) + pitch_limit_base);
    // retail lowers its pitch for a positive axis; the port's pitch is the mirrored one, so it
    // rises
    camera.pitch = angle_clamp(camera.pitch + angle_scaled(pitch_step, pitch_axis), limit);
    if (!(f.control_type & control_type_standard)) {
        // aim_yaw += ticks x turn_deg x pi/180 x stick: per tick, auto (sixty.md R9)
        f.aim_yaw = angle_clamp(f.aim_yaw - angle_scaled(f.turn_step, axis(f, axis_twist)),
                                quarter_pi);
    }
    f.aim_yaw_recenter = f.aim_yaw;
    camera.auto_turning = false;
    camera.auto_turn_delta = Angle{};
}

// decomp: actor_camera_update 0x7DCAA
void fighter_camera_update(Fighter& f, const BattleData& data, const StageWorld* stage) {
    FighterCamera& camera = f.camera;
    // the view yaw closes on heading + aim_yaw, the pitch on the pitch held within pi/4
    camera.view_yaw += camera.auto_turn_delta;
    camera.view_yaw += angle_scaled(f.heading + f.aim_yaw - camera.view_yaw, view_follow);
    const Angle pitch = angle_clamp(camera.pitch, quarter_pi);
    camera.view_pitch += angle_scaled(pitch - camera.view_pitch, view_follow);
    // head_position (the body node, set by last step's pose) less the position
    camera.head_offset = f.pose.head_position - f.position;
    camera_eye_update(f, data, stage);
}
