// The media player's list editor (decomp src/o1/u_0357d7.c: media_edit_select_update,
// media_edit_update): which list to edit, then the owned catalogue in pages of fifteen,
// A adds the song under the cursor, X takes the last one off, Y previews, LT and RT turn pages.
#include "audio/sound_cues.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/shops/media_player.hpp"
#include "world/world.hpp"

namespace {

constexpr const char* mp = "media_playlist";

int cad(const Ui& ui, float f) {
    return static_cast<int>(static_cast<float>(ui.frame_cadence) * f);
}

} // namespace

// which list to edit
// decomp: media_edit_select_update 0x38A2A
void media_edit_select_update(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    media_header_draw(fe, ui, m);
    media_panel_draw(fe, ui, m);
    sprite_rows_draw(ui, ui_slots(ui.data, mp, "media_sprites_7"));
    media_list_cursor_draw(ui, m);
    PointerRect rects[8];
    media_list_rects(ui, rects);
    if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        m.list = m.list_before;
        fe.screen.step = step_edit_leave;
    } else if (menu_accept(ui, &rects[m.list])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        m.col = m.row = m.page = 0;
        m.arrow_left = m.arrow_right = 0;
        m.edit_clock = 0;
        m.help_alive = false;
        media_track_text_set(fe, ui, m);
        fe.screen.step = step_edit;
    } else {
        menu_cursor(ui, m.list, 0, 8, 0x16, 0, 3, rects);
    }
}

namespace {

void page_step(MediaScreen& m, int d) {
    m.page = (m.page + d + m.pages) % m.pages;
}

// the catalogue's cells (5 x 3) and the button row: a cell is its art (0x69D, 56 x 40, drawn from
// 4..50 across and 5..31 down) at (89, 236) and 47 x 30 apart, widened to the pitch; the exit
// plate is media_catalogue_sprites_2 row 0's art (1778, 96 x 32 at (288, 378), drawn 2..92 x 2..29)
PointerRect cell_rect(int cell) {
    if (cell >= 15) {
        return {290.0f, 380.0f, 380.0f, 407.0f};
    }
    const float x = 89.0f + 47.0f * static_cast<float>(cell % 5);
    const float y = 236.0f + 30.0f * static_cast<float>(cell / 5);
    return {x + 4.0f, y + 3.0f, x + 51.0f, y + 33.0f};
}

int pointer_cell(const Ui& ui) {
    if (!ui.pointer.visible) {
        return -1;
    }
    for (int i = 0; i < 16; ++i) {
        if (pointer_inside(ui.pointer, cell_rect(i))) {
            return i;
        }
    }
    return -1;
}

} // namespace

// adding, removing, previewing, turning pages
// decomp: media_edit_update 0x38AFF
void media_edit_update(FrontEnd& fe, Ui& ui, MediaScreen& m) {
    media_header_draw(fe, ui, m);
    media_catalogue_draw(fe, ui, m);
    const int cell = m.row * 5 + m.col;
    const int at = cell >= 15 ? -1 : m.page * 15 + cell;
    const int id = at >= 0 && at < 81 ? m.ids[at] : -1;
    m.arrow_left = m.arrow_left > 0 ? m.arrow_left - 1 : 0;
    m.arrow_right = m.arrow_right > 0 ? m.arrow_right - 1 : 0;
    if (m.edit_clock < cad(ui, 0.15f) - (cad(ui, -0.3f) << 1) - cad(ui, -0.4f)) {
        return;
    }
    // the mouse: hovering a cell moves the cursor there
    const int hit = ui.pointer.moved ? pointer_cell(ui) : -1;
    if (hit >= 0 && (hit == 15 ? m.row != 3 : hit != cell)) {
        m.row = hit == 15 ? 3 : hit / 5;
        m.col = hit == 15 ? m.col : hit % 5;
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
        media_track_text_set(fe, ui, m);
        return;
    }
    const PadRecord& pad = ui.pad;
    // a left click confirms only on the cursor's cell (or the exit plate), where the pointer put
    // the cursor: a click elsewhere in the catalogue adds nothing and doesn't leave
    const PointerRect under = cell_rect(m.row == 3 ? 15 : cell);
    bool refresh = false;
    if (m.row == 3) {
        if (menu_accept(ui, &under)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            media_help_spawn(ui, m);
            fe.screen.step = step_edit_select;
            return;
        }
        refresh = menu_cursor(ui, m.row, 0, 4, 0x16, 0, 3) != 0;
    } else if ((pad.b.rising & pad_left_trigger) != 0 || (ui.pointer.wheel > 0 && m.pages > 1)) {
        if (m.pages > 1) {
            page_step(m, -1);
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            m.arrow_left = 15;
            refresh = true;
        }
    } else if ((pad.b.rising & pad_right_trigger) != 0 || (ui.pointer.wheel < 0 && m.pages > 1)) {
        if (m.pages > 1) {
            page_step(m, 1);
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            m.arrow_right = 15;
            refresh = true;
        }
    } else if ((pad.b.rising & pad_y) != 0) {
        // a preview: the song looped, its bio, a blank help line
        if (id != -1) {
            const MusicRecord* song = music_record(fe.music, id);
            if (!song) {
                song = music_record(fe.music, 0);
            }
            if (song) {
                ui_music(ui, song->wave, true);
                m.bio_alive = script_start(fe, ui, m.bio_script, m.bio, song->bio);
            }
            m.preview_frames = 0;
            media_help_spawn(ui, m);
            fe.screen.step = step_preview;
        }
    } else if (menu_cancel(ui)) {
        if (m.row != 3) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        m.row = 3;
    } else if (menu_accept(ui, &under)) {
        // silent test: sound 5 only when the song goes in; the same song may go in many times
        if (m.counts[m.list] < playlist_length && id != -1) {
            ui_sound(ui, bank_sys, se_menu_accept);
            m.tracks[m.list][m.counts[m.list]++] = id;
        }
    } else if ((pad.b.rising & pad_x) != 0) {
        int& count = m.counts[m.list];
        if (count > 0) {
            --count;
            m.tracks[m.list][count] = -1;
            ui_sound(ui, bank_sys, se_menu_cancel);
        }
    } else {
        const int moved = menu_cursor(ui, m.col, 0, 5, 0x17, 0, 3);
        if (moved == -1 && m.col == 4) {
            page_step(m, -1);
            if (m.pages > 1) {
                m.arrow_left = 15;
            }
        } else if (moved == 1 && m.col == 0) {
            page_step(m, 1);
            if (m.pages > 1) {
                m.arrow_right = 15;
            }
        }
        refresh = moved != 0 || menu_cursor(ui, m.row, 0, 4, 0x16, 0, 3) != 0;
    }
    if (refresh) {
        media_track_text_set(fe, ui, m);
    }
}

