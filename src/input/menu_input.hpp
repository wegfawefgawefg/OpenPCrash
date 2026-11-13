#pragma once
// What the original's screens ask of the pad (GenkiLib 0xDF060..0xDF7E0; decomp docs/port/
// screens.md "Cursors"), over the pad records read each step. Menus read player 0's record
// except in VS, where each player's screen part reads its own. A screen plays its cursor sound
// and stores its menu input time (`menu_input_mark`, which restarts the blinking) itself
// when a cursor moves; that is retail's order. No SDL.
#include "world/pad_record.hpp"

// A new press of A or START (0x110, input_accept_pressed), of B or BACK (0x220,
// input_cancel_pressed), of START alone (input_start_pressed), of BACK alone (input_back_pressed)
bool menu_accept_pressed(const PadRecord& pad);
bool menu_cancel_pressed(const PadRecord& pad);
bool menu_start_pressed(const PadRecord& pad);
bool menu_back_pressed(const PadRecord& pad);

// The conversations' fast-forward chord: both stick clicks and both triggers held together
// (0xC0C0, every bit; docs/port/progression.md "Commands"), unless the script's flags turn it off
bool text_skip_held(const PadRecord& pad);

// pad_cursor_move flags (pad_cursor_move)
inline constexpr unsigned cursor_horizontal = 0x1;  // left/right instead of up/down
inline constexpr unsigned cursor_repeat = 0x2;      // move on auto-repeat, not only new presses
inline constexpr unsigned cursor_wrap_press = 0x4;  // wrap only on a fresh press
inline constexpr unsigned cursor_wrap = 0x8;        // always wrap
inline constexpr unsigned cursor_left_stick = 0x10; // the left stick moves it too
inline constexpr unsigned cursor_right_stick = 0x20;
inline constexpr unsigned cursor_no_dpad = 0x40;
// the usual sets: 0x16 a vertical menu, 0x17 a horizontal one, 0x12 / 0x13 a vertical /
// horizontal value that stops at its ends, 0x1F a horizontal ring that wraps even when held
inline constexpr unsigned cursor_menu_vertical = 0x16;
inline constexpr unsigned cursor_menu_horizontal = 0x17;
inline constexpr unsigned cursor_value_vertical = 0x12;
inline constexpr unsigned cursor_value_horizontal = 0x13;
inline constexpr unsigned cursor_ring = 0x1F;

// Steps `value` within lo..lo+count-1 by the pad: down (or right) +1, up (or left) -1, wrapping
// or clamping by the flags. Returns the step taken (-1, 0 or 1; across a wrap still the
// direction moved). The screen plays its sound and marks its input time when it is not 0.
int menu_cursor_move(const PadRecord& pad, int& value, int lo, int count, unsigned flags);

// A scrolling list of `total` entries shown in `rows` rows (pad_list_cursor_move): `cursor` is the
// row lit, `scroll` the entry in the top row. A list that fits wraps its cursor; a longer one stops
// the cursor at the edge and scrolls round. Returns the entry under the cursor; `moved` gets the
// step.
int menu_list_move(const PadRecord& pad, int& cursor, int& scroll, int lo, int total, int rows,
                   unsigned flags, int& moved);
