#include "view/cockpit_view.hpp"

#include "fx/mat.hpp"

#include <algorithm>
#include <cmath>

namespace {

// actor_draw hides the body while view_switch_frames > 15 leaving view 0, or under 5 coming into
// it; R4: 30 and 10 of the port's 40 steps
constexpr int body_hidden_leaving = 30;
constexpr int body_hidden_coming = 10;
constexpr int view_switch_steps = 40;
// the battle projection (52 degrees vertical over 576 x 448), as the HUD's screen units
constexpr float screen_y_scale = 2.0503038f;
constexpr float screen_x_scale = 2.0503038f * 448.0f / 576.0f;

const Fighter* human(const World& world, int slot) {
    if (!world.battle || slot < 0 || slot >= fighter_slots) {
        return nullptr;
    }
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    return f.present && f.controller != -1 ? &f : nullptr;
}

// playing, or the death scene's first step (the fighter's own camera still shows)
bool own_camera_scene(const World& world, int slot) {
    const FighterScene& scene = world.battle_state.slots[slot].scene;
    return scene.state == 0 || (scene.state == 7 && scene.step == 0);
}

// the death scene's first step: the fighter's own camera still shows (camera_scene_update)
bool dying_camera(const World& world, int slot) {
    const FighterScene& scene = world.battle_state.slots[slot].scene;
    return scene.state == 7 && scene.step == 0;
}

// camera_scene_update draws a human's camera with a plain up, but for the death scene's
// first step, where view 0 and 1 draw the fighter's own view_matrix (and a replay's view 0 its
// look-at with camera_shake_up): camera_eye_update built view 0's with camera_shake_up's tilt
// (the world's FighterCamera::up), so for up to 15 updates after the killing hit view 0 rolls.
// The roll about the view axis that turns the plain look-at's up into the tilted one's, times
// scale (CAMERA SHAKE); the renderer applies it after the look-at (camera_matrices)
// decomp: camera_scene_update 0x80068
void dying_tilt(CameraPose& pose, const Fighter& f, const World& world, int slot, float scale) {
    const FxVec3 plain{Fx{}, fx(1), Fx{}};
    if (!dying_camera(world, slot) || f.camera.up == plain || scale <= 0.0f) {
        return;
    }
    const float cp = std::cos(pose.pitch);
    const float forward[3] = {std::sin(pose.yaw) * cp, std::sin(pose.pitch),
                              std::cos(pose.yaw) * cp};
    const auto cross = [](const float a[3], const float b[3], float out[3]) {
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
    };
    const auto dot = [](const float a[3], const float b[3]) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    // the camera's frame as the renderer builds it (right = forward x up, up = right x forward),
    // once with the plain up and once with the tilted one
    const float up_plain[3] = {0.0f, 1.0f, 0.0f};
    const float up_tilt[3] = {fx_float(f.camera.up.x), fx_float(f.camera.up.y),
                              fx_float(f.camera.up.z)};
    float right[3], up[3], right_t[3], up_t[3];
    cross(forward, up_plain, right);
    cross(right, forward, up);
    cross(forward, up_tilt, right_t);
    cross(right_t, forward, up_t);
    if (dot(right, right) < 1e-8f || dot(up_t, up_t) < 1e-8f) {
        return;
    }
    // view x rotation_z(roll) has up' = sin(roll) right + cos(roll) up
    pose.roll += std::atan2(dot(up_t, right), dot(up_t, up)) * std::clamp(scale, 0.0f, 1.0f);
}

} // namespace

bool own_body_hidden(const World& world, int slot, int replay_view) {
    const Fighter* f = human(world, slot);
    if (!f || world.battle_phase == 5 || world.battle_phase == 3 ||
        !own_camera_scene(world, slot)) {
        return false;
    }
    if (world.battle_phase == 2) {
        return replay_view == 0;
    }
    const FighterCamera& c = f->camera;
    return (c.view == 0 && c.view_switch_frames > body_hidden_leaving) ||
           (c.view_next == 0 && c.view_switch_frames < body_hidden_coming);
}

bool own_lamps_hidden(const World& world, int slot) {
    const Fighter* f = human(world, slot);
    if (!f || world.battle_phase == 2 || !own_camera_scene(world, slot)) {
        return false;
    }
    return f->camera.view == 0 || f->camera.view_next == 0;
}

float cockpit_share(const World& world, int slot) {
    const Fighter* f = human(world, slot);
    if (!f) {
        return 0.0f;
    }
    const FighterCamera& c = f->camera;
    const float frac = static_cast<float>(c.view_switch_frames) / view_switch_steps;
    if (c.view == 0) {
        return c.view_next == 0 ? 1.0f : frac;
    }
    return c.view_next == 0 ? 1.0f - frac : 0.0f;
}

void cockpit_shake_apply(CameraPose& pose, const World& world, int slot, float scale) {
    const Fighter* f = human(world, slot);
    if (!f || scale <= 0.0f || !pose.looks_at || !own_camera_scene(world, slot)) {
        return;
    }
    // the world's shift this step, unblended (retail's jumps once an update), times the scale
    const FxVec3& shift = f->camera.shake_shift;
    if (shift != FxVec3{}) {
        pose.target[0] += fx_float(shift.x) * scale;
        pose.target[1] += fx_float(shift.y) * scale;
        pose.target[2] += fx_float(shift.z) * scale;
        const float dx = pose.target[0] - pose.position[0];
        const float dy = pose.target[1] - pose.position[1];
        const float dz = pose.target[2] - pose.position[2];
        pose.yaw = std::atan2(dx, dz);
        pose.pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
    }
    dying_tilt(pose, *f, world, slot, scale);
}

void dying_tilt_apply(CameraPose& pose, const World& world, int slot, float scale) {
    if (const Fighter* f = human(world, slot)) {
        dying_tilt(pose, *f, world, slot, scale);
    }
}

void replay_shake_apply(CameraPose& pose, const World& world, int slot, float scale) {
    const Fighter* f = human(world, slot);
    if (!f || scale <= 0.0f || !pose.looks_at || world.battle_phase != 2 ||
        !own_camera_scene(world, slot)) {
        return;
    }
    const FxVec3 shift = cockpit_shake_of(*f, &world.stage);
    if (shift != FxVec3{}) {
        pose.target[0] += fx_float(shift.x) * scale;
        pose.target[1] += fx_float(shift.y) * scale;
        pose.target[2] += fx_float(shift.z) * scale;
        const float dx = pose.target[0] - pose.position[0];
        const float dy = pose.target[1] - pose.position[1];
        const float dz = pose.target[2] - pose.position[2];
        pose.yaw = std::atan2(dx, dz);
        pose.pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
    }
}

void cockpit_shake_hud(const World& world, int slot, float scale, float out[2]) {
    out[0] = out[1] = 0.0f;
    const Fighter* f = human(world, slot);
    const float undo = 1.0f - std::clamp(scale, 0.0f, 1.0f);
    if (!f || undo <= 0.0f || !own_camera_scene(world, slot)) {
        return;
    }
    const FighterCamera& c = f->camera;
    const FxVec3 up{Fx{}, fx(1), Fx{}};
    if (c.shake_shift == FxVec3{} && c.up == up) {
        return;
    }
    // the world's points went through the shaken look-at; the same point through the plain one
    // says how far the shake moved them, and (1 - scale) of that is taken back
    const FxMat4 shook = mat4_look_at_lh(c.eye, c.target, c.up);
    const FxMat4 plain = mat4_look_at_lh(c.eye, c.target - c.shake_shift, up);
    const FxVec3 a = transform_coord(f->reticle_center_world, plain);
    const FxVec3 b = transform_coord(f->reticle_center_world, shook);
    if (a.z <= fx_f(0.1) || b.z <= fx_f(0.1)) {
        return;
    }
    out[0] = (fx_float(a.x) / fx_float(a.z) - fx_float(b.x) / fx_float(b.z)) * screen_x_scale * undo;
    out[1] = (fx_float(a.y) / fx_float(a.z) - fx_float(b.y) / fx_float(b.z)) * screen_y_scale * undo;
}
