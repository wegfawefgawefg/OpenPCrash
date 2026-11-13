// The cockpit instruments (hud_draw_meters, decomp docs/port/hud.md "The cockpit
// instruments"): in view 0 with one view, models 0x4DD..0x4F1 under the HUD's identity view in
// pass 11, each tinted. The gauges they read step in hud_gauges.cpp (hud_meters_step); this file
// picks the models, their matrices, colours, gauges and texture cells for the frame, and the
// effects queue them (src/effects/cockpit_draw.cpp).
#include "hud/hud_internal.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr std::uint32_t meter_green = 0xFF64FF6E; // -0x9b0092
constexpr std::uint32_t armour_yellow = 0xFFFFC823;
constexpr std::uint32_t armour_red = 0xFFC04040;
constexpr std::uint32_t blink_blue = 0xFF8080E1;
constexpr std::uint32_t camo_blue = 0xFF3F77C1;
constexpr std::uint32_t camo_magenta = 0xFFC040C0;
// the cloak glow 0x4DD: 0x1E0F64 with the cloak's alpha
constexpr std::uint32_t glow_colour = 0x001E0F64;
// D_0017CB80 and D_0019F894: the groups slide up (0x4F1) and down (the rest) by 3 x
// view_switch_alpha
constexpr float slide_up = 3.0f, slide_down = -3.0f;
// D_00199F34 and D_0017E268: the jump cells (12) and the bar and speed cells (7) a full gauge spans
constexpr float jump_cells = 12.0f, bar_cells = 7.0f;
constexpr float radians_per_unit = 6.28318530718f / 4294967296.0f;

void add(HudCockpit& c, int slot, const Mat4& world, std::uint32_t factor, float gauge = -1) {
    CockpitModel m;
    m.slot = slot;
    m.world = world;
    m.factor = factor;
    m.gauge = gauge;
    c.models.push_back(m);
}

// cockpit_uv_set: a model's row of texel offsets on its 512 texture
void uv_set(HudCockpit& c, int model, const std::array<int, 2>& cell) {
    const int row = model - cockpit_first_model;
    c.uv[row][0] = static_cast<float>(cell[0]) / 512.0f;
    c.uv[row][1] = static_cast<float>(cell[1]) / 512.0f;
}

// camo_gauge_fraction, as the 2D gauge reads it (hud_gauges.cpp)
// decomp: camo_gauge_fraction 0x75342
float camo_fraction(const Camo& camo) {
    switch (camo.state) {
    case camo_ready:
        return 1.0f;
    case camo_tail:
    case camo_recharging:
        if (camo.recharge_max == Fx{}) {
            return 1.0f;
        }
        return fx_float(camo.recharge_max - camo.recharge) / fx_float(camo.recharge_max);
    case camo_none:
        return 0.0f;
    default:
        return fx_float(camo.left) / fx_float(camo.max);
    }
}

// the glow's alpha: 1 - timer / 60 fading in (states 1, 2), timer / 60 fading out (5, 6), timer
// / 20 in 7 and 8, whole while cloaked (3, 4). camo_timer is the port's step count (sixty.md
// "Camouflage": the original's 60 Hz ticks)
float glow_alpha(const Camo& camo) {
    const float timer = fx_float(camo.timer);
    switch (camo.state) {
    case 1:
    case 2:
        return 1.0f - timer / 60.0f;
    case 5:
    case 6:
        return timer / 60.0f;
    case 7:
    case 8:
        return timer / 20.0f;
    case 3:
    case 4:
        return 1.0f;
    default:
        return 0.0f;
    }
}

} // namespace

void hud_bob_step(HudSlot& s, const Fighter& f, bool shown) {
    // hud_update's fourth step, once an update while the HUD shows: bob follows the body's fall
    // since the last update (body_point, which actor_frame_reset copies at every update's start
    // whether the HUD shows or not, less the body matrix now), wholly within 1 of its value and
    // half the excess beyond
    const float y = fx_float(f.pose.head_position.y);
    const float delta = s.bob_body_y - y - s.bob;
    s.bob_body_y = y;
    if (!shown) {
        return;
    }
    if (delta > 1.0f) {
        s.bob += (delta - 1.0f) * 0.5f + 1.0f;
    } else if (delta < -1.0f) {
        s.bob += (delta + 1.0f) * 0.5f - 1.0f;
    } else {
        s.bob += delta;
    }
}

void hud_meters_build(HudView& hud, const World& world) {
    HudCockpit& c = hud.cockpit;
    const Fighter& f = world.fighters[static_cast<std::size_t>(hud.viewer)];
    const HudSlot& s = hud.slots[static_cast<std::size_t>(hud.viewer)];
    const HudData& d = hud.data;
    c.shown = true;
    c.models.clear();

    // the cells: the walk cycle (15 cells a second), the jump and the speed
    uv_set(c, 0x4ed, d.walk_cells[static_cast<std::size_t>(s.walk_frame / 4)]);
    const int jump_frame = std::min(static_cast<int>(jump_cells * s.jump_gauge), 11);
    uv_set(c, 0x4e1, d.jump_cells[static_cast<std::size_t>(jump_frame)]);
    const int jump_bar = std::min(static_cast<int>(bar_cells * s.jump_gauge), 6);
    uv_set(c, 0x4e2, d.bar_cells[static_cast<std::size_t>(jump_bar)]);
    const int speed_frame = std::min(static_cast<int>(bar_cells * s.speed_gauge), 6);
    uv_set(c, 0x4f0, d.bar_cells[static_cast<std::size_t>(speed_frame)]);
    uv_set(c, 0x4ef, d.speed_cells[static_cast<std::size_t>(speed_frame)]);

    // the cloak glow, a unit ahead, 1.5 wide
    const Camo& camo = f.camo;
    const float glow = glow_alpha(camo);
    if (glow != 0.0f) {
        const auto a = static_cast<std::uint32_t>(std::clamp(glow, 0.0f, 1.0f) * 255.0f);
        add(c, 0x4dd, mat_multiply(mat_scaling(1.5f, 1, 1), mat_translation(0, 0, 1)),
            glow_colour | a << 24);
    }

    // the turn to the aim against the view (the port's angles are the original's negated)
    const float turn = -static_cast<float>(angle_signed(
                           Angle{f.heading.raw + f.aim_yaw.raw - f.camera.view_yaw.raw})) *
                       radians_per_unit;
    const Mat4 spin = mat_rotation_y(turn);
    const float alpha = s.view_switch_alpha;
    // 0x4F1 sliding up, then 0x4DE, 0x4E9, 0x4ED, 0x4E1 sliding down, both bobbing half
    const Mat4 low = mat_translation(0, -0.1f - s.bob * 0.5f, 8.0f);
    add(c, 0x4f1, mat_multiply(spin, mat_multiply(low, mat_translation(0, slide_up * alpha, 0))),
        meter_green);
    const Mat4 lower =
        mat_multiply(spin, mat_multiply(low, mat_translation(0, slide_down * alpha, 0)));
    for (int model : {0x4de, 0x4e9, 0x4ed, 0x4e1}) {
        add(c, model, lower, meter_green);
    }
    // the rest bob a fifth: the frames, the jump bars (their second node scaled by the gauge),
    // the armour, the speed bars and dial, the O.C (camouflage) gauge
    const Mat4 high = mat_multiply(spin, mat_multiply(mat_translation(0, s.bob * 0.2f - 0.1f, 8.0f),
                                                      mat_translation(0, slide_down * alpha, 0)));
    add(c, 0x4df, high, meter_green);
    add(c, 0x4f0, high, meter_green);
    add(c, 0x4e2, high, meter_green);
    add(c, 0x4e3, high, meter_green, s.jump_gauge);
    add(c, 0x4ea, high, meter_green, s.jump_gauge);
    // the armour as the 2D bar: red under a quarter, blinking blue while it rolls up
    const float armour = static_cast<float>(s.bar_durability) /
                         static_cast<float>(std::max(f.combat.durability_max, 1));
    std::uint32_t armour_colour = armour < 0.25f ? armour_red : armour_yellow;
    if (f.combat.durability > s.bar_durability) {
        armour_colour = color_blink(hud, 10, armour_colour, blink_blue);
    }
    add(c, 0x4e0, high, armour_colour, armour);
    add(c, 0x4e5, high, meter_green, s.speed_gauge);
    add(c, 0x4ec, high, meter_green, s.speed_gauge);
    add(c, 0x4f0, high, meter_green); // submitted a second time, as retail does
    add(c, 0x4ef, high, meter_green);
    add(c, 0x4e8, high, meter_green);
    // the O.C gauge as the 2D camouflage bar: blinking green to blue while recharging, magenta
    // under a quarter, else blue
    const float oc = camo_fraction(camo);
    std::uint32_t oc_colour = oc < 0.25f ? camo_magenta : camo_blue;
    if (camo.state == 9 || camo.state == 10) {
        oc_colour = color_blink(hud, 10, meter_green, camo_blue);
    }
    add(c, 0x4e7, high, oc_colour, oc);
}
