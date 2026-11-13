// PC CONFIG (state 0xA0): the sound config's frame (its backdrop, its header with PC in place of
// SOUND, its footer with the level meter and the now-playing strip), a column of option buttons
// for the pages and the key config's bars for the rows, the same fades, cursor sounds, blinks and
// help line as the original's option screens (docs/settings.md, "The look").
#include "screens/pc/pc_settings_screen.hpp"

#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

// the sheets the sound config and key config load (scr_common, sound_config, key_config, option)
constexpr int texture_sets[] = {0x690, 0x685, 0x677, 0x67c};
// the sound config's backdrop and footer, the key config's bars and arrows
constexpr int backdrop = 2514, footer = 2520;
constexpr int bar_grey = 1597, bar_lit = 1598;
constexpr int arrow_left = 1588, arrow_left_lit = 1589, arrow_right = 1590, arrow_right_lit = 1591;
constexpr int scroll_up = 3171, scroll_down = 3170;
// the sound config's volume bar (2509) and its fill (0x9DB stretched)
constexpr int volume_bar = 2509, volume_fill = 0x9db;
// the options' EXIT button, normal and lit
constexpr int exit_button = 2108, exit_button_lit = 2109;

// the column: option buttons at the sound config's pitch from where the key config's rows start
constexpr float column_x = 46, column_y = 98, column_pitch = 43;
constexpr const char* page_words[page_count] = {"SCREEN",   "PICTURE", "SOUND", "INPUT",
                                                "BINDINGS", "GAME",    "EXIT"};
// the rows: the key config's bars, 26 apart, ten shown
constexpr float rows_x = 252, rows_y = 98, rows_w = 356, row_pitch = 26;
constexpr int rows_shown = 10;
constexpr float value_x = rows_x + 196; // the left arrow; the value between it and the right
constexpr float value_right = rows_x + rows_w - 22;
constexpr float keys_x = rows_x + 166, pad_x = rows_x + 306; // BINDINGS' two columns

float half_second(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

int rows_of(int page) {
    if (page == page_bindings) {
        return pc_binds_count();
    }
    return pc_rows_count(page);
}

PcRowView view_of(FrontEnd& fe, int page, int row) {
    if (page == page_bindings) {
        return pc_bind_view(fe, row);
    }
    PcRowView view = pc_row_view(*fe.pc, page, row);
    if (pc_row_is_calibrate(page, row)) {
        view.value = pc_calibrate_value(fe);
        view.dim = view.value == "NO PAD";
    }
    return view;
}

PointerRect column_rect(int i) {
    return pc_button_rect(column_x, column_y + column_pitch * static_cast<float>(i));
}
PointerRect row_rect(int shown) {
    const float y = rows_y + row_pitch * static_cast<float>(shown);
    return {rows_x, y, rows_x + rows_w, y + 32};
}

// the help line: what the page does in the column, the row's own in the rows, BINDINGS' word
// on what a bind did
void help_update(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    std::string text = pc_page_help(s.page);
    if (s.calibration.open) {
        text = pc_calibrate_help(fe);
    } else if (s.in_rows) {
        text = view_of(fe, s.page, s.row).help;
        if (s.page == page_bindings && !s.note.empty()) {
            text = s.note;
        }
    }
    if (text != s.help_text) {
        s.help_text = text;
        help_strip_init(ui, s.help, pc_help_text(text, help_lead_sound_config));
    }
}

void draw_value(FrontEnd& fe, Ui& ui, const PcRowView& view, float y, bool current) {
    const PcArt& art = fe.pc_art;
    const std::uint32_t colour = view.dim ? 0xff808080 : 0xffffffff;
    const bool lit = current && fe.screen.pc.in_rows;
    if (view.arrows) {
        Sprite left = sprite_of(ui, lit ? arrow_left_lit : arrow_left);
        left.pos[0] = value_x;
        left.pos[1] = y + 8;
        left.blend = lit ? 0x105 : 0x104;
        left.flags |= lit ? 0x2000 : 0;
        sprite_submit(ui, left);
        Sprite right = sprite_of(ui, lit ? arrow_right_lit : arrow_right);
        right.pos[0] = value_right + 4;
        right.pos[1] = y + 8;
        right.blend = left.blend;
        right.flags = right.flags | (lit ? 0x2000 : 0);
        sprite_submit(ui, right);
    }
    if (view.bar >= 0) {
        // the number, then the sound config's bar filled to the value
        pc_text_draw(ui, art, view.value, value_x + 22, y + 6, 0x103, colour);
        const float bar_x = value_x + 58, bar_w = value_right - bar_x - 4;
        pc_bar_draw(ui, volume_bar, bar_x, y + 8, bar_w, 0x103, 0xffffffff);
        Sprite fill = sprite_of(ui, volume_fill);
        fill.pos[0] = bar_x + 4;
        fill.pos[1] = y + 12;
        fill.scale[0] =
            (bar_w - 8) / 8.0f * static_cast<float>(std::clamp(view.bar, 0, 100)) / 100.0f;
        fill.blend = 0x104;
        sprite_submit(ui, fill);
        return;
    }
    const float width = static_cast<float>(pc_text_width(art, view.value));
    const float x = std::floor((value_x + 16 + value_right - width) * 0.5f);
    pc_text_draw(ui, art, view.value, x, y + 6, 0x103, colour);
}

// a bindings column ("A/B/C") cut back a bind at a time until it fits, a + for what's left out
std::string fit_binds(const PcArt& art, std::string text, float width) {
    bool cut = false;
    while (static_cast<float>(pc_text_width(art, text + (cut ? "+" : ""))) > width) {
        const std::size_t slash = text.rfind('/');
        if (slash == std::string::npos) {
            break;
        }
        text.erase(slash);
        cut = true;
    }
    return cut ? text + "+" : text;
}

void draw_row(FrontEnd& fe, Ui& ui, int row, int shown) {
    PcSettingsScreen& s = fe.screen.pc;
    const PcArt& art = fe.pc_art;
    const PcRowView view = view_of(fe, s.page, row);
    const float y = rows_y + row_pitch * static_cast<float>(shown);
    const bool current = row == s.row;
    // the bar grey, the current one again lit and pulsing (key_config_screen_draw)
    pc_bar_draw(ui, bar_grey, rows_x, y, rows_w, 2, 0xffa0a0a0);
    if (current && s.in_rows) {
        pc_bar_draw(ui, bar_lit, rows_x, y, rows_w, 3, 0xffffffff, 0x2000);
    }
    float label_x = rows_x + 8;
    if (view.icon >= 0) {
        Sprite icon = sprite_of(ui, view.icon);
        icon.pos[0] = label_x;
        icon.pos[1] = y + 8;
        icon.blend = 0x103;
        sprite_submit(ui, icon);
        label_x += 20;
    }
    pc_text_draw(ui, art, view.label, label_x, y + 6, 0x103, view.dim ? 0xff808080 : 0xffffffff);
    if (s.page != page_bindings || view.arrows) {
        draw_value(fe, ui, view, y, current);
        return;
    }
    // BINDINGS: the keys and mouse, then the pad; PRESS blinking while it listens
    if (current && s.listening) {
        pc_text_draw(ui, art, "PRESS", keys_x, y + 6, 0x103, 0xffffffff, 0x4000);
        return;
    }
    pc_text_draw(ui, art, fit_binds(art, view.value, pad_x - keys_x - 8), keys_x, y + 6, 0x103,
                 0xffffffff);
    pc_text_draw(ui, art, fit_binds(art, view.value_2, rows_x + rows_w - pad_x - 8), pad_x, y + 6,
                 0x103, 0xffffffff);
}

void pc_draw(FrontEnd& fe, Ui& ui, bool pulse) {
    PcSettingsScreen& s = fe.screen.pc;
    // the frame: backdrop, header and footer as sound_config_sprites places them
    Sprite back = sprite_of(ui, backdrop);
    back.blend = 1;
    sprite_submit(ui, back);
    pc_title_draw(ui, 4);
    // day and money as the sound config draws them, unless the options came from the title
    if (fe.options_entry_state != app_title && fe.world) {
        day_draw(ui, *fe.world, 0x108);
        money_draw(ui, *fe.world, 0x108);
    }
    Sprite foot = sprite_of(ui, footer);
    foot.pos[1] = 376;
    foot.blend = 4;
    sprite_submit(ui, foot);
    help_update(fe, ui);
    text_strip_draw_scroll(ui, s.help, 0xfff0f0f0);

    // the column: every button, the chosen one again lit (pulsing while it has the cursor)
    for (int i = 0; i < page_count; ++i) {
        const float y = column_y + column_pitch * static_cast<float>(i);
        for (const bool lit : {false, true}) {
            if (lit && i != s.page) {
                continue;
            }
            const int blend = lit ? 4 : 3;
            const std::uint32_t flags = lit && pulse && !s.in_rows ? 0x2000 : 0;
            if (i == page_exit) {
                Sprite exit = sprite_of(ui, lit ? exit_button_lit : exit_button);
                exit.pos[0] = column_x;
                exit.pos[1] = y;
                exit.blend = blend;
                exit.flags |= flags;
                sprite_submit(ui, exit);
            } else {
                pc_button_draw(ui, fe.pc_art, page_words[i], column_x, y, lit, blend, flags);
            }
        }
    }

    // STICK CALIBRATION's panel in the rows' place, or the page's rows, ten at a time from the
    // top one
    if (s.calibration.open) {
        pc_calibrate_draw(fe, ui);
        level_meter_update(ui, fe.screen.meter);
        now_playing_update(ui, fe.screen.strip);
        return;
    }
    const int count = rows_of(s.page);
    for (int shown = 0; shown < rows_shown && s.top + shown < count; ++shown) {
        draw_row(fe, ui, s.top + shown, shown);
    }
    if (s.top > 0) {
        Sprite up = sprite_of(ui, scroll_up);
        up.pos[0] = rows_x + rows_w * 0.5f - 8;
        up.pos[1] = rows_y - 14;
        up.blend = 0x104;
        sprite_submit(ui, up);
    }
    if (s.top + rows_shown < count) {
        Sprite down = sprite_of(ui, scroll_down);
        down.pos[0] = rows_x + rows_w * 0.5f - 8;
        down.pos[1] = rows_y + row_pitch * rows_shown;
        down.blend = 0x104;
        sprite_submit(ui, down);
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

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
    fade_out_start(ui, half_second(ui));
    fe.screen.step = 3;
}

// the cursor in the column
void column_update(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    PointerRect rects[page_count];
    for (int i = 0; i < page_count; ++i) {
        rects[i] = column_rect(i);
    }
    int page = s.page;
    if (menu_cursor(ui, page, 0, page_count, cursor_menu_vertical, 0, 3, rects)) {
        s.page = page;
        s.row = s.top = 0;
    }
    // the pointer onto a row takes the cursor there
    if (ui.pointer.moved && s.page != page_exit) {
        for (int shown = 0; shown < rows_shown && s.top + shown < rows_of(s.page); ++shown) {
            const PointerRect rect = row_rect(shown);
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
    if (menu_accept(ui, &rects[s.page]) || (right && s.page != page_exit)) {
        if (s.page == page_exit) {
            leave(fe, ui);
        } else if (rows_of(s.page) > 0) {
            enter_rows(fe, ui);
        }
    } else if (menu_cancel(ui) && s.page != page_exit) {
        // back moves to EXIT first, as the options do
        s.page = page_exit;
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
    }
}

// the cursor in the rows
void rows_update(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    const int count = rows_of(s.page);
    const PcRowView view = view_of(fe, s.page, s.row);
    const PointerRect current = row_rect(s.row - s.top);
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
            if (pointer_inside(ui.pointer, row_rect(shown)) && s.top + shown != s.row) {
                s.row = s.top + shown;
                s.note.clear();
                ui_sound(ui, bank_sys, se_menu_cursor_vertical);
                menu_input_mark(ui);
            }
        }
        for (int i = 0; i < page_count; ++i) {
            if (pointer_inside(ui.pointer, column_rect(i))) {
                s.in_rows = false;
                if (s.page != i) {
                    s.page = i;
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
        if (ui.pointer.left && pointer_inside(ui.pointer, current) && !s.quiet) {
            step = ui.pointer.x < (value_x + value_right) * 0.5f ? -1 : 1;
        }
    } else if (s.page == page_bindings && (ui.pad.b.rising & pad_dpad_left)) {
        s.in_rows = false; // a list without values: left goes back to the column
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        return;
    }
    const bool calibrate = pc_row_is_calibrate(s.page, s.row);
    if (step != 0) {
        const bool changed = s.page == page_bindings ? pc_bind_step(fe, s.row, step)
                             : calibrate             ? pc_calibrate_step(fe, step)
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
    if (!view.arrows && menu_accept(ui, &current)) {
        if (s.page == page_bindings) {
            ui_sound(ui, bank_sys, se_message_box);
            pc_bind_accept(fe, s.row);
        } else if (pc_row_accept(*fe.pc, s.page, s.row)) {
            ui_sound(ui, bank_sys, se_menu_accept);
        }
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
        pc_draw(fe, ui, false);
        if (fade_is_idle(ui)) {
            fe.screen.step = 2;
        }
        break;
    case 2:
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
        pc_draw(fe, ui, true);
        break;
    case 3: // the fade out, then the answer: back to the options
        pc_draw(fe, ui, false);
        if (fade_is_black(ui)) {
            task_header_set(fe, s.result);
            fe.screen.step = 4;
        }
        break;
    default:
        pc_draw(fe, ui, false);
        break;
    }
}

void pc_settings_destroy(FrontEnd&, Ui&) {
}

bool pc_settings_event(FrontEnd& fe, Ui& ui, const SDL_Event& event) {
    if (fe.state != app_pc_config || !fe.screen_alive || fe.screen.step != 2) {
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
