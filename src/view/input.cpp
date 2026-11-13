#include "view/view.hpp"

#include <cmath>

namespace {

// mouse look: binary angle units a pixel (2^20, about 0.088 degrees)
constexpr float turn_per_pixel = 1048576.0f;

void set_key(LatchedInput& input, SDL_Scancode key, bool down) {
    switch (key) {
    case SDL_SCANCODE_W:
        input.forward = down;
        break;
    case SDL_SCANCODE_S:
        input.back = down;
        break;
    case SDL_SCANCODE_A:
        input.left = down;
        break;
    case SDL_SCANCODE_D:
        input.right = down;
        break;
    case SDL_SCANCODE_E:
    case SDL_SCANCODE_SPACE:
        input.up = down;
        break;
    case SDL_SCANCODE_Q:
    case SDL_SCANCODE_LCTRL:
        input.down = down;
        break;
    case SDL_SCANCODE_LSHIFT:
        input.fast = down;
        break;
    default:
        break;
    }
}

} // namespace

void handle_event(View& view, const SDL_Event& event, SDL_Window* window) {
    LatchedInput& input = view.input;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        set_key(input, event.key.scancode, event.type == SDL_EVENT_KEY_DOWN);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_RIGHT) {
            input.mouse_look = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            SDL_SetWindowRelativeMouseMode(window, input.mouse_look);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (input.mouse_look) {
            input.mouse_x += event.motion.xrel;
            input.mouse_y += event.motion.yrel;
        }
        break;
    default:
        break;
    }
}

Inputs build_inputs(View& view) {
    LatchedInput& input = view.input;
    Inputs inputs;
    inputs.move_forward = static_cast<std::int8_t>(input.forward - input.back);
    inputs.move_right = static_cast<std::int8_t>(input.right - input.left);
    inputs.move_up = static_cast<std::int8_t>(input.up - input.down);
    inputs.fast = input.fast;
    // mouse right turns right, which is toward -x from +z: a falling yaw
    inputs.turn_yaw = static_cast<std::int32_t>(std::lround(-input.mouse_x * turn_per_pixel));
    inputs.turn_pitch = static_cast<std::int32_t>(std::lround(-input.mouse_y * turn_per_pixel));
    input.mouse_x = input.mouse_y = 0;
    return inputs;
}
