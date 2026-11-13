#pragma once
// What the HUD's files share: where the drawn 3D is in the 640x480 sprite screen, the HudElem
// draws, color_blink, the number font, and each file's draw entry.
#include "hud/hud.hpp"

#include "ai/ai_links.hpp"
#include "world/world.hpp"

#include <cstdint>

// The screen the HUD draws on. Retail places screen-unit things (the reticle, boxes, marks,
// senses) at x = (sx + 1) W / 2 + 32, y = (1 - sy) H / 2 + 16 with W x H the HUD's viewport
// (576 x 448 in one view; hud.md "Coordinates"). The world's screen units are retail's (its
// projection, 52 degrees over 576 x 448); the port may draw the 3D larger than the safe rectangle,
// so a unit spans `unit` sprite pixels about the drawn 3D's centre: retail's W/2, H/2 times the
// drawn 3D's height over H (the same vertical field of view), 1 in the safe rectangle.
struct HudFrame {
    float cx = 320, cy = 240;         // the drawn 3D's centre, sprite pixels
    float unit_x = 288, unit_y = 224; // sprite pixels a screen unit spans
    float scale_x = 0.9f, scale_y = 448.0f / 480.0f; // a 640x480 offset's scale (W/640, H/480)
    float sprite_scale = 1;           // reticle sprites: 0.5 in any split screen
    float alpha = 1;                  // between the last two steps
    int view_count = 1;
    // PC CONFIG's FIELD OF VIEW: the world's units (its 52 degrees) times this are the drawn
    // view's, about its centre (fight_fov_unit, view/scene_camera.hpp); 1 at 52
    float world = 1;
};

// units to sprite pixels; the world's screen x is mirrored with the port's frame
inline float hud_px(const HudFrame& f, float ux) {
    return f.cx + ux * f.unit_x;
}
inline float hud_py(const HudFrame& f, float uy) {
    return f.cy - uy * f.unit_y;
}
// a world screen point (the reticle's centre, the tracking point, a sense) to sprite pixels in
// the drawn view
inline float hud_wx(const HudFrame& f, float ux) {
    return hud_px(f, ux * f.world);
}
inline float hud_wy(const HudFrame& f, float uy) {
    return hud_py(f, uy * f.world);
}
inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

// the battle's view count as the layouts take it: 1, 2, or 3 for three and four
inline int hud_views(const HudView& hud) {
    return hud.view_count <= 1 ? 1 : hud.view_count == 2 ? 2 : 3;
}
const HudLayout& hud_layout(const HudView& hud);

// HudElem::draw / draw_alpha / draw_either's 2D side 0x4D25B..: the row at its place plus
// origin, x scaled, flags 4, alpha on the colour's alpha; nothing for sprite -1 or scale 0
void hud_row_draw(Ui& ui, const HudRow& row, float scale_x = 1, float alpha = 1, float dx = 0,
                  float dy = 0, std::uint32_t color = 0, bool use_color = false);
// HudElem::draw_scale: scaled about its middle
void hud_row_draw_scaled(Ui& ui, const HudRow& row, float scale);
// a registry sprite with retail's Sprite fields (flags 4 | extra)
void hud_sprite(Ui& ui, int id, float x, float y, float scale_x, float scale_y, float turn,
                int blend, std::uint32_t color, std::uint32_t flags);
// color_blink: from `from` toward `to` and back over `frames` ticks of the task timer
// (retail overshoots past `from` on the way back; docs/bugs.md)
std::uint32_t color_blink(const HudView& hud, int frames, std::uint32_t from, std::uint32_t to);

// number_draw's flags
inline constexpr std::uint32_t number_commas = 0x4; // a comma every three digits
inline constexpr std::uint32_t number_right = 0x8;  // right-aligned at x, else left-aligned
inline constexpr std::uint32_t number_zeros = 0x10; // leading zeroes kept
// number_draw in number font 3 (font_record(3)); `scale` multiplies the value (the money's x 10)
void hud_number(Ui& ui, std::uint32_t flags, long long value, int x, int y, int blend,
                std::uint32_t color, int digits, int scale);
// glyph_draw_advance of font 3's row (12 comma, 15 the time separator): drawn at x,
// then x moves on
void hud_glyph(Ui& ui, int row, int& x, int y, int blend, std::uint32_t color);
int hud_font_advance(Ui& ui, int row);

// hud_readout.cpp: hud_draw_readout (the 2D readout, warnings, signs, popups, counters)
void hud_draw_readout(HudView& hud, Ui& ui, const World& world, const HudFrame& frame);
// hud_gauges.cpp: hud_draw_durability_and_camo; the popups' and gauges' steps
void hud_draw_gauges(HudView& hud, Ui& ui, const World& world);
void hud_popups_step(HudSlot& slot, bool one_view);
void hud_meters_step(HudSlot& slot, const Fighter& f);
// hud_meters.cpp: hud_update's bob, once an update; hud_draw_meters's models for the
// viewer into hud.cockpit
void hud_bob_step(HudSlot& slot, const Fighter& f, bool shown);
void hud_meters_build(HudView& hud, const World& world);
// hud_reticle.cpp: the tracking box, lock box, lock marks, melee mark
void hud_draw_reticle(HudView& hud, Ui& ui, const World& world, const HudFrame& frame);
void hud_marks_step(HudView& hud, const World& world, int slot);
// hud_radar.cpp: hud_draw_radar and the name tags
void hud_draw_radar(HudView& hud, Ui& ui, const World& world, const HudFrame& frame);
void hud_fighter_tag_draw(HudView& hud, Ui& ui, const World& world, int viewer, int fighter,
                          float x, float y);
// hud_scenes.cpp: READY/GO, the letterbox, the death card, the VS marks and end menu
void hud_scenes_step(HudView& hud, Ui& ui, const World& world);
// the world's VS round and end menu into the scenes, with the menu's sounds and the quit box
void hud_vs_step(HudView& hud, Ui& ui, const World& world);
void hud_draw_scenes(HudView& hud, Ui& ui, const World& world, const HudFrame& frame);
void hud_draw_vs_mark(HudView& hud, Ui& ui, const World& world, int slot);
// hud_barks.cpp: the bark window
void hud_barks_step(HudView& hud, Ui& ui, const World& world);
void hud_draw_barks(HudView& hud, Ui& ui);
