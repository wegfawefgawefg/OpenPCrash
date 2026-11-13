#include "input/virtual_pads.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

std::vector<SDL_Joystick*> pads;

struct Named {
    const char* name;
    int button; // SDL_GamepadButton, or -1 for an axis
    int axis;
    int value;
};

constexpr Named names[] = {
    {"a", SDL_GAMEPAD_BUTTON_SOUTH, 0, 0},
    {"b", SDL_GAMEPAD_BUTTON_EAST, 0, 0},
    {"x", SDL_GAMEPAD_BUTTON_WEST, 0, 0},
    {"y", SDL_GAMEPAD_BUTTON_NORTH, 0, 0},
    {"start", SDL_GAMEPAD_BUTTON_START, 0, 0},
    {"back", SDL_GAMEPAD_BUTTON_BACK, 0, 0},
    {"up", SDL_GAMEPAD_BUTTON_DPAD_UP, 0, 0},
    {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0, 0},
    {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0, 0},
    {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0, 0},
    {"lb", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0, 0},
    {"rb", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0, 0},
    {"ls", SDL_GAMEPAD_BUTTON_LEFT_STICK, 0, 0},
    {"rs", SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0, 0},
    {"lt", -1, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767},
    {"rt", -1, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767},
    {"lx+", -1, SDL_GAMEPAD_AXIS_LEFTX, 32767},
    {"lx-", -1, SDL_GAMEPAD_AXIS_LEFTX, -32768},
    // SDL's y is down positive
    {"ly+", -1, SDL_GAMEPAD_AXIS_LEFTY, -32768},
    {"ly-", -1, SDL_GAMEPAD_AXIS_LEFTY, 32767},
    {"rx+", -1, SDL_GAMEPAD_AXIS_RIGHTX, 32767},
    {"rx-", -1, SDL_GAMEPAD_AXIS_RIGHTX, -32768},
    {"ry+", -1, SDL_GAMEPAD_AXIS_RIGHTY, -32768},
    {"ry-", -1, SDL_GAMEPAD_AXIS_RIGHTY, 32767},
};

} // namespace

void virtual_pads_attach() {
    const char* count = std::getenv("PHANTOMCRASH_PADS");
    for (int i = 0; count && i < std::atoi(count) && i < 4; ++i) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "Virtual pad";
        const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        if (id == 0) {
            std::fprintf(stderr, "virtual pad: %s\n", SDL_GetError());
            continue;
        }
        pads.push_back(SDL_OpenJoystick(id));
        // triggers rest at the bottom of their travel
        SDL_SetJoystickVirtualAxis(pads.back(), SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
        SDL_SetJoystickVirtualAxis(pads.back(), SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
        std::printf("virtual pad %d: joystick %u\n", i, static_cast<unsigned>(id));
    }
}

void virtual_pads_frame(int frame) {
    // PHANTOMCRASH_MOUSE_MOVE="F:DX:DY,...": mouse motion (counts, right and down) at frame F
    if (const char* moves = std::getenv("PHANTOMCRASH_MOUSE_MOVE")) {
        const std::string text = moves;
        for (std::size_t from = 0; from < text.size();) {
            const std::size_t comma = std::min(text.find(',', from), text.size());
            int at = 0;
            float dx = 0, dy = 0;
            if (std::sscanf(text.substr(from, comma - from).c_str(), "%d:%f:%f", &at, &dx, &dy) ==
                    3 &&
                at == frame) {
                SDL_Event event{};
                event.type = SDL_EVENT_MOUSE_MOTION;
                event.motion.xrel = dx;
                event.motion.yrel = dy;
                SDL_PushEvent(&event);
            }
            from = comma + 1;
        }
    }
    // PHANTOMCRASH_KEY_PRESS="F:KEY,...": a key tapped at frame F (SDL's key names: Return,
    // Escape, Down...); the input layer latches it as one read held
    if (const char* keys = std::getenv("PHANTOMCRASH_KEY_PRESS")) {
        const std::string text = keys;
        for (std::size_t from = 0; from < text.size();) {
            const std::size_t comma = std::min(text.find(',', from), text.size());
            int at = 0;
            char name[32] = {};
            if (std::sscanf(text.substr(from, comma - from).c_str(), "%d:%31[^,]", &at, name) == 2 &&
                at == frame) {
                SDL_Event event{};
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.scancode = SDL_GetScancodeFromName(name);
                event.key.down = true;
                SDL_PushEvent(&event);
                event.type = SDL_EVENT_KEY_UP;
                event.key.down = false;
                SDL_PushEvent(&event);
            }
            from = comma + 1;
        }
    }
    // PHANTOMCRASH_PAD_UNPLUG="F:P,...": virtual pad P pulled out at frame F
    if (const char* unplug = std::getenv("PHANTOMCRASH_PAD_UNPLUG")) {
        const std::string text = unplug;
        for (std::size_t from = 0; from < text.size();) {
            const std::size_t comma = std::min(text.find(',', from), text.size());
            int at = 0, pad = 0;
            if (std::sscanf(text.substr(from, comma - from).c_str(), "%d:%d", &at, &pad) == 2 &&
                at == frame && pad >= 0 && pad < static_cast<int>(pads.size()) &&
                pads[static_cast<std::size_t>(pad)]) {
                SDL_Joystick*& joystick = pads[static_cast<std::size_t>(pad)];
                const SDL_JoystickID id = SDL_GetJoystickID(joystick);
                SDL_CloseJoystick(joystick);
                joystick = nullptr;
                SDL_DetachVirtualJoystick(id);
                std::printf("virtual pad %d: pulled out\n", pad);
            }
            from = comma + 1;
        }
    }
    // PHANTOMCRASH_PAD_SPIN="F:P:N[:REACH[:X:Y]]": virtual pad P's sticks as a worn pad's, resting
    // at X, Y of the travel and reaching only REACH of it; from frame F for N frames both turn
    // round their edge twice and the triggers pull to REACH, then for N/2 frames they lean to the
    // edge up and right (STICK CALIBRATION's captures)
    if (const char* spin = std::getenv("PHANTOMCRASH_PAD_SPIN")) {
        int at = 0, pad = 0, frames = 1;
        float reach = 1, ox = 0, oy = 0;
        if (std::sscanf(spin, "%d:%d:%d:%f:%f:%f", &at, &pad, &frames, &reach, &ox, &oy) >= 3 &&
            pad >= 0 && pad < static_cast<int>(pads.size()) &&
            pads[static_cast<std::size_t>(pad)] && frames > 0) {
            const int t = frame - at;
            float x = ox, y = oy, pull = 0;
            if (t >= 0 && t < frames) {
                const float turn = 12.566371f * static_cast<float>(t) / static_cast<float>(frames);
                x += reach * std::cos(turn);
                y += reach * std::sin(turn);
                pull = reach;
            } else if (t >= frames && t < frames + frames / 2) {
                x += reach * 0.7071f;
                y -= reach * 0.7071f;
                pull = reach;
            }
            const auto raw = [](float v) {
                return static_cast<Sint16>(std::clamp(v * 32767.0f, -32768.0f, 32767.0f));
            };
            SDL_Joystick* joystick = pads[static_cast<std::size_t>(pad)];
            for (const auto& [ax, ay] :
                 {std::pair{SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY},
                  std::pair{SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY}}) {
                SDL_SetJoystickVirtualAxis(joystick, ax, raw(x));
                SDL_SetJoystickVirtualAxis(joystick, ay, raw(y));
            }
            // a virtual trigger's axis runs -32768 (rest) to 32767
            for (const SDL_GamepadAxis trigger :
                 {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}) {
                SDL_SetJoystickVirtualAxis(joystick, trigger, raw(pull * 2.0f - 1.0f));
            }
        }
    }
    const char* script = std::getenv("PHANTOMCRASH_PAD_PRESS");
    if (!script || pads.empty()) {
        return;
    }
    const std::string text = script;
    for (std::size_t from = 0; from < text.size();) {
        const std::size_t comma = std::min(text.find(',', from), text.size());
        const std::string one = text.substr(from, comma - from);
        from = comma + 1;
        int at = 0, pad = 0, frames = 1;
        char name[16] = {};
        if (std::sscanf(one.c_str(), "%d:%d:%15[^:]:%d", &at, &pad, name, &frames) < 3 || pad < 0 ||
            pad >= static_cast<int>(pads.size()) || (frame != at && frame != at + frames)) {
            continue;
        }
        const bool down = frame == at;
        for (const Named& n : names) {
            if (std::strcmp(n.name, name) != 0) {
                continue;
            }
            SDL_Joystick* joystick = pads[static_cast<std::size_t>(pad)];
            if (!joystick) {
                break; // pulled out
            }
            if (n.button >= 0) {
                SDL_SetJoystickVirtualButton(joystick, n.button, down);
            } else {
                const bool trigger = n.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
                                     n.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                SDL_SetJoystickVirtualAxis(joystick, n.axis,
                                           static_cast<Sint16>(down ? n.value
                                                               : trigger ? -32768
                                                                         : 0));
            }
        }
    }
}
