#pragma once
// GenkiLib's pad reader (pads_read and its helpers 0xDECE0, 0xDED50, 0xDEE20, 0xDEF90),
// the part that does not touch a device: what one read of a pad turns into in its PadRecord.
// devices.cpp samples the PC's keyboard, mouse and pads into a PadSample and calls this once a
// step. No SDL.
#include "world/pad_record.hpp"

#include <cstdint>

// One read of a player's devices, already through the original's per-device dead zones (so the
// first device with a stick off centre can be told), merged into one pad
struct PadSample {
    bool connected = false;
    std::uint16_t buttons = 0;                  // the digital low byte: d-pad, start, back, thumbs
    std::uint8_t analog[pad_analog_count] = {}; // levels after pad_analog_level
    std::int16_t stick[2][2] = {};              // [left, right][x, y], after pad_stick_axis
};

// an analog button's 0..255 through the dead zone of 30 (genki::analog_button): 30 and under is 0
int pad_analog_level(int raw);
// a stick axis in XInput's sense (-32768..32767, y up) clamped to +-0x7FFF and through the dead
// zone of `dead` each side, the rest rescaled to full range (genki::stick_dead_zone). The
// original's zone is 0x4000, half the travel per axis, and the port's default (input/bindings.hpp).
// `outer` (the port's OUTER DEAD ZONE, 0 retail) is the travel at each end that already reads as
// full
inline constexpr int original_stick_dead_zone = 0x4000;
std::int16_t pad_stick_axis(int raw, int dead, int outer = 0);

// The read: previous word, new word, the analog buttons' bits at a quarter of their range, edges,
// auto-repeat, and each stick's directions with their own edges and repeat. `cadence` is the
// original update rate of the screen reading it (60, or 30 for battle-rate states), so the
// repeat keeps retail's wall-clock timing although the port reads every 60 Hz step.
void pad_record_read(PadRecord& pad, const PadSample& sample, int cadence);
// one read's auto-repeat for a button word whose cur and rising are already set (0xDEE20), and a
// stick's four directions as buttons with their edges and repeat from its x and y (0xDEF90); the
// pieces of a read the VS end menu runs again in the world (battle/vs_battle.cpp)
void pad_buttons_repeat(PadButtons& b, int delay, int cadence);
void pad_stick_directions(PadStick& stick, int cadence);
// pad_record_clear: the record to neutral but for `connected`, as for a pad not read
// (a fade, a lost-pad pause). A button still held at the next read comes back as a new press.
void pad_record_clear(PadRecord& pad);
