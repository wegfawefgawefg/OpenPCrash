// The 2D gauges (hud_draw_durability_and_camo) and the per-step state the drawn set
// moves: the popup list (hud_draw_readout's tail) and the cockpit instruments' gauges and walk
// cycle (hud_draw_meters). Retail steps these inside its draw, once an update while the
// HUD shows; the port steps them once a world step with hud.md's 60 Hz rules.
#include "hud/hud_internal.hpp"

#include "ai/ai_links.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr std::uint32_t armour_yellow = 0xFFFFC823;
constexpr std::uint32_t armour_red = 0xFFC04040;
constexpr std::uint32_t blink_blue = 0xFF8080E1;
constexpr std::uint32_t camo_blue = 0xFF3F77C1;
constexpr std::uint32_t camo_magenta = 0xFFC040C0;
constexpr std::uint32_t camo_green = 0xFF64FF6E;
// the shown scroll's per-update half (shown_scroll x 0.25 x ticks) at 60 Hz (R7): 1 - sqrt(0.5),
// and its floor of 3 halved (R8)
constexpr float scroll_keep = 0.29289322f;
constexpr float scroll_least = 1.5f;
// the gauges ease at most 0.05 an update: 0.025 a step (R8)
constexpr float gauge_step = 0.025f;
// gravity a step squared (9.8 / 60^2): jump_step is the port's per step
constexpr float gravity_60 = 9.8f / 3600.0f;

// time left over the maximum while cloaking and cloaked (1..8),
// recharged over the recharge time (9, 10), 1 ready, 0 without the option
// decomp: camo_gauge_fraction 0x75342
float camo_fraction(const World& world, int slot) {
    const Camo& camo = world.fighters[static_cast<std::size_t>(slot)].camo;
    switch (camo.state) {
    case camo_ready:
        return 1.0f;
    case camo_tail:
    case camo_recharging:
        // no recharge time divides by zero in retail; the port shows it full (docs/bugs.md)
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

} // namespace

void hud_draw_gauges(HudView& hud, Ui& ui, const World& world) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(hud.viewer)];
    const HudSlot& s = hud.slots[static_cast<std::size_t>(hud.viewer)];
    const HudLayout& rows = hud_layout(hud);
    const bool one_view = hud_views(hud) == 1;
    const float armour_len = one_view ? 128.0f : 102.0f;
    const float camo_len = one_view ? 72.0f : 58.0f;
    // the gauges drop 100 px out of sight during a view change
    const float dy = s.view_switch_alpha * 100.0f;
    // armour: the rolling bar, red under 25%, blinking blue while it rolls up after a repair
    const float armour = static_cast<float>(s.bar_durability) /
                         static_cast<float>(std::max(f.combat.durability_max, 1));
    std::uint32_t armour_colour = armour < 0.25f ? armour_red : armour_yellow;
    if (f.combat.durability > s.bar_durability) {
        armour_colour = color_blink(hud, 10, armour_colour, blink_blue);
    }
    hud_row_draw(ui, rows[row_armour_frame], 1, 1, 0, dy);
    hud_row_draw(ui, rows[row_armour_bar], armour_len / 8.0f * armour, 1, 0, dy, armour_colour,
                 true);
    // camouflage: blinking green to blue while recharging, magenta under 25%, else blue
    const int state = camo_state(world, hud.viewer);
    const float camo = camo_fraction(world, hud.viewer);
    std::uint32_t camo_colour;
    if (state == 9 || state == 10) {
        camo_colour = color_blink(hud, 10, camo_green, camo_blue);
    } else {
        camo_colour = camo < 0.25f ? camo_magenta : camo_blue;
    }
    hud_row_draw(ui, rows[row_camo_frame], 1, 1, 0, dy);
    hud_row_draw(ui, rows[row_camo_bar], camo_len / 8.0f * camo, 1, 0, dy, camo_colour, true);
}

void hud_popups_step(HudSlot& s, bool one_view) {
    if (!one_view) {
        return; // split screen: the queue only fills
    }
    s.shown_scroll_prev = s.shown_scroll;
    // last step's lines count down after they were drawn (retail: at the end of the draw)
    for (HudPopup& line : s.shown) {
        if (line.kind != -1) {
            line.frames -= 1.0f;
            if (line.frames <= 0.0f) {
                line.frames = 120.0f;
                line.kind = -1;
            }
        }
    }
    // the queue's head into shown[0], the shown list pushed down, 15 px of scroll each
    int shown = 0;
    for (const HudPopup& line : s.shown) {
        shown += line.kind != -1 ? 1 : 0;
    }
    while (shown < hud_popup_lines && s.popup_count > 0) {
        std::copy_backward(s.shown, s.shown + hud_popup_lines - 1, s.shown + hud_popup_lines);
        ++shown;
        s.shown_scroll += 15.0f;
        s.shown_scroll_prev += 15.0f; // the lines moved down a place: blend from there
        s.shown[0] = s.queued[0];
        std::copy(s.queued + 1, s.queued + hud_popup_lines, s.queued);
        s.queued[hud_popup_lines - 1] = HudPopup{};
        s.popup_count -= 1;
    }
    // more waiting: the oldest lines are cut short to their last 10 ticks, up to one still coming in
    for (int i = 0; i < s.popup_count; ++i) {
        HudPopup& line = s.shown[hud_popup_lines - 1 - i];
        if (line.kind == -1) {
            continue;
        }
        if (line.frames > 110.0f) {
            break;
        }
        if (line.frames < 110.0f && line.frames > 10.0f) {
            line.frames = 10.0f;
        }
    }
    if (s.shown_scroll > 0) {
        s.shown_scroll -= std::max(s.shown_scroll * scroll_keep, scroll_least);
        s.shown_scroll = std::max(s.shown_scroll, 0.0f);
    }
}

void hud_meters_step(HudSlot& s, const Fighter& f) {
    // the walk cycle: 15 cells a second, a step at a time (retail: an update, cell = frame / 2)
    s.walk_frame = (s.walk_frame + 1) % 60;
    // the jump gauge: thrust rising over 2.5 g, a fall over 6 g
    const float jump = fx_float(f.jump_step);
    const float jump_target = jump >= 0 ? jump / (gravity_60 * 2.5f) : -jump / (gravity_60 * 6.0f);
    s.jump_gauge += std::clamp(jump_target - s.jump_gauge, -gauge_step, gauge_step);
    s.jump_gauge = std::clamp(s.jump_gauge, 0.0f, 1.0f);
    // the speed gauge: ground speed (retail's per update: twice a step's) over run_rate
    const float vx = fx_float(f.ground_velocity.x), vz = fx_float(f.ground_velocity.z);
    const float run = std::max(fx_float(f.run_rate), 1e-6f);
    const float speed_target = 2.0f * std::sqrt(vx * vx + vz * vz) / run;
    s.speed_gauge += std::clamp(speed_target - s.speed_gauge, -gauge_step, gauge_step);
    s.speed_gauge = std::clamp(s.speed_gauge, 0.0f, 1.0f);
}
