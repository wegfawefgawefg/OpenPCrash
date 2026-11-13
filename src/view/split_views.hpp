#pragma once
// Split screen's views (decomp docs/port/render.md "Views and cameras", battle.md "Split-screen
// VS"): viewport_layouts 0x25B920 by view count and viewports_from_mask. One view is the
// 576x448 safe rectangle; two are top and bottom strips of 576x220 in seat order; three and four
// are quarters of 284x220, each seat in its own quarter (a quarter no seat has stays empty).
#include <array>

struct SplitView {
    int seat = 0;
    // retail's rectangle in the 640x480 screen (where the view's HUD sprites are placed)
    float rect[4] = {32, 16, 576, 448};
    // where the port draws the view's 3D: the same, or with the 3D on the whole screen
    // (the default, Scenery::safe_rect off) that rectangle stretched from the safe one to it
    float drawn[4] = {0, 0, 640, 480};
};

struct SplitLayout {
    int view_count = 1; // Battle.view_count: the seats in the battle, 1..4
    int count = 0;      // views drawn
    std::array<SplitView, 4> views{};
};

// the layout for the seats in a battle (bit n: seat n has a fighter)
SplitLayout split_layout(unsigned seat_mask, bool safe_rect);
// the view a seat draws in, -1 none
int split_view_of(const SplitLayout& layout, int seat);
