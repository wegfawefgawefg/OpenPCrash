// PC CONFIG (state 0xA0): the sound config's frame (its backdrop, its header with PC in place of
// SOUND, its footer with the level meter and the now-playing strip), a column of option buttons
// for the pages and the key config's bars for the rows, the same fades, cursor sounds, blinks and
// help line as the original's option screens (docs/settings.md, "The look"); drawn in
// pc_settings_draw.cpp.
#include "screens/pc/pc_settings_screen.hpp"

#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/music_panel.hpp"
#include "screens/pc/pause_settings.hpp"
#include "screens/pc/pc_settings_layout.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

// the sheets the sound config and key config load (scr_common, sound_config, key_config, option)
constexpr int texture_sets[] = {0x690, 0x685, 0x677, 0x67c};

float half_second(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

} // namespace

int pc_page_rows(int page) {
    if (page == page_bindings) {
        return pc_binds_count();
    }
    if (page == page_options) {
        return pause_options_count();
    }
    if (page == page_music || page == page_playlist) {
        return music_rows_count(page);
    }
    if (page == page_cheats) {
        return pc_cheats_count();
    }
    return pc_rows_count(page);
}

PcRowView pc_page_row_view(FrontEnd& fe, int page, int row) {
    if (page == page_bindings) {
        return pc_bind_view(fe, row);
    }
    if (page == page_options) {
        return pause_options_view(fe, row);
    }
    if (page == page_music || page == page_playlist) {
        return music_row_view(fe, page, row);
    }
    if (page == page_cheats) {
        return pc_cheats_view(fe, row);
    }
    PcRowView view = pc_row_view(*fe.pc, page, row);
    // CHEATS: in a game, between fights
    if (pc_row_is_cheats(page, row) && !pc_cheats_allowed(fe)) {
        view.dim = true;
        view.value = fe.screen.pc.paused ? "NOT IN A FIGHT" : "IN A GAME";
        view.help += fe.screen.pc.paused ? " Open them from the menus between fights."
                                         : " Load or start a game first.";
    }
    if (pc_row_is_calibrate(page, row)) {
        view.value = pc_calibrate_value(fe);
        view.dim = view.value == "NO PAD";
    }
    // over the pause: a setting the fight took at its start says it waits for the next
    if (fe.screen.pc.paused && pc_row_next_fight(page, row)) {
        view.value += " NEXT FIGHT";
        view.help += " This fight keeps the one it started with.";
    }
    return view;
}

int pc_column_size(const PcSettingsScreen& s) {
    if (s.music) {
        return static_cast<int>(std::size(pc_column_music));
    }
    return s.paused ? static_cast<int>(std::size(pc_column_paused))
                    : static_cast<int>(std::size(pc_column_plain));
}
int pc_column_page(const PcSettingsScreen& s, int i) {
    if (s.music) {
        return pc_column_music[i];
    }
    return s.paused ? pc_column_paused[i] : pc_column_plain[i];
}
int pc_column_of(const PcSettingsScreen& s, int page) {
    if (page == page_cheats) {
        page = page_game; // CHEATS is GAME's
    }
    for (int i = 0; i < pc_column_size(s); ++i) {
        if (pc_column_page(s, i) == page) {
            return i;
        }
    }
    return 0;
}

PointerRect pc_column_rect(int i) {
    return pc_button_rect(column_x, column_y + column_pitch * static_cast<float>(i));
}
PointerRect pc_row_rect(int shown) {
    const float y = rows_y + row_pitch * static_cast<float>(shown);
    return {rows_x, y, rows_x + rows_w, y + 32};
}

namespace {

void keep_in_view(PcSettingsScreen& s) {
    if (s.row < s.top) {
        s.top = s.row;
    } else if (s.row >= s.top + rows_shown) {
        s.top = s.row - rows_shown + 1;
    }
}

// a page's rows taken up: from the top
void enter_rows(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    s.in_rows = true;
    s.row = 0;
    s.top = 0;
    s.note.clear();
    ui_sound(ui, bank_sys, se_menu_accept);
    menu_input_mark(ui);
}

void leave(FrontEnd& fe, Ui& ui) {
    ui_sound(ui, bank_sys, se_menu_accept);
    if (fe.screen.pc.music_from_sound) {
        music_panel_leave(fe, ui); // the MUSIC panel back to SOUND's MUSIC row
        return;
    }
    if (fe.screen.pc.paused) {
        pause_settings_close(fe, ui); // back to the pause, on its item
        return;
    }
    fade_out_start(ui, half_second(ui));
    fe.screen.step = 3;
}

// the cursor in the column
void column_update(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    const int size = pc_column_size(s);
    PointerRect rects[page_count];
    for (int i = 0; i < size; ++i) {
        rects[i] = pc_column_rect(i);
    }
    int at = pc_column_of(s, s.page);
    if (menu_cursor(ui, at, 0, size, cursor_menu_vertical, 0, 3, rects)) {
        s.page = pc_column_page(s, at);
        s.row = s.top = 0;
    }
    // the pointer onto a row takes the cursor there
    if (ui.pointer.moved && s.page != page_exit) {
        for (int shown = 0; shown < rows_shown && s.top + shown < pc_page_rows(s.page); ++shown) {
            const PointerRect rect = pc_row_rect(shown);
            if (pointer_inside(ui.pointer, rect)) {
                s.in_rows = true;
                s.row = s.top + shown;
                ui_sound(ui, bank_sys, se_menu_cursor_vertical);
                menu_input_mark(ui);
                return;
            }
        }
    }
    if (s.quiet > 0) {
        return;
    }
    const bool right = (ui.pad.b.rising & pad_dpad_right) != 0;
    if (menu_accept(ui, &rects[pc_column_of(s, s.page)]) || (right && s.page != page_exit)) {
        if (s.page == page_exit) {
            leave(fe, ui);
        } else if (pc_page_rows(s.page) > 0) {
            enter_rows(fe, ui);
        }
    } else if (menu_cancel(ui) && s.page != page_exit) {
        // back moves to EXIT first, as the options do
        s.page = page_exit;
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
    } else if (menu_cancel(ui) && (s.paused || s.music)) {
        leave(fe, ui); // over the pause (Escape is Back there) or in MUSIC back again leaves
    }
}

// the cursor in the rows
void rows_update(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    const int count = pc_page_rows(s.page);
    const PcRowView view = pc_page_row_view(fe, s.page, s.row);
    const PointerRect current = pc_row_rect(s.row - s.top);
    // the wheel over the current row steps its value instead of moving the cursor
    const bool wheel_value =
        ui.pointer.wheel != 0 && view.arrows && pointer_inside(ui.pointer, current);
    int row = s.row;
    if (!wheel_value && menu_cursor(ui, row, 0, count, cursor_menu_vertical, 0, 3)) {
        s.row = row;
        s.note.clear();
    }
    // the pointer: a row under it takes the cursor; a button in the column goes back there
    if (ui.pointer.moved) {
        for (int shown = 0; shown < rows_shown && s.top + shown < count; ++shown) {
            if (pointer_inside(ui.pointer, pc_row_rect(shown)) && s.top + shown != s.row) {
                s.row = s.top + shown;
                s.note.clear();
                ui_sound(ui, bank_sys, se_menu_cursor_vertical);
                menu_input_mark(ui);
            }
        }
        for (int i = 0; i < pc_column_size(s); ++i) {
            if (pointer_inside(ui.pointer, pc_column_rect(i))) {
                s.in_rows = false;
                if (s.page != pc_column_page(s, i)) {
                    s.page = pc_column_page(s, i);
                    s.row = s.top = 0;
                    ui_sound(ui, bank_sys, se_menu_cursor_vertical);
                }
                return;
            }
        }
    }
    keep_in_view(s);

    // the value: left and right (held repeats), the wheel, a click on either half
    int step = 0;
    if (view.arrows) {
        int v = 1;
        step = menu_cursor_move(ui.pad, v, 0, 3, cursor_value_horizontal);
        if (wheel_value) {
            step = ui.pointer.wheel > 0 ? 1 : -1;
        }
        // a click on either half (a wide row's on its value: its label accepts)
        const float left = row_value_x(view);
        if (ui.pointer.left && pointer_inside(ui.pointer, current) && !s.quiet &&
            (!view.wide || ui.pointer.x >= left)) {
            step = ui.pointer.x < (left + value_right) * 0.5f ? -1 : 1;
        }
    } else if (s.page == page_bindings && (ui.pad.b.rising & pad_dpad_left)) {
        s.in_rows = false; // a list without values: left goes back to the column
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        return;
    }
    const bool calibrate = pc_row_is_calibrate(s.page, s.row);
    const bool music = s.page == page_music || s.page == page_playlist;
    if (step != 0) {
        const bool changed = s.page == page_bindings  ? pc_bind_step(fe, s.row, step)
                             : s.page == page_options ? pause_options_step(fe, ui, s.row, step)
                             : music                  ? music_row_step(fe, ui, s.page, s.row, step)
                             : calibrate              ? pc_calibrate_step(fe, step)
                             : s.page == page_cheats  ? pc_cheats_step(fe, s.row, step)
                                                      : pc_row_step(*fe.pc, s.page, s.row, step);
        if (changed) {
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        }
        menu_input_mark(ui);
        return;
    }
    if (s.quiet > 0) {
        return;
    }
    // STICK CALIBRATION: accept (or a click on the label) opens the panel on the row's pad
    const PointerRect label{current.left, current.top, value_x, current.bottom};
    if (calibrate && menu_accept(ui, &label)) {
        pc_calibrate_open(fe, ui);
        return;
    }
    // the MUSIC panel: accept (or a click on a wide row's label) plays a song or opens a page
    const PointerRect music_label{current.left, current.top, row_value_x(view), current.bottom};
    if (music && menu_accept(ui, view.wide ? &music_label : &current)) {
        if (music_row_accept(fe, ui, s.page, s.row)) {
            ui_sound(ui, bank_sys, se_menu_accept);
        }
        menu_input_mark(ui);
        return;
    }
    if (!view.arrows && menu_accept(ui, &current)) {
        if (s.page == page_bindings) {
            ui_sound(ui, bank_sys, se_message_box);
            pc_bind_accept(fe, s.row);
        } else if (pc_row_is_report(s.page, s.row)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            report_panel_open(fe, false);
        } else if (pc_row_is_music(s.page, s.row)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            music_panel_open(fe, ui, true);
        } else if (pc_row_is_cheats(s.page, s.row)) {
            if (pc_cheats_allowed(fe)) {
                ui_sound(ui, bank_sys, se_menu_accept);
                s.page = page_cheats;
                s.row = s.top = 0;
            }
        } else if (s.page == page_cheats) {
            if (pc_cheats_accept(fe, s.row)) {
                ui_sound(ui, bank_sys, se_menu_accept);
            }
        } else if (pc_row_accept(*fe.pc, s.page, s.row)) {
            ui_sound(ui, bank_sys, se_menu_accept);
        }
        menu_input_mark(ui);
    } else if (menu_cancel(ui) && s.page == page_cheats) {
        // back from CHEATS to GAME's CHEATS row
        s.page = page_game;
        s.row = pc_cheats_row();
        s.top = 0;
        keep_in_view(s);
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
    } else if (menu_cancel(ui)) {
        s.in_rows = false;
        s.note.clear();
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
    }
}

} // namespace

void pc_options_item_draw(FrontEnd& fe, Ui& ui, float x, float y, bool lit, int blend,
                          std::uint32_t flags) {
    pc_art_load(fe.pc_art, ui);
    pc_button_draw(ui, fe.pc_art, "PC CONFIG", x, y, lit, blend, flags);
}

void pc_settings_spawn(FrontEnd& fe, Ui&) {
    fe.screen.pc = PcSettingsScreen{};
}

void pc_settings_step(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    if (!fe.pc) {
        task_header_set(fe, 0);
        return;
    }
    switch (fe.screen.step) {
    case 0: // as sound_config_screen_update: the sheets, the meter, the strip, a half-second fade
        for (const int set : texture_sets) {
            texture_set_load(ui, set);
        }
        pc_art_load(fe.pc_art, ui);
        level_meter_spawn(ui, fe.screen.meter);
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
        state_music_play(fe, ui, 0x1a);
        fade_in_start(ui, half_second(ui));
        fe.screen.step = 1;
        break;
    case 1:
        pc_settings_draw(fe, ui, false);
        if (fade_is_idle(ui)) {
            fe.screen.step = 2;
        }
        break;
    case 2:
        pc_settings_input(fe, ui);
        pc_settings_draw(fe, ui, true);
        break;
    case 3: // the fade out, then the answer: back to the options
        pc_settings_draw(fe, ui, false);
        if (fade_is_black(ui)) {
            task_header_set(fe, s.result);
            fe.screen.step = 4;
        }
        break;
    default:
        pc_settings_draw(fe, ui, false);
        break;
    }
}

void pc_settings_input(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    music_panel_frame(fe, ui);
    if (s.quiet > 0) {
        --s.quiet;
    }
    if (s.listening) {
        if (--s.listen_ticks <= 0) {
            s.listening = false;
            s.note = "Stopped listening.";
        }
    } else if (s.calibration.open) {
        pc_calibrate_update(fe, ui);
    } else if (s.in_rows) {
        rows_update(fe, ui);
    } else {
        column_update(fe, ui);
    }
}

void pc_settings_destroy(FrontEnd&, Ui&) {
}

bool pc_settings_event(FrontEnd& fe, Ui& ui, const SDL_Event& event) {
    const bool menu = fe.state == app_pc_config && fe.screen_alive && fe.screen.step == 2;
    if (!menu && !fe.pause_settings) {
        return false;
    }
    if (fe.screen.pc.calibration.open) {
        return pc_calibrate_event(fe, ui, event);
    }
    if (fe.screen.pc.page != page_bindings) {
        return false;
    }
    return pc_bind_event(fe, ui, event);
}
