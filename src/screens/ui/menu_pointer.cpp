#include "screens/ui/menu_pointer.hpp"

#include "screens/ui/ui_anchors.hpp"

#include <algorithm>

namespace {

// window coordinates to the 640x480 screen as the sprite layer fits it (4:3, bars)
void to_screen(SDL_Window* window, float wx, float wy, float& x, float& y) {
    int w = 1, h = 1;
    SDL_GetWindowSize(window, &w, &h);
    const float scale = std::min(static_cast<float>(w) / 640.0f, static_cast<float>(h) / 480.0f);
    const float ox = (static_cast<float>(w) - 640.0f * scale) * 0.5f;
    const float oy = (static_cast<float>(h) - 480.0f * scale) * 0.5f;
    x = (wx - ox) / scale;
    y = (wy - oy) / scale;
}

} // namespace

void pointer_event(MenuPointer& pointer, const SDL_Event& event, SDL_Window* window) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        to_screen(window, event.motion.x, event.motion.y, pointer.x, pointer.y);
        pointer.visible = true;
        pointer.pending_moved = true;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        to_screen(window, event.button.x, event.button.y, pointer.x, pointer.y);
        if (event.button.button == SDL_BUTTON_LEFT) {
            pointer.pending_left = pointer.visible;
            pointer.held = pointer.visible;
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            pointer.pending_right = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT) {
            to_screen(window, event.button.x, event.button.y, pointer.x, pointer.y);
            pointer.pending_released = pointer.held;
            pointer.held = false;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        pointer.pending_wheel += static_cast<int>(event.wheel.y > 0 ? 1 : event.wheel.y < 0 ? -1 : 0);
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        pointer.visible = false;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (event.gaxis.value > 16384 || event.gaxis.value < -16384) {
            pointer.visible = false;
        }
        break;
    default:
        break;
    }
}

void pointer_update(MenuPointer& pointer) {
    pointer.moved = pointer.pending_moved;
    pointer.left = pointer.pending_left;
    pointer.right = pointer.pending_right;
    pointer.wheel = pointer.pending_wheel;
    pointer.released = pointer.pending_released;
    pointer.pending_moved = pointer.pending_left = pointer.pending_right = false;
    pointer.pending_released = false;
    pointer.pending_wheel = 0;
}

bool pointer_inside(const MenuPointer& pointer, const PointerRect& in) {
    const PointerRect rect = anchor_rect(pointer.anchors, pointer.frame, in);
    return pointer.visible && pointer.x >= rect.left && pointer.x < rect.right &&
           pointer.y >= rect.top && pointer.y < rect.bottom;
}

int pointer_pick(const MenuPointer& pointer, const PointerRect* rects, int count) {
    if (!rects || !pointer.visible) {
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        if (pointer_inside(pointer, rects[i])) {
            return i;
        }
    }
    return -1;
}
