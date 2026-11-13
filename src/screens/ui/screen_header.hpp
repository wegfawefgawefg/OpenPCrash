#pragma once
// The small pieces most menu screens draw over their frame (decomp src/hand/u_0b1a50.c,
// src/hand/u_0bbb00.c number_draw, src/od/u_06c3e0.c): the day and the money from the number
// fonts, the selected garage's name banner sliding in, and an icon on a plate (the A / B hints).
#include "screens/ui/screen_text.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct World;
struct Garage;

// number_draw in full: flags 1 minus sign, 2 plus sign, 4 commas, 8 right-aligned at x
// (else left-aligned), 0x10 leading zeros, 0x20 trailing decimal zeros, 0x40 nines when too
// long; decimals after a dot, the value times scale; returns the measured width
int number_draw_full(Ui& ui, std::uint32_t flags, long long value, float frac, int x, int y,
                     int blend, std::uint32_t color, int digits, int decimals, int scale,
                     int font);
// number_draw_full with no decimals
int number_draw_ex(Ui& ui, std::uint32_t flags, long long value, int x, int y, int blend,
                   std::uint32_t color, int digits, int scale, int font);
// day_draw: the DAY label 0xEA7 and the day + 1. Retail overwrites its position arguments
// with (0x33, 0x31), so every caller's day lands there
void day_draw(Ui& ui, const World& world, int blend);
// money_draw: the money at (0xFD, 0x31) whatever the caller passes (as day_draw)
void money_draw(Ui& ui, const World& world, int blend);
// money_amount_draw: an amount in units of 10 NY, right-aligned at x, the NY mark 0xEAA
void money_amount_draw(Ui& ui, long long amount, int x, int y, int blend, int digits,
                       std::uint32_t color);
// day_draw_at: the label at x, y and a day number (the day itself, shown + 1)
void day_draw_at(Ui& ui, int day, int x, int y, int blend);

// garage_name_banner_draw (flag 0): the garage name plate 0xC08 sliding in from x -320
// over 8 updates from `timer` (timer_start_b; -1 not started), the name in the font beside it
void garage_name_banner_draw(Ui& ui, const Garage& garage, int timer, int y, int blend);
// selected_garage_banner_draw: the selected (boarded) garage's; plate is flag 1, the
// area select's plate 0x367 sliding in from the right
void garage_banner_draw(Ui& ui, const World& world, int timer, int y, int blend,
                        bool plate = false);

// icon_on_plate_draw: the plate 0xC32 at x, y and the icon at x + dx, y + 2
void icon_on_plate_draw(Ui& ui, int x, int y, int blend, int icon, int dx);

// a glyph name (-1 ended or full) as text
GlyphText glyph_name_text(const std::int16_t* name, int length);

// a string of text/strings.sexp by its id ("str.19c608"), UTF-8; "" when missing
std::string text_string(Ui& ui, const std::string& id);
// a layout table's string (row `index`), "" when missing
std::string table_text(Ui& ui, const std::string& screen, const std::string& table, int index = 0);
// a layout table's int (row `index`), `fallback` when missing
int table_int(Ui& ui, const std::string& screen, const std::string& table, int index,
              int fallback = -1);
// SpriteSlot rows from a table on, running into the tables after it in memory as code that
// indexes past a table's end reads them (record_sprites_3[16] is record_sprites_4[10])
std::vector<SpriteSlot> ui_slot_run(Ui& ui, const std::string& screen, const std::string& name);
// SpriteRow::draw_offset: a row moved by dx, dy
void sprite_row_offset(Ui& ui, SpriteSlot row, float dx, float dy);
// the pointer over one of `count` rectangles this update (moved or clicked), -1 none
int pointer_hover(const Ui& ui, const PointerRect* rects, int count);
