#include "input/menu_input.hpp"

// decomp: input_accept_pressed 0xDF270
bool menu_accept_pressed(const PadRecord& pad) {
    return pad_button_test(pad, PadEdge::Pressed, PadMatch::Any, pad_a | pad_start);
}

bool menu_cancel_pressed(const PadRecord& pad) {
    return pad_button_test(pad, PadEdge::Pressed, PadMatch::Any, pad_b | pad_back);
}

bool menu_start_pressed(const PadRecord& pad) {
    return pad_button_test(pad, PadEdge::Pressed, PadMatch::Any, pad_start);
}

bool menu_back_pressed(const PadRecord& pad) {
    return pad_button_test(pad, PadEdge::Pressed, PadMatch::Any, pad_back);
}

bool text_skip_held(const PadRecord& pad) {
    return pad_button_test(pad, PadEdge::Held, PadMatch::All,
                           pad_left_thumb | pad_right_thumb | pad_left_trigger | pad_right_trigger);
}

int menu_cursor_move(const PadRecord& pad, int& value, int lo, int count, unsigned flags) {
    const int hi = count == 0 ? lo : lo + count - 1;
    int old = value;
    int v = value;

    // which way is +1: down (or right); the d-pad and the sticks' directions share the bits
    // (1 up, 2 down, 4 left, 8 right)
    const std::uint16_t more = flags & cursor_horizontal ? pad_dpad_right : pad_dpad_down;
    const std::uint16_t less = flags & cursor_horizontal ? pad_dpad_left : pad_dpad_up;
    const PadEdge which = flags & cursor_repeat ? PadEdge::Repeat : PadEdge::Pressed;
    const auto step = [&](std::uint16_t bits) {
        if (bits & more) {
            ++v;
        }
        if (bits & less) {
            --v;
        }
    };
    if (!(flags & cursor_no_dpad)) {
        step(pad_bits(pad.b, which));
    }
    if (flags & cursor_left_stick) {
        step(pad_bits(pad.stick[0].dirs, which));
    }
    if (flags & cursor_right_stick) {
        step(pad_bits(pad.stick[1].dirs, which));
    }

    // wrap always, or only on a fresh press of a d-pad or stick the menu reads; otherwise stop
    // at the ends. Retail tests the d-pad's press even under cursor_no_dpad, so a d-pad tap
    // wraps a stick-held cursor (docs/bugs.md). Across a wrap `old` moves by count so the step
    // returned is still the direction pressed.
    const std::uint16_t both = more | less;
    const bool fresh = (!(flags & cursor_no_dpad) && (pad.b.rising & both) != 0) ||
                       ((flags & cursor_left_stick) && (pad.stick[0].dirs.rising & both) != 0) ||
                       ((flags & cursor_right_stick) && (pad.stick[1].dirs.rising & both) != 0);
    if (count > 1 && ((flags & cursor_wrap) || ((flags & cursor_wrap_press) && fresh))) {
        if (v < lo) {
            old += count;
            v = hi;
        } else if (v > hi) {
            old -= count;
            v = lo;
        }
    } else {
        v = v < lo ? lo : v;
        v = v > hi ? hi : v;
    }
    value = v;
    return v - old;
}

int menu_list_move(const PadRecord& pad, int& cursor, int& scroll, int lo, int total, int rows,
                   unsigned flags, int& moved) {
    // the entry under the cursor, wrapped into the list
    const auto entry = [total](int at, int top) {
        int pos = at + top;
        if (total > 0) {
            while (pos >= total) {
                pos -= total;
            }
        }
        return pos;
    };
    int at = cursor;
    int top = scroll;
    int pos = entry(at, top);
    moved = menu_cursor_move(pad, pos, lo, total, flags);
    if (moved == 0) {
        return pos;
    }

    // a list that fits wraps its cursor; a longer one holds it at the edge and scrolls
    int over = 0;
    at += moved;
    if (total <= rows) {
        if (at > total - 1) {
            at = 0;
        } else if (at < 0) {
            at = total - 1;
        }
    } else if (at > rows - 1) {
        over = at - rows + 1;
        at = rows - 1;
    } else if (at < 0) {
        over = at;
        at = 0;
    }
    cursor = at;
    if (over != 0) {
        top += over;
        while (top >= total) {
            top -= total;
        }
        while (top < 0) {
            top += total;
        }
        scroll = top;
    }
    return entry(at, top);
}
