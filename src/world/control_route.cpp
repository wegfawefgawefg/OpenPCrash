#include "world/control_route.hpp"

namespace {

// stick_dodge_window: 5 updates at 30 Hz (sv_control_route), 10 steps at 60
// (docs/sixty.md, "stick double flick", R12 D)
constexpr std::int32_t flick_window = 10;

// a stick value s (-0x7FFF..0x7FFF) as retail's float s / 32767 against a threshold num/den,
// done in integers so the test is exact: |s| / 32767 > num / den
bool stick_past(int s, int num, int den) {
    return static_cast<std::int64_t>(s) * den > static_cast<std::int64_t>(32767) * num;
}
bool stick_below(int s, int num, int den) {
    return static_cast<std::int64_t>(s) * den < -static_cast<std::int64_t>(32767) * num;
}
bool stick_inside(int s, int num, int den) {
    const std::int64_t m = s < 0 ? -static_cast<std::int64_t>(s) : s;
    return m * den < static_cast<std::int64_t>(32767) * num;
}

// s / 32767 through the 0.01 dead zone, rescaled so the rest still reaches 1:
// (|s| / 32767 - 0.01) / 0.99 = (100 |s| - 32767) / (99 * 32767), nearest, sign kept
Fx stick_axis(int s) {
    const std::int64_t m = s < 0 ? -static_cast<std::int64_t>(s) : s;
    if (m * 100 < 32767) {
        return Fx{};
    }
    const std::int64_t raw = div_round((m * 100 - 32767) * fx_one_raw, 99 * 32767);
    return fx_saturate(s < 0 ? -raw : raw);
}

// the same dead zone on a value already in the record (the twist kept in the air):
// (|a| - 0.01) / 0.99 = (100 |a| - 1) / 99
Fx axis_dead_zone(Fx a) {
    const std::int64_t m = a.raw < 0 ? -static_cast<std::int64_t>(a.raw) : a.raw;
    if (m * 100 < fx_one_raw) {
        return Fx{};
    }
    const std::int64_t raw = div_round(m * 100 - fx_one_raw, 99);
    return fx_saturate(a.raw < 0 ? -raw : raw);
}

// a mouse share added to an axis; the sum stays in the stick's -1..1
void add_aim(Fx& axis, Fx aim) {
    if (aim.raw == 0) {
        return;
    }
    std::int64_t raw = static_cast<std::int64_t>(axis.raw) + aim.raw;
    raw = raw > fx_one_raw ? fx_one_raw : raw < -fx_one_raw ? -fx_one_raw : raw;
    axis = fx_saturate(raw);
}

bool held(const PadRecord& pad, std::uint16_t mask) {
    return mask != 0 && pad_button_test(pad, PadEdge::Held, PadMatch::Any, mask);
}
bool pressed(const PadRecord& pad, std::uint16_t mask) {
    return mask != 0 && pad_button_test(pad, PadEdge::Pressed, PadMatch::Any, mask);
}

// the double flick under Standard: out past half, back under half, out again the same way, each
// within the window; a flick right is the right step (0x10), a flick left the left step
std::uint32_t step_flick(StickFlick& flick, int x) {
    std::uint32_t buttons = 0;
    const bool right = stick_past(x, 1, 2);
    const bool left = stick_below(x, 1, 2);
    const bool centre = stick_inside(x, 1, 2);
    const auto go = [&flick](std::int32_t state) {
        flick.state = state;
        flick.window = flick_window;
    };
    switch (flick.state) {
    case 0:
        if (right) {
            go(1);
        } else if (left) {
            go(3);
        }
        break;
    case 1:
        if (centre) {
            go(2);
        } else if (left) {
            go(3);
        }
        break;
    case 2:
        if (right) {
            buttons |= control_step_right;
            go(0);
        } else if (left) {
            go(3);
        }
        break;
    case 3:
        if (centre) {
            go(4);
        } else if (right) {
            go(1);
        }
        break;
    case 4:
        if (left) {
            buttons |= control_step_left;
            go(0);
        } else if (right) {
            go(1);
        }
        break;
    default:
        break;
    }
    flick.window -= 1;
    if (flick.window < 0) {
        flick.window = flick_window;
        flick.state = 0;
    }
    return buttons;
}

} // namespace

ControlBindings control_bindings_from_keys(const std::array<int, 8>& keys) {
    // actor_init ORs sv_trigger_bit_of(key) into each mask; an index out of 0..7 gives nothing
    const auto bits = [&keys](KeyAction action) -> std::uint16_t {
        const int key = keys[static_cast<std::size_t>(action)];
        return key >= 0 && key < 8 ? key_index_bits[static_cast<std::size_t>(key)] : 0;
    };
    ControlBindings bindings;
    bindings.fire[0] = bits(KeyAction::FireArmA);
    bindings.fire[1] = bits(KeyAction::FireArmB);
    bindings.fire[2] = bits(KeyAction::FireBackA);
    bindings.fire[3] = bits(KeyAction::FireBackB);
    bindings.dodge_a = bits(KeyAction::StepRight);
    bindings.dodge_b = bits(KeyAction::StepLeft);
    bindings.jump = bits(KeyAction::Jump);
    bindings.camo = bits(KeyAction::Camo);
    return bindings;
}

// decomp: sv_control_route 0x7742D
void control_route(ControlRecord& record, const PadRecord& pad, const MouseAim& aim,
                   const ControlBindings& bindings, const RouteFighter& fighter,
                   StickFlick& flick) {
    // in the door, in a scene, at a VS round's end or dead: nothing reaches the fighter
    record.control_buttons = 0;
    if (fighter.blocked) {
        record = ControlRecord{};
        return;
    }

    // sticks to the four axes by control type; in the air a Standard player turns with either
    // stick (the right one wins once it is off centre) and the twist keeps last step's value
    const PadStick& left = pad.stick[0];
    const PadStick& right = pad.stick[1];
    int axis[4] = {left.y, left.x, right.y, right.x};
    bool twist_kept = false;
    if (fighter.control_type & control_type_standard) {
        if (fighter.airborne) {
            axis[axis_leg_turn] = stick_inside(right.x, 1, 1000) ? left.x : right.x;
            twist_kept = true;
        } else {
            axis[axis_leg_turn] = right.x;
            axis[axis_twist] = left.x;
        }
        if (!(fighter.control_type & control_type_no_flick)) {
            record.control_buttons |= step_flick(flick, left.x);
        }
    }
    if (fighter.control_type & control_type_view_normal) {
        axis[axis_pitch] = -axis[axis_pitch];
    }
    for (int i = 0; i < 4; ++i) {
        record.control_axis[i] = stick_axis(axis[i]);
    }
    if (twist_kept) {
        record.control_axis[axis_twist] = axis_dead_zone(record.control_axis[axis_twist]);
    }

    // the mouse: on the axis the right stick's x drives, and the pitch (actor_camera_input
    // lowers the view for a positive axis, so up is subtracted)
    const int turn = fighter.control_type & control_type_standard ? axis_leg_turn : axis_twist;
    add_aim(record.control_axis[turn], aim.twist);
    add_aim(record.control_axis[axis_pitch], fx_raw(-aim.pitch.raw));

    // buttons: levels from what is held, edges from what went down this read
    std::uint32_t& buttons = record.control_buttons;
    if (held(pad, bindings.button1)) {
        buttons |= control_dash;
    }
    if (held(pad, bindings.jump)) {
        buttons |= control_jump_held;
    }
    if (pressed(pad, bindings.jump)) {
        buttons |= control_jump_pressed;
    }
    if (held(pad, bindings.dodge_a)) {
        buttons |= control_step_right;
    }
    if (held(pad, bindings.dodge_b)) {
        buttons |= control_step_left;
    }
    for (int i = 0; i < 4; ++i) {
        if (held(pad, bindings.fire[i])) {
            buttons |= mount_fire_bits[i];
        }
    }
    if (pressed(pad, bindings.camo)) {
        buttons |= control_camouflage;
    }
    if (pressed(pad, bindings.dpad_up)) {
        buttons |= control_view_near;
    }
    if (pressed(pad, bindings.dpad_down)) {
        buttons |= control_view_far;
    }
    if (pressed(pad, pad_dpad_right)) {
        buttons |= control_view_right;
    }
    if (pressed(pad, pad_dpad_left)) {
        buttons |= control_view_left;
    }
    if (pad_button_test(pad, PadEdge::Held, PadMatch::All, pad_right_thumb)) {
        buttons |= control_recentre;
    }
}
