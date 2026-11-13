#pragma once
// Pieces the garage and shop screens share (decomp src/hand/u_0bd620.c, src/od/u_06c3e0.c,
// src/o1/u_03c51a.c): pop-in plates, slides, the menu wheels. The numbers, day and money, the
// garage banners, the A-button plate and glyph names are screen_header.hpp's.
#include "screens/ui/screen_header.hpp"
#include "screens/ui/ui.hpp"
#include "world/game_state.hpp"

#include <cstdint>

struct World;

// sprite_draw_at_if_valid (and sprite_draw_at): one sprite at x, y
void sprite_draw_at(Ui& ui, int id, float x, float y, int blend, std::uint32_t color = 0xffffffff,
                    std::uint32_t flags = 0, float z = 1.0f);
// decomp: task_timer_get 0xE0410
inline int task_timer_get(const Ui& ui) {
    return static_cast<int>(ui.task_timer);
}

// pop_scale_peak and box_pop_scale (peak 1.2, start 0.05)
void pop_scale_peak(float out[2], int rise, int end, int t, float peak, float start);
void box_pop_scale(float out[2], int rise, int end, int t);

// a Timer for timer_sprite_* (timer_sprite_init, _start, _pop_draw): a plate popping
// open, optionally moving from (a, b) to its place while it opens
struct PopTimer {
    int started = -1;
    int delay = 0, duration = 0;
    int sprite = -1;
    bool moves = false;
    int a = 0, b = 0;
    float c = 1, d = 1; // final scale
};
void pop_timer_init(PopTimer& t, int sprite, int delay, int duration);
void pop_timer_start(Ui& ui, PopTimer& t, float c, float d, bool moves, int a, int b);
// true while it runs
bool pop_timer_draw(Ui& ui, const PopTimer& t, int x, int y, int blend);

// sprite_slide_init / sprite_slide_draw: sprites sliding to targets,
// the step (target0 - x0) / (frames - age) from the first, sound 0x2C on age 1
struct Slide {
    int started = -1;
    int pos[10] = {};
};
void slide_init(Slide& slide, int count, const int* start);
inline void slide_start(Ui& ui, Slide& slide) {
    slide.started = task_timer_get(ui);
}
void slide_draw(Ui& ui, Slide& slide, int count, int frames, int blend, int y, const int* targets,
                const int* ids);

// the menu wheels (garage_menu_screen_draw, plus_tech_menu_draw,
// wild_machines_menu_draw): eight sprites turning about the pivot, `shown` the item at
// rest, turning to `cursor` over 8 frames
void menu_wheel_draw(Ui& ui, const int* ids, int count, int shown, int cursor, int frame);
// the wheel items' hit boxes for the pointer: the one at rest under the lit sprite only
PointerRect menu_wheel_rect(Ui& ui, int lit_id);
