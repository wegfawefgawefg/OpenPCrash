// The reticle's boxes and the lock marks (decomp docs/port/hud.md "The reticle and the tracking
// box", "The lock box and lock-on"): hud_draw_tracking_box, hud_draw_lock_box,
// hud_melee_lock_draw, and the marks' screen trail (lock_mark_start,
// lock_mark_step) kept here beside the world's marks. hud_update draws the tracking box,
// the radar, the lock box and the melee mark in that order, all at layer 0x101.
#include "hud/hud_internal.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float pi = 3.14159265f;
constexpr std::uint32_t box_colour = 0x7FFFFFFF;
constexpr std::uint32_t mark_colour = 0xCC00B496;
// PC: the mouse's target dot, the tracking dot at two thirds of its alpha
constexpr std::uint32_t mouse_target_colour = 0x50FFFFFF;
// centred, filtered, unsnapped; the marks add
constexpr std::uint32_t reticle_flags = 0x1C;
constexpr std::uint32_t mark_flags = 0x1D;
// lock_mark_corners (the extract doesn't split it out of the code range yet): a mark's
// four corners 9 px round its goal
constexpr float mark_corners[4][2] = {
    {-0.028125f, 0.0375f}, {0.028125f, 0.0375f}, {0.028125f, -0.0375f}, {-0.028125f, -0.0375f}};
// the wobble's spread (rand_float_crt(0.075), (0.1)): retail's is 0..spread, up and right only;
// the port centres it (docs/bugs.md)
constexpr float wobble_x = 0.075f, wobble_y = 0.1f;

float rand_unit(std::uint32_t& state) {
    state = state * 1103515245u + 12345u;
    return static_cast<float>((state >> 16) & 0x7FFF) / 32767.0f;
}

// lock_mark_start: the wobble, and the trail at the lock box's corners round the reticle (retail
// puts them round the screen's centre whatever the pitch; docs/bugs.md)
void mark_start(HudView& hud, HudMarkTrail& t, int target, const float box[2],
                const float centre[2]) {
    t.target = target;
    t.offset[0] = (rand_unit(hud.random) - 0.5f) * wobble_x;
    t.offset[1] = (rand_unit(hud.random) - 0.5f) * wobble_y;
    const float sign[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
    for (int i = 0; i < 4; ++i) {
        t.trail[i][0] = centre[0] + sign[i][0] * box[0];
        t.trail[i][1] = centre[1] + sign[i][1] * box[1];
        t.prev[i][0] = t.trail[i][0];
        t.prev[i][1] = t.trail[i][1];
    }
}

// one world mark's trail: started when the world starts it, then closed toward the target's
// screen point + wobble + corner. Retail keeps timer / lock_frames of the distance once an update;
// the port keeps the square root of that a step, which is the same over the pair (hud.md "Timers")
void mark_sync(HudView& hud, HudMarkTrail& t, const LockMark& m, float frames, const float box[2],
               const float centre[2], const float target_at[2]) {
    if (m.target == -1) {
        t.target = -1;
        return;
    }
    const float timer = fx_float(m.timer);
    if (t.target != m.target || timer > t.timer) {
        mark_start(hud, t, m.target, box, centre);
    }
    t.timer = timer;
    const float keep = frames > 0 ? std::sqrt(std::max(timer, 0.0f) / frames) : 0.0f;
    for (int i = 0; i < 4; ++i) {
        t.prev[i][0] = t.trail[i][0];
        t.prev[i][1] = t.trail[i][1];
        const float gx = target_at[0] + t.offset[0] + mark_corners[i][0];
        const float gy = target_at[1] + t.offset[1] + mark_corners[i][1];
        t.trail[i][0] = gx + (t.trail[i][0] - gx) * keep;
        t.trail[i][1] = gy + (t.trail[i][1] - gy) * keep;
    }
}

// four corners of a box round the reticle centre: retail moves each in by (half the sprite + pad)
// of its 640 x 480 pixels / 640 (or / 480) screen units, mirrors the right ones and turns the
// bottom ones over
void box_draw(Ui& ui, const HudFrame& frame, int sprite, const float centre[2], float bx,
              float by, float pad) {
    const float hx = (static_cast<float>(sprite_width(ui, sprite)) * 0.5f + pad) / 640.0f;
    const float hy = (static_cast<float>(sprite_height(ui, sprite)) * 0.5f + pad) / 480.0f;
    const float one = frame.sprite_scale;
    const struct {
        float sx, sy, flip, turn;
    } corners[4] = {{-1, 1, 1, 0}, {1, 1, -1, 0}, {1, -1, 1, pi}, {-1, -1, -1, pi}};
    for (const auto& c : corners) {
        const float ux = centre[0] + c.sx * (bx - hx);
        const float uy = centre[1] + c.sy * (by - hy);
        hud_sprite(ui, sprite, hud_px(frame, ux), hud_py(frame, uy), c.flip * one, one, c.turn,
                   0x101, box_colour, reticle_flags);
    }
}

void marks_draw(Ui& ui, const HudFrame& frame, const HudMarkTrail& t, int sprite) {
    if (t.target == -1) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        const float ux = lerpf(t.prev[i][0], t.trail[i][0], frame.alpha);
        const float uy = lerpf(t.prev[i][1], t.trail[i][1], frame.alpha);
        hud_sprite(ui, sprite, hud_px(frame, ux), hud_py(frame, uy), frame.sprite_scale,
                   frame.sprite_scale, static_cast<float>(i) * pi * 0.5f, 0x101, mark_colour,
                   mark_flags);
    }
}

} // namespace

void hud_marks_step(HudView& hud, const World& world, int slot) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    HudSlot& s = hud.slots[static_cast<std::size_t>(slot)];
    const LockHud& lock = f.combat.hud;
    const float box[2] = {fx_float(lock.box_b[0]), fx_float(lock.box_b[1])};
    const float frames = static_cast<float>(f.combat.lock_frames);
    for (int i = 0; i < lock_mark_count; ++i) {
        const LockMark& m = lock.locks[i];
        const int target = m.target >= 0 && m.target < fighter_slots ? m.target : 0;
        mark_sync(hud, s.marks[static_cast<std::size_t>(i)], m, frames, box, s.center[1],
                  s.sense[1][target]);
    }
    const LockMark& melee = lock.warning;
    const int target = melee.target >= 0 && melee.target < fighter_slots ? melee.target : 0;
    mark_sync(hud, s.melee, melee, frames, box, s.center[1], s.sense[1][target]);
}

void hud_draw_reticle(HudView& hud, Ui& ui, const World& world, const HudFrame& frame) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(hud.viewer)];
    const HudSlot& s = hud.slots[static_cast<std::size_t>(hud.viewer)];
    const LockHud& lock = f.combat.hud;
    const float centre[2] = {lerpf(s.center[0][0], s.center[1][0], frame.alpha),
                             lerpf(s.center[0][1], s.center[1][1], frame.alpha)};
    // the tracking box (0x561..0x563 by the chip's kind) and its dot at the tracking point
    const int kind_a = f.combat.reticle_kind_a >= 0 && f.combat.reticle_kind_a <= 2
                           ? f.combat.reticle_kind_a
                           : 0;
    box_draw(ui, frame, 0x561 + kind_a, centre, fx_float(lock.box_a[0]), fx_float(lock.box_a[1]),
             2.0f);
    const float tx = lerpf(s.track[0][0], s.track[1][0], frame.alpha);
    const float ty = lerpf(s.track[0][1], s.track[1][1], frame.alpha);
    hud_sprite(ui, 0x56b, hud_px(frame, tx), hud_py(frame, ty), frame.sprite_scale,
               frame.sprite_scale, 0, 0x101, box_colour, reticle_flags);
    // PC: where the mouse asked to aim, a dimmer copy of the dot, until the aim gets there. The
    // angle left goes to screen units through the world's projection (52 degrees over 576 x 448)
    if (f.controller >= 0 && f.controller < 4) {
        const auto& target = hud.mouse_target[static_cast<std::size_t>(f.controller)];
        if (target[0] != 0.0f || target[1] != 0.0f) {
            const float half = std::tan(26.0f * pi / 180.0f);
            const float ux = centre[0] + std::tan(std::clamp(target[0], -1.4f, 1.4f)) /
                                             (half * 576.0f / 448.0f);
            const float uy = centre[1] + std::tan(std::clamp(target[1], -1.4f, 1.4f)) / half;
            hud_sprite(ui, 0x56b, hud_px(frame, ux), hud_py(frame, uy), frame.sprite_scale,
                       frame.sprite_scale, 0, 0x101, mouse_target_colour, reticle_flags);
        }
    }
    hud_draw_radar(hud, ui, world, frame);
    // the lock box (0x564..0x566) and the missile marks: back mount 0's 0x56E, 1's 0x56D
    const int kind_b = f.combat.reticle_kind_b >= 0 && f.combat.reticle_kind_b <= 2
                           ? f.combat.reticle_kind_b
                           : 0;
    box_draw(ui, frame, 0x564 + kind_b, centre, fx_float(lock.box_b[0]), fx_float(lock.box_b[1]),
             14.0f);
    for (int i = 0; i < lock_mark_count; ++i) {
        const HudMarkTrail& t = s.marks[static_cast<std::size_t>(i)];
        marks_draw(ui, frame, t, lock.locks[i].mount == 0 ? 0x56e : 0x56d);
    }
    // the melee lock's mark
    marks_draw(ui, frame, s.melee, 0x56f);
}
