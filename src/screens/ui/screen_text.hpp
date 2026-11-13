#pragma once
// The screens' text pieces (decomp src/hand/u_0bd620.c, u_0bbb00.c): table strings drawn with the
// 18x18 font, the scrolling help line (TextStrip), the message boxes that grow open with their OK
// mark or yes/no answers, and the number fonts' digits.
#include "screens/ui/ui.hpp"

#include <cstdint>
#include <string>

// wide_text_draw_small: glyphs from x, y at layer 0x101 + texture, proportional widths,
// -2 a new line, -9..-18 colours; at most wrap glyphs
void text_draw(Ui& ui, const GlyphText& text, int x, int y, int texture, int glyph_w,
               int glyph_h, std::uint32_t color, int wrap, std::uint32_t flags = 0);
// wide_text_advance_total: the pixel width of a string
int text_width(Ui& ui, const GlyphText& text);

// TextStrip (include/draw.h): a line that scrolls left one pixel a tick and repeats across its
// clip rectangle
struct TextStrip {
    GlyphText text;
    int x = 0, y = 0;
    int next = 0; // pixels scrolled
    int left = 0, top = 0, right = 640, bottom = 480;
};
// text_strip_init with the string from a table (UTF-8)
void text_strip_init(Ui& ui, TextStrip& strip, int x, int y, const std::string& text, int left,
                     int top, int right, int bottom);
// text_strip_draw_scroll; text_strip_scroll_107 is layer 0x107
void text_strip_draw_scroll(Ui& ui, TextStrip& strip, std::uint32_t color, int blend = 0x107);
// the standard help line: (0, 0x48, 37 characters, clipped to the screen)
void help_strip_init(Ui& ui, TextStrip& strip, const std::string& text);

// a MsgBox row being shown: msg_box_open sets its scale to 0.1 and clears done
struct MsgBox {
    MsgBoxRow row;
    bool valid = false;
};
MsgBox msg_box_from(Ui& ui, const std::string& screen, const std::string& table, int row);
void msg_box_open(MsgBox& box);
// func_000BEB8F: the box growing out of its middle, its lines once it is open
void msg_box_update(Ui& ui, MsgBox& box);
// with the OK mark (sprites 0xC1B, 0xC1C)
void msg_box_update_ok(Ui& ui, MsgBox& box);
// with the two answers (0xE85 / 0xE83, the chosen one lit 0xE86 / 0xE84)
void msg_box_update_choice(Ui& ui, MsgBox& box, int choice);
// the answers' hit boxes for the pointer (yes, no) and the OK mark's
void msg_box_choice_rects(Ui& ui, const MsgBox& box, PointerRect out[2]);
PointerRect msg_box_ok_rect(Ui& ui, const MsgBox& box);

// number_draw for the plain cases: right-aligned at x, digits from font_record(font),
// flags 0x10 keeps the leading zeroes (the measure pass of flag 8 skipped)
void number_draw(Ui& ui, std::uint32_t flags, long long value, int x, int y, int blend,
                 std::uint32_t color, int digits, int font);
