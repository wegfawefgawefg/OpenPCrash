#pragma once
// PC CONFIG's layout (pc_settings_screen.hpp), shared by its input (pc_settings_screen.cpp) and its
// drawing (pc_settings_draw.cpp): where the column and the rows sit on the original's 640x480, and
// which pages and rows there are.
#include "screens/pc/pc_settings_screen.hpp"

// the column: option buttons at the sound config's pitch from where the key config's rows start;
// over the pause OPTIONS first (no footer there, so eight fit)
inline constexpr float column_x = 46, column_y = 98, column_pitch = 43;
inline constexpr int pc_column_plain[] = {page_screen,   page_picture, page_sound, page_input,
                                          page_bindings, page_game,    page_exit};
inline constexpr int pc_column_paused[] = {page_options, page_screen,   page_picture, page_sound,
                                           page_input,   page_bindings, page_game,    page_exit};
// the MUSIC panel's (music_panel.hpp)
inline constexpr int pc_column_music[] = {page_music, page_playlist, page_exit};
// the rows: the key config's bars, 26 apart, ten shown
inline constexpr float rows_x = 252, rows_y = 98, rows_w = 356, row_pitch = 26;
inline constexpr int rows_shown = 10;
inline constexpr float value_x = rows_x + 196; // the left arrow; the value between it and the right
inline constexpr float value_right = rows_x + rows_w - 22;
inline constexpr float value_x_wide = rows_x + 118; // a wide row's left arrow
// where a row's value starts (its left arrow)
inline float row_value_x(const PcRowView& view) {
    return view.wide ? value_x_wide : value_x;
}

// a page's rows and how each shows (BINDINGS', OPTIONS' and the rest's)
int pc_page_rows(int page);
PcRowView pc_page_row_view(FrontEnd& fe, int page, int row);
// the column's pages in order, and where a page sits in it
int pc_column_size(const PcSettingsScreen& s);
int pc_column_page(const PcSettingsScreen& s, int i);
int pc_column_of(const PcSettingsScreen& s, int page);
// the hit boxes: a column button, a row shown
PointerRect pc_column_rect(int i);
PointerRect pc_row_rect(int shown);
