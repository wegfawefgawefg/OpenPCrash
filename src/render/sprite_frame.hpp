#pragma once
// Where the 2D's 640x480 screen sits in a window of any shape (docs/screens.md "Widescreen"). The
// screen is fitted 4:3 into the middle of the window (the box); a wide window reaches `spread`
// screen pixels past the box on each side. Edge-anchored 2D moves out by `edge` (spread times PC
// CONFIG's HUD WIDTH), backgrounds and bars reach the window's edges. A split-screen view's 2D is
// centred on the view's drawn 3D and spreads over its width the same way. Pure arithmetic, shared
// by the sprite layer, the HUD's projection of world points and the menus' mouse.
#include <algorithm>

struct SpriteFrame {
    float scale = 1;      // window pixels per screen pixel
    float ox = 0, oy = 0; // the window position of the screen's (0, 0)
    float spread = 0;     // screen pixels the frame reaches past the box on each side
    float shift = 0;      // a view's 2D moved across to stay centred on its 3D, screen pixels
    float edge = 0;       // how far left- and right-anchored 2D moves out, screen pixels
};

// a window of width x height; fit_43 (ASPECT 4:3) keeps everything in the box. drawn: a split
// view's 3D in the 640x480 screen (null for the whole screen); hud_width 0..100 percent
inline SpriteFrame sprite_frame(int width, int height, bool fit_43, int hud_width,
                                const float* drawn = nullptr) {
    SpriteFrame f;
    const float tw = static_cast<float>(std::max(width, 1));
    const float th = static_cast<float>(std::max(height, 1));
    f.scale = std::min(tw / 640.0f, th / 480.0f);
    f.ox = (tw - 640.0f * f.scale) * 0.5f;
    f.oy = (th - 480.0f * f.scale) * 0.5f;
    if (fit_43) {
        return f;
    }
    // the drawn 3D's width and centre as the 3D stretches it over the window, and as the box
    // would place it
    const float whole[4] = {0, 0, 640, 480};
    const float* d = drawn ? drawn : whole;
    const float wide_w = d[2] * tw / 640.0f, box_w = d[2] * f.scale;
    const float wide_c = (d[0] + d[2] * 0.5f) * tw / 640.0f;
    const float box_c = f.ox + (d[0] + d[2] * 0.5f) * f.scale;
    f.spread = std::max(0.0f, (wide_w - box_w) * 0.5f / f.scale);
    f.shift = (wide_c - box_c) / f.scale;
    f.edge = f.spread * static_cast<float>(std::clamp(hud_width, 0, 100)) / 100.0f;
    return f;
}

// how far past the box's title-safe edges (32 and 608) the unclipped 2D may draw: none at 4:3, the
// whole window once it is wider than the box's border
inline float sprite_frame_reach(const SpriteFrame& f) {
    return f.spread + std::min(f.spread, 32.0f);
}
inline float sprite_frame_reach_y(const SpriteFrame& f) {
    return std::min(f.spread, 16.0f);
}
