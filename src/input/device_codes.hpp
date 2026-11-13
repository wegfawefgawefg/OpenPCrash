#pragma once
// A bound control as the SDL code an event carries, so a key, mouse button or pad button that
// went down and up between two reads can still be matched to its binding (devices.cpp). Gubsy
// keeps its own table private (src/input_binding_utils.cpp, kLegacyButtonMap); this is the same
// mapping.
#include <ginput/types.hpp>

struct DeviceCode {
    ginput::DeviceKind kind = ginput::DeviceKind::Keyboard;
    int device = ginput::any_device_id; // a pad's SDL joystick id, or any
    int code = 0; // SDL_Scancode, SDL mouse button number (1 left..5 X2), SDL_GamepadButton
};

// a binding's encoded control (a GubsyButton, or gubsy's extended encoding) as an SDL code
bool device_code_of(int encoded, DeviceCode& out);
