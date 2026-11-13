#pragma once
// One player's pad as GenkiLib's `pads_read` leaves it each read: the button word with
// its edges and auto-repeat, the eight analog buttons, both sticks and each stick's four
// directions as buttons (decomp `PadRec`, 0x88 bytes at 0xAC6E48 per port,
// src/hand/genkilib_pad_o2.c). The input layer (src/input/pad_read.cpp) fills it once a step from
// the PC's devices; the world reads it only through `control_route` (world/control_route.hpp) and
// VS's end menu (battle/vs_battle.cpp), and menus through src/input/menu_input.hpp. No SDL here:
// it is part of the world's input.
//
// What the world reads of a record, and so all an online seat sends (docs/multiplayer.md "What
// goes over the wire"): `b.cur`, `b.rising` and both sticks' `x` and `y`, plus the seat's
// MouseAim. `connected`, `b.prev`, `b.falling`, `b.repeat`, `b.timer`, `analog` and the sticks'
// `dirs` are the menus' only; a world read of any of them would have to go on the wire too.
#include "fx/fx.hpp"

#include <cstdint>

// the button word: XInput's digital buttons in the low byte, the eight analog buttons above them
// from bit 8, each down at a quarter of its range (findings "Pad buttons and menu input")
inline constexpr std::uint16_t pad_dpad_up = 0x1;
inline constexpr std::uint16_t pad_dpad_down = 0x2;
inline constexpr std::uint16_t pad_dpad_left = 0x4;
inline constexpr std::uint16_t pad_dpad_right = 0x8;
inline constexpr std::uint16_t pad_start = 0x10;
inline constexpr std::uint16_t pad_back = 0x20;
inline constexpr std::uint16_t pad_left_thumb = 0x40;
inline constexpr std::uint16_t pad_right_thumb = 0x80;
inline constexpr std::uint16_t pad_a = 0x100;
inline constexpr std::uint16_t pad_b = 0x200;
inline constexpr std::uint16_t pad_x = 0x400;
inline constexpr std::uint16_t pad_y = 0x800;
inline constexpr std::uint16_t pad_black = 0x1000;
inline constexpr std::uint16_t pad_white = 0x2000;
inline constexpr std::uint16_t pad_left_trigger = 0x4000;
inline constexpr std::uint16_t pad_right_trigger = 0x8000;
// the analog buttons' order in `analog` and in the word from bit 8
// (XINPUT_GAMEPAD_A..RIGHT_TRIGGER)
inline constexpr int pad_analog_count = 8;
inline constexpr int pad_analog_first_bit = 8;

// a stick's directions as buttons (0xDEF90): past 0x3F either way after the dead zone
inline constexpr std::uint8_t stick_up = 0x1;
inline constexpr std::uint8_t stick_down = 0x2;
inline constexpr std::uint8_t stick_left = 0x4;
inline constexpr std::uint8_t stick_right = 0x8;

// players with a pad record (pads_count_set(4) in frame_cadence_init)
inline constexpr int max_pad_players = 4;

// One set of edge-detected buttons (GenkiLib `PadButtons`): the word before this read and now,
// what went down and up between them, and the auto-repeat word with its counter. `timer` counts
// the port's 60 Hz reads (retail counted its own reads; src/input/pad_read.cpp scales).
struct PadButtons {
    std::uint16_t prev = 0;
    std::uint16_t cur = 0;
    std::uint16_t rising = 0;
    std::uint16_t falling = 0;
    std::uint16_t repeat = 0;
    std::uint16_t timer = 0;
};

// A stick after `pads_read`'s clamp and dead zone: -0x7FFF..0x7FFF, y positive up (XInput's
// sense), and its directions as buttons
struct PadStick {
    std::int16_t x = 0;
    std::int16_t y = 0;
    PadButtons dirs;
};

struct PadRecord {
    bool connected = false; // retail `port`, the XInput handle: 0 for no pad
    PadButtons b;
    std::uint8_t analog[pad_analog_count] = {}; // levels 0..255 after the dead zone of 30
    PadStick stick[2];                          // 0 left, 1 right
};

// The mouse's share of a player's aim this step (no retail counterpart; docs/input.md, "Mouse
// aim"): the upper-body turn and the pitch as control axis values, 1 being the original's full
// stick rate. The input layer turns mouse motion into a target angle and the target into these
// per step; `control_route` adds them to the stick axes and holds the sum to the original's -1..1,
// so the mouse turns at the mech's own rate, exactly as a full stick (owner, 2026-11-11: no faster
// mouse in any mode).
struct MouseAim {
    Fx twist; // positive turns right, as the right stick does
    Fx pitch; // positive looks up, whatever the View Change setting
};

// which set `pad_button_test` looks at, and how
enum class PadEdge { Held = 0, Pressed = 1, Released = 2, Repeat = 3 };
enum class PadMatch { All = 0, Any = 1 };

inline std::uint16_t pad_bits(const PadButtons& b, PadEdge which) {
    switch (which) {
    case PadEdge::Held:
        return b.cur;
    case PadEdge::Pressed:
        return b.rising;
    case PadEdge::Released:
        return b.falling;
    case PadEdge::Repeat:
        return b.repeat;
    }
    return 0;
}

// every bit of mask (All) or any of them (Any) in the chosen set
// decomp: pad_button_test 0xDF060
inline bool pad_button_test(const PadRecord& pad, PadEdge which, PadMatch match,
                            std::uint16_t mask) {
    const std::uint16_t bits = pad_bits(pad.b, which) & mask;
    return match == PadMatch::All ? bits == mask : bits != 0;
}
