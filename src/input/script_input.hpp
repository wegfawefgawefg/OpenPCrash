#pragma once
// --script FILE: player 1's pad for headless runs, read in place of the devices, so a capture or
// a --trace run can drive the fighter the same way every time. Not a test: a way to see a move.
//
// One line per held input: `steps lx ly rx ry buttons`, sticks -1..1 (y up), buttons the pad word
// in hex (world/pad_record.hpp: 0x100 A, the default jump; 0x40 the left thumb, dash; 0x8000 the
// right trigger, arm A; 0x2000 white, the left step...). `#` starts a comment. Edges come from
// one line to the next, so a press is a line with the bit after one without it.
#include "world/pad_record.hpp"

#include <string>
#include <vector>

struct ScriptLine {
    int steps = 0;
    float stick[2][2] = {};
    std::uint16_t buttons = 0;
};

struct InputScript {
    std::vector<ScriptLine> lines;
    int line = 0;      // the line playing
    int done = 0;      // steps of it played
    std::uint16_t previous = 0;
};

bool load_input_script(InputScript& script, const std::string& path, std::string& error);
// the steps the whole script runs
int script_steps(const InputScript& script);
// the next step's pad (neutral after the end)
PadRecord next_script_pad(InputScript& script);
