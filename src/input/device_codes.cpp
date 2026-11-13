#include "input/device_codes.hpp"

#include <SDL3/SDL.h>
#include <bit>
#include <gubsy/input/types.hpp>

namespace {

using B = GubsyButton;
using K = ginput::DeviceKind;

// gubsy's extended encoding (src/input_binding_utils.cpp): a flag, the kind, a device id (0x3FF
// for any) and a 16-bit code, where a mouse code is SDL_BUTTON_MASK of the button
constexpr int extended_flag = 1 << 29;
constexpr int kind_shift = 26;
constexpr int device_shift = 16;
constexpr int device_mask = 0x3FF;

// the keys that are not in a run of SDL scancodes, in GubsyButton order from KB_KP_PERIOD
constexpr SDL_Scancode loose_keys[] = {
    SDL_SCANCODE_KP_PERIOD,    SDL_SCANCODE_KP_DIVIDE,    SDL_SCANCODE_KP_MULTIPLY,
    SDL_SCANCODE_KP_MINUS,     SDL_SCANCODE_KP_PLUS,      SDL_SCANCODE_KP_ENTER,
    SDL_SCANCODE_KP_EQUALS,    SDL_SCANCODE_UP,           SDL_SCANCODE_DOWN,
    SDL_SCANCODE_LEFT,         SDL_SCANCODE_RIGHT,        SDL_SCANCODE_LSHIFT,
    SDL_SCANCODE_RSHIFT,       SDL_SCANCODE_LCTRL,        SDL_SCANCODE_RCTRL,
    SDL_SCANCODE_LALT,         SDL_SCANCODE_RALT,         SDL_SCANCODE_LGUI,
    SDL_SCANCODE_RGUI,         SDL_SCANCODE_ESCAPE,       SDL_SCANCODE_TAB,
    SDL_SCANCODE_CAPSLOCK,     SDL_SCANCODE_SPACE,        SDL_SCANCODE_BACKSPACE,
    SDL_SCANCODE_RETURN,       SDL_SCANCODE_INSERT,       SDL_SCANCODE_DELETE,
    SDL_SCANCODE_HOME,         SDL_SCANCODE_END,          SDL_SCANCODE_PAGEUP,
    SDL_SCANCODE_PAGEDOWN,     SDL_SCANCODE_PRINTSCREEN,  SDL_SCANCODE_SCROLLLOCK,
    SDL_SCANCODE_PAUSE,        SDL_SCANCODE_MINUS,        SDL_SCANCODE_EQUALS,
    SDL_SCANCODE_LEFTBRACKET,  SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_BACKSLASH,
    SDL_SCANCODE_SEMICOLON,    SDL_SCANCODE_APOSTROPHE,   SDL_SCANCODE_GRAVE,
    SDL_SCANCODE_COMMA,        SDL_SCANCODE_PERIOD,       SDL_SCANCODE_SLASH,
    SDL_SCANCODE_NUMLOCKCLEAR, SDL_SCANCODE_APPLICATION,  SDL_SCANCODE_MENU,
};
static_assert(sizeof(loose_keys) / sizeof(loose_keys[0]) ==
              static_cast<int>(B::MOUSE_LEFT) - static_cast<int>(B::KB_KP_PERIOD));

constexpr int mouse_buttons[] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE,
                                 SDL_BUTTON_X1, SDL_BUTTON_X2};

// GP_A..GP_TOUCHPAD, as gubsy maps them
constexpr SDL_GamepadButton pad_buttons[] = {
    SDL_GAMEPAD_BUTTON_SOUTH,          SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_WEST,           SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK,           SDL_GAMEPAD_BUTTON_GUIDE,
    SDL_GAMEPAD_BUTTON_START,          SDL_GAMEPAD_BUTTON_LEFT_STICK,
    SDL_GAMEPAD_BUTTON_RIGHT_STICK,    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN,      SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT,     SDL_GAMEPAD_BUTTON_MISC1,
    SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,  SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
    SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,  SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
    SDL_GAMEPAD_BUTTON_TOUCHPAD,
};
static_assert(sizeof(pad_buttons) / sizeof(pad_buttons[0]) ==
              static_cast<int>(B::COUNT) - static_cast<int>(B::GP_A));

bool between(int value, B first, B last) {
    return value >= static_cast<int>(first) && value <= static_cast<int>(last);
}

bool legacy(int button, DeviceCode& out) {
    out.device = ginput::any_device_id;
    out.kind = K::Keyboard;
    if (between(button, B::KB_A, B::KB_Z)) {
        out.code = SDL_SCANCODE_A + (button - static_cast<int>(B::KB_A));
    } else if (button == static_cast<int>(B::KB_0)) {
        out.code = SDL_SCANCODE_0;
    } else if (between(button, B::KB_1, B::KB_9)) {
        out.code = SDL_SCANCODE_1 + (button - static_cast<int>(B::KB_1));
    } else if (between(button, B::KB_F1, B::KB_F12)) {
        out.code = SDL_SCANCODE_F1 + (button - static_cast<int>(B::KB_F1));
    } else if (button == static_cast<int>(B::KB_KP_0)) {
        out.code = SDL_SCANCODE_KP_0;
    } else if (between(button, B::KB_KP_1, B::KB_KP_9)) {
        out.code = SDL_SCANCODE_KP_1 + (button - static_cast<int>(B::KB_KP_1));
    } else if (between(button, B::KB_KP_PERIOD, B::KB_MENU)) {
        out.code = loose_keys[button - static_cast<int>(B::KB_KP_PERIOD)];
    } else if (between(button, B::MOUSE_LEFT, B::MOUSE_X2)) {
        out.kind = K::Mouse;
        out.code = mouse_buttons[button - static_cast<int>(B::MOUSE_LEFT)];
    } else if (between(button, B::GP_A, B::GP_TOUCHPAD)) {
        out.kind = K::Gamepad;
        out.code = pad_buttons[button - static_cast<int>(B::GP_A)];
    } else {
        return false;
    }
    return true;
}

} // namespace

bool device_code_of(int encoded, DeviceCode& out) {
    if ((encoded & extended_flag) == 0) {
        return legacy(encoded, out);
    }
    const int kind = (encoded >> kind_shift) & 0x7;
    const int device = (encoded >> device_shift) & device_mask;
    const int code = encoded & 0xFFFF;
    out.kind = static_cast<K>(kind);
    out.device = device == device_mask ? ginput::any_device_id : device;
    out.code = code;
    if (out.kind == K::Mouse) {
        // the extended mouse code is a button mask; the lowest button in it
        if (code == 0) {
            return false;
        }
        out.code = std::countr_zero(static_cast<unsigned>(code)) + 1;
    }
    return out.kind == K::Keyboard || out.kind == K::Mouse || out.kind == K::Gamepad;
}
