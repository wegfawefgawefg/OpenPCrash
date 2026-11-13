#include "input/pad_read.hpp"

namespace {

// an analog button counts as down at a quarter of its limit (0xDECE0); the limit is always 0xFF
// (pad_record_clear), so at 63
constexpr int analog_down = 0xFF * 25 / 100;
// a stick direction counts past 0x3F after the dead zone (0xDEF90)
constexpr int stick_button_edge = 0x3F;

// Auto-repeat (0xDEE20). Retail counts reads: a fresh press repeats at once, a hold repeats after
// `frame_cadence * 20 / 60` reads (a third of a second at either rate) and then every `delay`
// reads (2 for the buttons, 1..4 for a stick by how far it leans). The port reads every 60 Hz
// step, so both counts are scaled by the steps one original read stands for.
void repeat_update(PadButtons& b, int delay, int cadence) {
    const int weight = cadence > 0 && cadence < 60 ? 60 / cadence : 1;
    const int limit = cadence * 20 / 60 * weight;
    b.repeat = 0;
    if (b.rising != 0) {
        b.timer = 0;
        b.repeat = b.rising;
    } else if (b.cur != 0) {
        b.timer = static_cast<std::uint16_t>(b.timer + 1);
        if (b.timer >= limit) {
            b.repeat = b.cur;
            b.timer = static_cast<std::uint16_t>(limit - delay * weight);
        }
    }
}

void edges(PadButtons& b) {
    const std::uint16_t changed = b.cur ^ b.prev;
    b.rising = changed & b.cur;
    b.falling = changed & b.prev;
}

// a stick's four directions as buttons, with a repeat that speeds up the further it leans
void stick_buttons(PadStick& stick, int cadence) {
    PadButtons& b = stick.dirs;
    b.prev = b.cur;
    b.cur = 0;
    // the lean past the edge on both axes. Retail sums it in a short, which wraps negative on a
    // hard diagonal and slows the repeat to every 3 or 4 reads (docs/bugs.md); summed whole, a
    // hard diagonal repeats every read like a hard lean on one axis
    int lean = 0;
    if (stick.x > stick_button_edge) {
        lean = stick.x - stick_button_edge;
        b.cur = stick_right;
    } else if (stick.x < -stick_button_edge) {
        lean = -stick_button_edge - stick.x;
        b.cur = stick_left;
    }
    if (stick.y > stick_button_edge) {
        lean += stick.y - stick_button_edge;
        b.cur |= stick_up;
    } else if (stick.y < -stick_button_edge) {
        lean += -stick_button_edge - stick.y;
        b.cur |= stick_down;
    }
    edges(b);
    const int left = 0x6000 - lean / 2;
    const int delay = (left > 0 ? left : 0) / 12289 + 1;
    repeat_update(b, delay, cadence);
}

} // namespace

void pad_buttons_repeat(PadButtons& b, int delay, int cadence) {
    repeat_update(b, delay, cadence);
}

void pad_stick_directions(PadStick& stick, int cadence) {
    stick_buttons(stick, cadence);
}

int pad_analog_level(int raw) {
    if (raw <= 0x1E) {
        return 0;
    }
    return raw > 0xFF ? 0xFF : raw;
}

std::int16_t pad_stick_axis(int raw, int dead, int outer) {
    int v = raw > 0x7FFF ? 0x7FFF : raw < -0x7FFF ? -0x7FFF : raw;
    if (v >= dead) {
        v -= dead;
    } else if (v <= -dead) {
        v += dead;
    } else {
        v = 0;
    }
    // what is left between the two zones spread over the whole range (retail: no outer zone)
    const int range = 0x7FFF - dead - outer;
    v = v > range ? range : v < -range ? -range : v;
    return static_cast<std::int16_t>(v * 0x7FFF / range);
}

void pad_record_read(PadRecord& pad, const PadSample& sample, int cadence) {
    if (!sample.connected) {
        pad_record_clear(pad);
        pad.connected = false;
        return;
    }
    pad.connected = true;

    // the word, the levels and the sticks
    pad.b.prev = pad.b.cur;
    pad.b.cur = sample.buttons;
    for (int i = 0; i < pad_analog_count; ++i) {
        pad.analog[i] = sample.analog[i];
    }
    for (int s = 0; s < 2; ++s) {
        pad.stick[s].x = sample.stick[s][0];
        pad.stick[s].y = sample.stick[s][1];
    }

    // analog buttons into the word, then edges and repeat (0xDECE0, 0xDED50, 0xDEF70)
    for (int i = 0; i < pad_analog_count; ++i) {
        if (pad.analog[i] >= analog_down) {
            pad.b.cur |= static_cast<std::uint16_t>(1u << (pad_analog_first_bit + i));
        }
    }
    edges(pad.b);
    repeat_update(pad.b, 2, cadence);

    // each stick's directions (0xDEF90)
    for (PadStick& stick : pad.stick) {
        stick_buttons(stick, cadence);
    }
}

// decomp: pad_record_clear 0xDEC80
void pad_record_clear(PadRecord& pad) {
    // everything but the port, which pads_read sets before it decides to read
    const bool connected = pad.connected;
    pad = PadRecord{};
    pad.connected = connected;
}
