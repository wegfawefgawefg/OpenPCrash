#pragma once
// The mouse in the original's menus, which are pad-only: the screens keep the original's cursor
// (an index moved by pad_cursor_move, confirm, back) and the mouse drives that same cursor.
// Hovering an item moves the cursor to it with the screen's d-pad sound, a left click confirms,
// a right click goes back (Escape and Backspace are the pad's Back through the bindings), the
// wheel steps lists (bound to the d-pad) and value sliders. Hit boxes are the items' layout
// rectangles (their sprites at the layout table's positions). The pointer shows once the mouse
// moves and hides on a key or a pad button. Lists and grids keep this cursor model, so dragging
// can be added later without changing a screen's logic.
#include "render/sprite_frame.hpp"

#include <SDL3/SDL.h>

struct AnchorScreen;

// a rectangle in 640x480 screen pixels; the sprite drawn there (sprite_rect's), so a hit box moves
// with its art in a wide window (ui_anchors.hpp)
struct PointerRect {
    float left = 0, top = 0, right = 0, bottom = 0;
    int sprite = -1;
};

struct MenuPointer {
    float x = -1, y = -1; // 640x480 pixels
    bool visible = false;
    // this update's
    bool moved = false;
    bool left = false;  // a left click
    bool right = false; // a right click
    int wheel = 0;      // notches, away from the player positive
    bool held = false;     // the left button is down (a drag)
    bool released = false; // the left button came up this update
    // gathered from events since the last update
    bool pending_moved = false, pending_left = false, pending_right = false;
    bool pending_released = false;
    int pending_wheel = 0;
    // the item hovering last picked, by its list: hovering selects only on entering another item,
    // so the wheel or keys can move the cursor off the item the pointer rests on
    const void* hover_list = nullptr;
    int hover_item = -1;
    // the screen's widescreen anchors and frame (set each update): hit boxes move as their art
    const AnchorScreen* anchors = nullptr;
    SpriteFrame frame;
};

// every SDL event: the position mapped through the 4:3 fit (outside 0..640 beside the box in a
// wide window), clicks, the wheel, and hiding on keys and pad buttons
void pointer_event(MenuPointer& pointer, const SDL_Event& event, SDL_Window* window);
// once an update: what the events gathered becomes this update's
void pointer_update(MenuPointer& pointer);
// the pointer over a rectangle, moved with its anchor in a wide window
bool pointer_inside(const MenuPointer& pointer, const PointerRect& rect);
// a sprite's rectangle less its see-through margins (left, top, right, bottom): the art the mouse
// should hit, where neighbours' rectangles overlap or leave dead strips
inline PointerRect pointer_rect_inset(PointerRect r, float left, float top, float right,
                                      float bottom) {
    return {r.left + left, r.top + top, r.right - right, r.bottom - bottom, r.sprite};
}
// the first rectangle under the pointer, -1 none
int pointer_pick(const MenuPointer& pointer, const PointerRect* rects, int count);
