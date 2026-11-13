// A fighter's six camera views and the switch between them (decomp src/od/u_07d2b0.c,
// docs/port/cameras.md "Input", "The six views", "Blending"): the d-pad's part of
// actor_camera_input, camera_view_functions (camera_view0_cockpit,
// camera_view1_chase, camera_view2_side, camera_view3_side_high,
// camera_view4_front, camera_view5_rails) and camera_view_blend. In the
// world because the camera is the aim: the HUD's screen points, the lock-on boxes and the
// tracking point are taken through the fighter's own look-at, whichever view it is.
#include "fx/mat.hpp"
#include "mech/fighter.hpp"
#include "mech/fighter_rates.hpp"
#include "stage/stage_world.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace {

// view_switch_frames: 20 updates, -1 an update, the blend new x (1 - frames / 20); R4: 40 steps,
// / 40 (sixty.md "Camera and aim")
constexpr int view_switch_steps = 40;
// the chase view: 2 up (camera_chase_height) plus 2 |pitch|, 6.15 behind
// (camera_chase_distance), looking at 8 ahead; z mirrored
constexpr Fx chase_height = fx(2);
constexpr Fx chase_distance = fx_f(6.15);
constexpr Fx chase_look_ahead = fx(8);
// the eye closes a third of the way an update: R7, 1 - sqrt(2/3)
constexpr Fx chase_follow = fx_f(0.18350341907227397);
// too close to the fighter (within radius + 1) it rises 0.1 an update: R1, 0.05 a step
constexpr Fx chase_rise = fx_f(0.05);
// the side views turn by the view yaw alone: camera_side_distance 5.5 out on the -x side at
// camera_side_height 1.65; camera_side_high_distance 8 out on +x at camera_side_high_height 5
// + 2 |pitch|; the front, camera_front_distance 8 ahead at camera_front_height 1.65 + 2 |pitch|
// (0x2326DC..0x232700); the port's local z is the original's negated
constexpr Fx side_distance = fx_f(5.5);
constexpr Fx side_height = fx_f(1.65);
constexpr Fx side_high_distance = fx(8);
constexpr Fx side_high_height = fx(5);
constexpr Fx front_distance = fx(8);
constexpr Fx front_height = fx_f(1.65);
// rail_repick_clock wraps at 30 calls, one an update; R4: 60 steps
constexpr int rail_repick_steps = 60;
// rail_nearest_picks' sentinel distance
constexpr Fx rail_far = fx(10000);
// hit_shake_jitter's draws are 0..0.8 (0..0.1 for the tilt), so retail's shake only ever moves
// the look-at up and right; the port centres them (docs/bugs.md): the draws less half their range
constexpr Fx shake_half = fx_f(0.4);
constexpr Fx tilt_half = fx_f(0.05);

Fx abs_pitch_radians(const FighterCamera& camera) {
    return angle_radians(
        Angle{static_cast<std::uint32_t>(std::abs(angle_signed(camera.view_pitch)))});
}

// the eye smoothed from the last one stored (camera.eye)
// decomp: camera_view1_chase 0x7ED7A
void chase_view(const Fighter& f, const BattleData& data, FxVec3& eye_out, FxVec3& target) {
    const FighterCamera& camera = f.camera;
    const Fx height = chase_height + abs_pitch_radians(camera) * 2;
    const FxMat3 turn =
        mat3_mul(mat3_rotation_x(camera.view_pitch), mat3_rotation_y(camera.view_yaw));
    target = f.position + transform({Fx{}, height, chase_look_ahead}, turn);
    FxVec3 eye = f.position + camera.head_offset + transform({Fx{}, height, -chase_distance}, turn);
    if (!f.grounded) {
        eye.y += f.head_height - fx_mul_near(f.radius, fx_f(0.5));
    }
    // a wall between the head and the eye brings the eye in front of it
    const Fx anchor_up = f.grounded ? f.head_height : f.head_height - f.radius;
    const FxVec3 anchor = f.position + FxVec3{Fx{}, anchor_up, Fx{}};
    FxVec3 desired = eye;
    FxVec3 point, normal;
    if (collision_segment_frame(data.stage, anchor, eye, &point, &normal, false)) {
        desired = point + normal;
    }
    FxVec3 step = desired - camera.eye;
    step = {fx_mul_near(step.x, chase_follow), fx_mul_near(step.y, chase_follow),
            fx_mul_near(step.z, chase_follow)};
    FxVec3 next = camera.eye + step;
    const FxVec3 centre = f.position + FxVec3{Fx{}, f.radius, Fx{}};
    if (shorter_than(centre - next, f.radius + fx(1))) {
        step.y += chase_rise;
        next = camera.eye + step;
    }
    SphereQuery sphere{next, camera.eye, step, fx(1), Fx{}};
    eye_out = collision_query_sphere(data.stage, sphere) ? sphere.centre : next;
}

// the eye at the body node plus the leg class's eye height and
// reach ahead (cockpit_eye_height_by_leg, cockpit_eye_ahead_by_leg, D_002326D8 [0] and [6] both 0),
// turned by the view; the target 24 ahead of the fighter's centre; the eye kept out of the walls
// by a sphere of the fighter's radius. The hit shake's move of the target is cockpit_shake below
// decomp: camera_view0_cockpit 0x7E881
void cockpit_view(const Fighter& f, const BattleData& data, FxVec3& eye_out, FxVec3& target) {
    static constexpr Fx heights[21] = {
        fx_f(0.8), fx_f(0.9), fx_f(0.8), fx_f(1.0), fx_f(1.1), fx_f(0.9), fx_f(1.1),
        fx_f(1.1), fx_f(1.0), fx_f(1.0), fx_f(0.8), fx_f(0.4), fx_f(0.4), fx_f(0.4),
        fx_f(0.4), fx_f(0.4), fx_f(0.4), fx_f(1.2), fx_f(1.2), fx_f(1.2), fx_f(0.0)};
    static constexpr Fx aheads[21] = {
        fx_f(1.0), fx_f(1.0), fx_f(1.0), fx_f(1.6), fx_f(1.5), fx_f(1.2), fx_f(1.2),
        fx_f(1.2), fx_f(0.9), fx_f(1.0), fx_f(1.0), fx_f(1.7), fx_f(1.8), fx_f(1.7),
        fx_f(1.8), fx_f(1.7), fx_f(1.7), fx_f(1.0), fx_f(1.2), fx_f(1.5), fx_f(0.0)};
    const FighterCamera& camera = f.camera;
    const std::size_t leg =
        static_cast<std::size_t>(f.leg_class >= 0 && f.leg_class < 21 ? f.leg_class : 0);
    const FxMat3 turn =
        mat3_mul(mat3_rotation_x(camera.view_pitch), mat3_rotation_y(camera.view_yaw));
    const FxVec3 eye =
        f.position + camera.head_offset + transform({Fx{}, heights[leg], aheads[leg]}, turn);
    target = f.position + transform({Fx{}, Fx{}, fx(24)}, turn) + FxVec3{Fx{}, f.radius, Fx{}};
    SphereQuery sphere{eye, eye, f.velocity, f.radius, Fx{}};
    eye_out = collision_query_sphere(data.stage, sphere) ? sphere.centre : eye;
}

// the hit shake reaches this fighter's look-at: the stage holds a jitter, and the fighter is an
// AI (retail's cockpit_shake_x / y are globals every view 0 reads, and an AI's camera is always
// view 0) or the human whose hit set it (StageWorld::hit_seat; retail has one human outside VS)
bool shake_reaches(const Fighter& f, const StageWorld* stage) {
    return stage && hit_shake_held(*stage) &&
           (f.controller == -1 || f.controller == stage->hit_seat);
}

// camera_view0_cockpit's (cockpit_shake_x, cockpit_shake_y, 0) turned by the view: the shake's
// move of the cockpit target
FxVec3 cockpit_shake(const Fighter& f, const StageWorld* stage) {
    if (!shake_reaches(f, stage)) {
        return {};
    }
    const FxMat3 turn =
        mat3_mul(mat3_rotation_x(f.camera.view_pitch), mat3_rotation_y(f.camera.view_yaw));
    return transform({stage->shake_x - shake_half, stage->shake_y - shake_half, Fx{}}, turn);
}

// views 2..5's target: the head point raised by the radius
FxVec3 head_target(const Fighter& f) {
    return f.position + f.camera.head_offset + FxVec3{Fx{}, f.radius, Fx{}};
}

// camera_view2_side, camera_view3_side_high, camera_view4_front: the eye about the fighter's
// position (not its head; cameras.md's table says the head, the code doesn't) by the view yaw
FxVec3 orbit_eye(const Fighter& f, int view) {
    const Fx up = abs_pitch_radians(f.camera) * 2;
    FxVec3 local;
    if (view == 2) {
        local = {-side_distance, side_height, Fx{}};
    } else if (view == 3) {
        local = {side_high_distance, side_high_height + up, Fx{}};
    } else {
        local = {Fx{}, front_height + up, front_distance};
    }
    return f.position + transform(local, mat3_rotation_y(f.camera.view_yaw));
}

// stage_zone_functions_by_mode: shibuya_zone_at and aqualine_zone_at in the
// original's frame (z the port's negated), 0 for Shinjuku and the training stage
int stage_zone_of(int selector, FxVec3 at) {
    const Fx x = at.x, y = at.y, z = -at.z;
    switch (selector / 2) {
    case 1:
        // inside the round plaza's 39.295 circle by height, outside by depth
        if (shorter_than(FxVec2{x - fx_f(30.522), z - fx_f(59.5941)}, fx_f(39.295))) {
            return y < fx(-25) ? 2 : y < fx_f(-0.5) ? 0 : 1;
        }
        return y > fx_f(-2.5) ? 1 : y > fx(-40) ? 3 : 4;
    case 2:
        // the island's box by height, the far end, else outside
        if (fx(-92) < x && x < fx_f(60.5) && fx(-32) < z && z < fx(32)) {
            return y < fx(12) ? 1 : y < fx(23) ? 2 : 3;
        }
        return x < fx(-142) ? 4 : 0;
    default:
        return 0;
    }
}

// every 30th call the three rail points of the hit centre's zone
// nearest it across are tried in order, the first with a clear line to it (the stage, and the
// props not gone) kept, else the nearest; the eye is the kept point
// decomp: camera_view5_rails 0x7F5C1
FxVec3 rails_eye(Fighter& f, const BattleData& data, const StageWorld* stage) {
    const CameraRails& rails = data.rails;
    FighterCamera& camera = f.camera;
    const int count = static_cast<int>(rails.points.size());
    if (count == 0) {
        return orbit_eye(f, 4);
    }
    const FxVec3 centre = f.combat.damage.center;
    if (camera.rail_repick_clock == 0) {
        const int zone = stage_zone_of(rails.selector, centre);
        const int first = rails.zones[zone][0];
        const int end = std::min(rails.zones[zone][1], count);
        // the three nearest; with fewer than three only those are tried (retail repeats the last:
        // docs/bugs.md)
        int picks[3] = {0, 0, 0};
        if (first < end) {
            std::vector<Fx> distances(static_cast<std::size_t>(count), rail_far);
            for (int j = first; j < end; ++j) {
                const FxVec3 d = rails.points[static_cast<std::size_t>(j)] - centre;
                distances[static_cast<std::size_t>(j)] = length(FxVec2{d.x, d.z});
            }
            for (int& pick : picks) {
                Fx nearest = rail_far;
                for (int j = first; j < end; ++j) {
                    nearest = fx_min(nearest, distances[static_cast<std::size_t>(j)]);
                }
                for (int j = first; j < end; ++j) {
                    if (distances[static_cast<std::size_t>(j)] == nearest) {
                        distances[static_cast<std::size_t>(j)] = rail_far;
                        pick = j;
                    }
                }
            }
        }
        camera.rail_point = -1;
        const int tries = first < end ? std::min(3, end - first) : 1;
        for (int k = 0; k < tries; ++k) {
            const int pick = picks[k];
            const FxVec3 from = rails.points[static_cast<std::size_t>(pick)];
            FxVec3 point, normal;
            bool blocked =
                collision_segment_frame(data.stage, from, centre, &point, &normal, false) != 0;
            if (stage) {
                for (const Prop& prop : stage_props(*stage)) {
                    PropRayHit hit;
                    if (!(prop.flags & prop_gone) &&
                        prop_ray_test(prop, from, centre, false, &hit)) {
                        blocked = true;
                    }
                }
            }
            if (!blocked) {
                camera.rail_point = pick;
                break;
            }
        }
        if (camera.rail_point < 0) {
            camera.rail_point = picks[0];
        }
    }
    camera.rail_repick_clock = (camera.rail_repick_clock + 1) % rail_repick_steps;
    const int point = camera.rail_point >= 0 && camera.rail_point < count ? camera.rail_point : 0;
    return rails.points[static_cast<std::size_t>(point)];
}

// camera_view_functions[view]
void view_eye(Fighter& f, const BattleData& data, const StageWorld* stage, int view, FxVec3& eye,
              FxVec3& target) {
    switch (view) {
    case 0:
        cockpit_view(f, data, eye, target);
        break;
    case 2:
    case 3:
    case 4:
        eye = orbit_eye(f, view);
        target = head_target(f);
        break;
    case 5:
        eye = rails_eye(f, data, stage);
        target = head_target(f);
        break;
    default:
        chase_view(f, data, eye, target);
        break;
    }
}

FxVec3 mix(FxVec3 a, FxVec3 b, Fx keep) {
    const Fx take = fx(1) - keep;
    return {fx_mul_near(a.x, keep) + fx_mul_near(b.x, take),
            fx_mul_near(a.y, keep) + fx_mul_near(b.y, take),
            fx_mul_near(a.z, keep) + fx_mul_near(b.z, take)};
}

} // namespace

FxVec3 cockpit_shake_of(const Fighter& f, const StageWorld* stage) {
    return cockpit_shake(f, stage);
}

void camera_view_input(Fighter& f, bool single_view) {
    FighterCamera& camera = f.camera;
    const std::uint32_t buttons = f.control.control_buttons;
    // d-pad up: the cockpit, in one view only; else d-pad down: the chase view. Both blend
    if (buttons & control_view_near) {
        if (single_view && camera.view_switch_frames == 0 && camera.view != 0) {
            camera.view_next = 0;
            camera.view_switch_frames = view_switch_steps;
        }
    } else if ((buttons & control_view_far) && camera.view_switch_frames == 0 && camera.view != 1) {
        camera.view_next = 1;
        camera.view_switch_frames = view_switch_steps;
    }
    // d-pad right steps 2, 3, 4, 5, 2 and left the other way (from 0 or 1: right goes to 2, left
    // to 5), a cut, not in beginner mode nor during a blend
    if (!f.camo.beginner && camera.view == camera.view_next) {
        if (buttons & control_view_right) {
            camera.view = camera.view < 2 ? 2 : (camera.view - 1) % 4 + 2;
            camera.view_next = camera.view;
            camera.rail_repick_clock = 0;
        } else if (buttons & control_view_left) {
            camera.view = camera.view < 2 ? 5 : (camera.view + 1) % 4 + 2;
            camera.view_next = camera.view;
            camera.rail_repick_clock = 0;
        }
    }
    if (camera.view_switch_frames > 0) {
        camera.view_switch_frames -= 1;
    } else {
        camera.view = camera.view_next;
    }
}

void camera_view_cut(FighterCamera& camera, int view) {
    camera.view = camera.view_next = view;
    camera.view_switch_frames = 0;
}

// decomp: camera_eye_update 0x7FF25
void camera_eye_update(Fighter& f, const BattleData& data, const StageWorld* stage) {
    FighterCamera& camera = f.camera;
    FxVec3 eye, target;
    // the hit shake: view 0's target moved by it, and in a blend by view 0's share of it
    const FxVec3 shake = cockpit_shake(f, stage);
    FxVec3 shift;
    // an AI's camera is always the cockpit, whatever its view (camera_eye_update): what
    // its HUD's on-screen tests see. A human's is its view, blended into view_next over a switch
    if (f.controller == -1) {
        cockpit_view(f, data, eye, target);
        shift = shake;
    } else {
        view_eye(f, data, stage, camera.view, eye, target);
        if (camera.view == 0) {
            shift = shake;
        }
        if (camera.view != camera.view_next) {
            FxVec3 next_eye, next_target;
            view_eye(f, data, stage, camera.view_next, next_eye, next_target);
            const Fx keep = fx_ratio(camera.view_switch_frames, view_switch_steps);
            eye = mix(eye, next_eye, keep);
            target = mix(target, next_target, keep);
            shift = mix(shift, camera.view_next == 0 ? shake : FxVec3{}, keep);
        }
    }
    camera.eye = eye;
    camera.target = target + shift;
    camera.shake_shift = shift;
    // the look-at's up: camera_shake_up (t, 1 - t, 0) for a human in view 0 while the shake holds,
    // else (0, 1, 0) (an AI's view is 1 in retail, so its up is plain)
    camera.up = {Fx{}, fx(1), Fx{}};
    if (f.controller != -1 && camera.view == 0 && shake_reaches(f, stage)) {
        const Fx t = stage->shake_tilt - tilt_half;
        camera.up = {t, fx(1) - t, Fx{}};
    }
}

void cockpit_camera_of(const Fighter& f, const BattleData& data, FxVec3& eye, FxVec3& target) {
    cockpit_view(f, data, eye, target);
}

void fighter_view_of(const Fighter& f, const BattleData& data, int view, FxVec3& eye,
                     FxVec3& target) {
    if (view == 0) {
        cockpit_view(f, data, eye, target);
    } else if (view == 1) {
        chase_view(f, data, eye, target);
    } else {
        eye = orbit_eye(f, view);
        target = head_target(f);
    }
}
