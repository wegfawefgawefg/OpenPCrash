#include "view/view.hpp"

#include "render/camo_draw.hpp"
#include "view/scene_camera.hpp"

#include <cmath>

namespace {

constexpr float pi = 3.14159265358979f;
// radians in floats from here on: presentation
constexpr float radians_per_unit = 6.28318530718f / 4294967296.0f;

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}
float angle_lerp(float a, float b, float t) {
    float turn = b - a;
    turn = turn > pi ? turn - 2 * pi : turn < -pi ? turn + 2 * pi : turn;
    return a + turn * t;
}
float signed_radians(Angle a) {
    return static_cast<float>(angle_signed(a)) * radians_per_unit;
}
void to_floats(FxVec3 v, float out[3]) {
    out[0] = fx_float(v.x);
    out[1] = fx_float(v.y);
    out[2] = fx_float(v.z);
}

Mat4 mat_of(const FxMat4& m) {
    Mat4 out;
    const FxVec3* rows[4] = {&m.x, &m.y, &m.z, &m.w};
    for (int r = 0; r < 4; ++r) {
        out.m[r][0] = fx_float(rows[r]->x);
        out.m[r][1] = fx_float(rows[r]->y);
        out.m[r][2] = fx_float(rows[r]->z);
        out.m[r][3] = r == 3 ? 1.0f : 0.0f;
    }
    return out;
}

// the world's pose in floats: the palette, the mounts (0x10, 0x20, arm A, arm B, body, as
// MechPose orders them) and the weapon models' nodes
void pose_of(const FighterPoseState& in, MechPose& out) {
    for (int part = 0; part < pose_parts; ++part) {
        for (int m = 0; m < pose_matrices; ++m) {
            if (in.stored[part] & (1u << m)) {
                out.matrices[part][m] = mat_of(in.palette[part][m]);
                out.stored[part][m] = true;
            }
        }
    }
    for (int i = 0; i < 5; ++i) {
        out.mount[i] = mat_of(in.mount[i]);
        out.mounted[i] = in.mounted[i];
    }
    for (int w = 0; w < 4; ++w) {
        out.weapon_node_count[w] = in.weapon_node_count[w];
        for (int n = 0; n < in.weapon_node_count[w]; ++n) {
            out.weapon_nodes[w][n] = mat_of(in.weapon_nodes[w][n]);
        }
    }
}

// between two matrices: the places lerped, the turns as quaternions the short way round,
// normalized (a step apart the nlerp is the slerp to the eye)
struct Quat {
    float x, y, z, w;
};
Quat quat_of(const Mat4& m) {
    const float(*a)[4] = m.m;
    const float trace = a[0][0] + a[1][1] + a[2][2];
    if (trace > 0) {
        const float s = std::sqrt(trace + 1.0f) * 2;
        return {(a[1][2] - a[2][1]) / s, (a[2][0] - a[0][2]) / s, (a[0][1] - a[1][0]) / s, s / 4};
    }
    if (a[0][0] >= a[1][1] && a[0][0] >= a[2][2]) {
        const float s = std::sqrt(1.0f + a[0][0] - a[1][1] - a[2][2]) * 2;
        return {s / 4, (a[0][1] + a[1][0]) / s, (a[0][2] + a[2][0]) / s, (a[1][2] - a[2][1]) / s};
    }
    if (a[1][1] >= a[2][2]) {
        const float s = std::sqrt(1.0f + a[1][1] - a[0][0] - a[2][2]) * 2;
        return {(a[0][1] + a[1][0]) / s, s / 4, (a[1][2] + a[2][1]) / s, (a[2][0] - a[0][2]) / s};
    }
    const float s = std::sqrt(1.0f + a[2][2] - a[0][0] - a[1][1]) * 2;
    return {(a[0][2] + a[2][0]) / s, (a[1][2] + a[2][1]) / s, s / 4, (a[0][1] - a[1][0]) / s};
}
Mat4 blend_matrix(const Mat4& a, const Mat4& b, float t) {
    const Quat p = quat_of(a);
    Quat q = quat_of(b);
    if (p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w < 0) {
        q = {-q.x, -q.y, -q.z, -q.w};
    }
    Quat r{lerp(p.x, q.x, t), lerp(p.y, q.y, t), lerp(p.z, q.z, t), lerp(p.w, q.w, t)};
    const float size = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    r = {r.x / size, r.y / size, r.z / size, r.w / size};
    Mat4 out;
    out.m[0][0] = 1 - 2 * (r.y * r.y + r.z * r.z);
    out.m[0][1] = 2 * (r.x * r.y + r.z * r.w);
    out.m[0][2] = 2 * (r.x * r.z - r.y * r.w);
    out.m[1][0] = 2 * (r.x * r.y - r.z * r.w);
    out.m[1][1] = 1 - 2 * (r.x * r.x + r.z * r.z);
    out.m[1][2] = 2 * (r.y * r.z + r.x * r.w);
    out.m[2][0] = 2 * (r.x * r.z + r.y * r.w);
    out.m[2][1] = 2 * (r.y * r.z - r.x * r.w);
    out.m[2][2] = 1 - 2 * (r.x * r.x + r.y * r.y);
    for (int k = 0; k < 3; ++k) {
        out.m[3][k] = lerp(a.m[3][k], b.m[3][k], t);
    }
    return out;
}

FighterPose fighter_pose(const Fighter& f) {
    FighterPose pose;
    pose.present = f.present;
    to_floats(f.position, pose.position);
    pose.heading = signed_radians(f.heading);
    pose.clip = f.motion_slot_a;
    pose.phase = fx_float(f.motion_phase);
    pose.motion_state = f.motion_state;
    // the port's angles about y and x are the original's negated
    pose.aim_yaw = -signed_radians(f.aim_yaw);
    pose.pitch = -signed_radians(f.camera.view_pitch);
    pose.turn_rate = fx_float(f.turn_rate);
    pose_of(f.pose, pose.pose);
    return pose;
}

// a fighter's camera as the view shows it: a human's chase camera, or behind an AI watched with
// --follow (an AI's own camera is its cockpit). The target without the hit shake: the drawn
// camera takes it again unblended, scaled by CAMERA SHAKE (cockpit_shake_apply)
CameraPose fighter_camera(const Fighter& shown_fighter) {
    CameraPose pose;
    const FighterCamera& camera = shown_fighter.camera;
    pose.looks_at = true;
    to_floats(camera.eye, pose.position);
    to_floats(camera.target - camera.shake_shift, pose.target);
    if (shown_fighter.controller == -1) {
        float at[3];
        to_floats(shown_fighter.position, at);
        const float heading = signed_radians(shown_fighter.heading);
        const float fx = std::sin(heading), fz = std::cos(heading);
        pose.position[0] = at[0] - fx * 10.0f;
        pose.position[1] = at[1] + 5.0f;
        pose.position[2] = at[2] - fz * 10.0f;
        pose.target[0] = at[0] + fx * 6.0f;
        pose.target[1] = at[1] + 1.5f;
        pose.target[2] = at[2] + fz * 6.0f;
    }
    return pose;
}

StepPose step_pose(const World& world, int follow) {
    StepPose pose;
    if (!world.battle) {
        pose.camera = camera_pose(world.camera);
        return pose;
    }
    for (int i = 0; i < fighter_slots; ++i) {
        pose.fighters[i] = fighter_pose(world.fighters[static_cast<std::size_t>(i)]);
        camo_pose_from(world, world.fighters[static_cast<std::size_t>(i)], pose.fighters[i]);
    }
    // the main view: --follow's slot, else seat 0's fighter, else slot 0
    int shown = follow >= 0 && follow < fighter_slots &&
                        world.fighters[static_cast<std::size_t>(follow)].present
                    ? follow
                    : seat_slot(world, 0);
    shown = shown < 0 ? 0 : shown;
    pose.camera = fighter_camera(world.fighters[static_cast<std::size_t>(shown)]);
    // and each seat's own, for split screen
    for (int seat = 0; seat < max_pad_players; ++seat) {
        const int slot = seat_slot(world, seat);
        if (slot >= 0) {
            pose.seat_cameras[seat] = fighter_camera(world.fighters[static_cast<std::size_t>(slot)]);
        }
    }
    return pose;
}

} // namespace

CameraPose camera_pose(const FlyCamera& camera) {
    CameraPose pose;
    to_floats(camera.position, pose.position);
    pose.yaw = static_cast<float>(camera.yaw.raw) * radians_per_unit;
    pose.pitch = signed_radians(camera.pitch);
    return pose;
}

int seat_slot(const World& world, int seat) {
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        if (f.present && f.controller == seat) {
            return slot;
        }
    }
    return -1;
}

CameraPose interpolate_camera(const View& view, float alpha) {
    return interpolate_camera_pair(view.previous.camera, view.current.camera, alpha);
}

CameraPose interpolate_seat_camera(const View& view, int seat, float alpha) {
    const auto i = static_cast<std::size_t>(seat < 0 || seat >= max_pad_players ? 0 : seat);
    return interpolate_camera_pair(view.previous.seat_cameras[i], view.current.seat_cameras[i],
                                   alpha);
}

CameraPose interpolate_camera_pair(const CameraPose& a, const CameraPose& b, float alpha) {
    CameraPose pose;
    if (b.scene.row && a.scene.row == b.scene.row) {
        // the same scene camera both steps: its curves sampled at the drawn frame's own place
        // between them, not the two samples blended
        SceneCurveAt at = b.scene;
        at.frame = lerp(a.scene.frame, b.scene.frame, alpha);
        at.heading = angle_lerp(a.scene.heading, b.scene.heading, alpha);
        at.mix = lerp(a.scene.mix, b.scene.mix, alpha);
        for (int i = 0; i < 3; ++i) {
            at.focus[i] = lerp(a.scene.focus[i], b.scene.focus[i], alpha);
            at.own_position[i] = lerp(a.scene.own_position[i], b.scene.own_position[i], alpha);
            at.own_target[i] = lerp(a.scene.own_target[i], b.scene.own_target[i], alpha);
        }
        pose = scene_curve_pose(at);
    } else {
        for (int i = 0; i < 3; ++i) {
            pose.position[i] = lerp(a.position[i], b.position[i], alpha);
        }
        pose.fov = lerp(a.fov, b.fov, alpha);
        pose.roll = lerp(a.roll, b.roll, alpha);
    }
    if (pose.scene.row) {
        const float dx = pose.target[0] - pose.position[0];
        const float dy = pose.target[1] - pose.position[1];
        const float dz = pose.target[2] - pose.position[2];
        pose.yaw = std::atan2(dx, dz);
        pose.pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
        return pose;
    }
    if (b.looks_at) {
        // the eye and target blended, the angles taken from the blend
        for (int i = 0; i < 3; ++i) {
            pose.target[i] = lerp(a.target[i], b.target[i], alpha);
        }
        const float dx = pose.target[0] - pose.position[0];
        const float dy = pose.target[1] - pose.position[1];
        const float dz = pose.target[2] - pose.position[2];
        pose.looks_at = true;
        pose.yaw = std::atan2(dx, dz);
        pose.pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
        return pose;
    }
    pose.yaw = angle_lerp(a.yaw, b.yaw, alpha);
    pose.pitch = lerp(a.pitch, b.pitch, alpha);
    return pose;
}

FighterPose interpolate_fighter(const View& view, int slot, float alpha) {
    const FighterPose& a = view.previous.fighters[slot];
    const FighterPose& b = view.current.fighters[slot];
    FighterPose pose = b;
    if (!a.present || !b.present) {
        return pose;
    }
    for (int i = 0; i < 3; ++i) {
        pose.position[i] = lerp(a.position[i], b.position[i], alpha);
    }
    pose.heading = angle_lerp(a.heading, b.heading, alpha);
    pose.aim_yaw = lerp(a.aim_yaw, b.aim_yaw, alpha);
    pose.pitch = lerp(a.pitch, b.pitch, alpha);
    pose.turn_rate = lerp(a.turn_rate, b.turn_rate, alpha);
    for (int i = 0; i < 4; ++i) {
        pose.camo_sphere[i] = lerp(a.camo_sphere[i], b.camo_sphere[i], alpha);
    }
    if (a.clip == b.clip && b.phase >= a.phase) {
        pose.phase = lerp(a.phase, b.phase, alpha);
    }
    // the world's pose between the steps
    for (int part = 0; part < 3; ++part) {
        for (int m = 0; m < 32; ++m) {
            if (a.pose.stored[part][m] && b.pose.stored[part][m]) {
                pose.pose.matrices[part][m] =
                    blend_matrix(a.pose.matrices[part][m], b.pose.matrices[part][m], alpha);
            }
        }
    }
    for (int i = 0; i < 5; ++i) {
        if (a.pose.mounted[i] && b.pose.mounted[i]) {
            pose.pose.mount[i] = blend_matrix(a.pose.mount[i], b.pose.mount[i], alpha);
        }
    }
    for (int w = 0; w < 4; ++w) {
        if (a.pose.weapon_node_count[w] == b.pose.weapon_node_count[w]) {
            for (int n = 0; n < b.pose.weapon_node_count[w]; ++n) {
                pose.pose.weapon_nodes[w][n] =
                    blend_matrix(a.pose.weapon_nodes[w][n], b.pose.weapon_nodes[w][n], alpha);
            }
        }
    }
    return pose;
}

void take_step_poses(View& view, const World& before, const World& after) {
    view.previous = step_pose(before, view.follow);
    view.current = step_pose(after, view.follow);
    // camo_update reads the aim point before actor_settle_damage moves it to this update's
    // position (actor_step), so the reveal sphere sits on the last one's
    for (int i = 0; i < fighter_slots; ++i) {
        const Fighter& was = before.fighters[static_cast<std::size_t>(i)];
        const Fighter& now = after.fighters[static_cast<std::size_t>(i)];
        if (!before.battle || !was.present || !now.present) {
            continue;
        }
        const FxVec3 back = was.combat.aim_point - now.combat.aim_point;
        view.current.fighters[i].camo_sphere[0] += fx_float(back.x);
        view.current.fighters[i].camo_sphere[1] += fx_float(back.y);
        view.current.fighters[i].camo_sphere[2] += fx_float(back.z);
    }
}
