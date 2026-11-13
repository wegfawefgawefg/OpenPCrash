#include "input/script_input.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

std::int16_t stick_value(float v) {
    const float clamped = v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
    return static_cast<std::int16_t>(std::lround(clamped * 32767.0f));
}

} // namespace

bool load_input_script(InputScript& script, const std::string& path, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "can't open " + path;
        return false;
    }
    std::string text;
    int number = 0;
    while (std::getline(in, text)) {
        ++number;
        const std::size_t hash = text.find('#');
        if (hash != std::string::npos) {
            text.resize(hash);
        }
        std::istringstream line(text);
        ScriptLine row;
        std::string buttons;
        if (!(line >> row.steps)) {
            continue; // blank
        }
        if (!(line >> row.stick[0][0] >> row.stick[0][1] >> row.stick[1][0] >> row.stick[1][1] >>
              buttons)) {
            error = path + ":" + std::to_string(number) + ": want steps lx ly rx ry buttons";
            return false;
        }
        row.buttons = static_cast<std::uint16_t>(std::stoul(buttons, nullptr, 16));
        script.lines.push_back(row);
    }
    return true;
}

int script_steps(const InputScript& script) {
    int steps = 0;
    for (const ScriptLine& line : script.lines) {
        steps += line.steps;
    }
    return steps;
}

PadRecord next_script_pad(InputScript& script) {
    while (script.line < static_cast<int>(script.lines.size()) &&
           script.done >= script.lines[static_cast<std::size_t>(script.line)].steps) {
        ++script.line;
        script.done = 0;
    }
    PadRecord pad;
    pad.connected = true;
    std::uint16_t buttons = 0;
    if (script.line < static_cast<int>(script.lines.size())) {
        const ScriptLine& row = script.lines[static_cast<std::size_t>(script.line)];
        buttons = row.buttons;
        for (int s = 0; s < 2; ++s) {
            pad.stick[s].x = stick_value(row.stick[s][0]);
            pad.stick[s].y = stick_value(row.stick[s][1]);
        }
        ++script.done;
    }
    pad.b.prev = script.previous;
    pad.b.cur = buttons;
    pad.b.rising = static_cast<std::uint16_t>(buttons & ~script.previous);
    pad.b.falling = static_cast<std::uint16_t>(script.previous & ~buttons);
    script.previous = buttons;
    return pad;
}
