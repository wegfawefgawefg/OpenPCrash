// The name entry (name_entry_* state 0x16, decomp src/od/u_02c4a0.c): a 15 x 6 grid on three
// pages (letters and digits, symbols, end), the tabs under it, and the name box above; its input
// and steps. The drawing is name_entry_draw.cpp.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/shops/music_player.hpp"
#include "world/world.hpp"

#include <cmath>

namespace {

constexpr int common_asset = 0x690;
constexpr int name_asset = 0x67b;

float half_second(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

std::int16_t* target(FrontEnd& fe) {
    NameTarget& t = fe.shop.name;
    return t.buffer ? t.buffer : t.scratch;
}

void help_init(Ui& ui, NameEntryScreen& n, int page) {
    const UiTable* help = ui_table(ui.data, "name_entry", "name_entry_help_lines");
    if (page >= 0 && page < 3) {
        help_strip_init(ui, n.help[page],
                        help && page < static_cast<int>(help->strings.size())
                            ? help->strings[static_cast<std::size_t>(page)]
                            : std::string());
    }
}

void page_to(Ui& ui, NameEntryScreen& n, int page) {
    ui_sound(ui, bank_sys, se_page_switch);
    n.prev_page = n.page;
    n.page = page;
    n.page_fade = 0.0f;
    help_init(ui, n, n.page);
}

PointerRect cell_rect(int row, int col) {
    const float x = static_cast<float>(72 + col * 34), y = static_cast<float>(180 + row * 26);
    return {x - 8, y - 4, x + 26, y + 22};
}

// decomp: name_entry_screen_reset 0x2C84B
void reset(Ui& ui, FrontEnd& fe, NameEntryScreen& n) {
    const std::int16_t* name = target(fe);
    int count = 0;
    while (name[count] != -1) {
        if (++count > 8) {
            count = 0;
            break;
        }
    }
    n.length = count;
    n.col = n.row = n.on_tabs = n.was_on_tabs = n.tab = n.page = n.prev_page = 0;
    n.tab_row = 6;
    n.page_fade = 1.0f;
    n.title_x = 640.0f;
    n.title_alpha = n.grid_alpha = 0.0f;
    n.pop_step = 0;
    n.timer = -1;
    for (int page = 0; page < 3; ++page) {
        help_init(ui, n, page);
    }
}

// decomp: name_entry_screen_leave 0x2D8A7
void leave(Ui& ui, FrontEnd& fe, NameEntryScreen& n, int result) {
    n.result = result;
    n.frames = static_cast<int>(half_second(ui));
    fade_out_start(ui, static_cast<float>(n.frames));
    fe.screen.step = 4;
}

// a name with a non-space glyph leaves; anything else asks
// decomp: name_entry_accept 0x2D579
void accept_name(Ui& ui, FrontEnd& fe, NameEntryScreen& n) {
    std::int16_t* name = target(fe);
    name[n.length] = -1;
    bool valid = false;
    for (int i = 0; i < n.length; ++i) {
        valid = valid || name[i] != 0;
    }
    if (!valid) {
        name[0] = -1;
        n.choice = 1;
        fe.screen.step = 3;
        return;
    }
    ui_music_stop(ui);
    // the result by where the entry was opened from; keep SV's comes back to it (retail sends it
    // to Club Wired: docs/bugs.md)
    int result = 0;
    switch (n.from) {
    case 0x30:
        result = 1;
        break;
    case app_assemble:
        result = 2;
        break;
    case app_dealer:
        result = 3;
        break;
    case app_wild_machines:
        result = 4;
        break;
    case app_hangar:
    case app_keep_sv:
        result = 5;
        break;
    case app_media_playlist:
        result = 6;
        break;
    default:
        result = 0;
        break;
    }
    leave(ui, fe, n, result);
}

// decomp: name_entry_screen_input 0x2CCA1, name_entry_tabs_input 0x2D114
void input(FrontEnd& fe, Ui& ui, NameEntryScreen& n) {
    // the mouse buttons press the triggers through the bindings: a click is not a page turn
    PadRecord pad = ui.pad;
    if (ui.pointer.left || ui.pointer.right) {
        const std::uint16_t triggers = pad_left_trigger | pad_right_trigger;
        pad.b.rising &= static_cast<std::uint16_t>(~triggers);
        pad.b.repeat &= static_cast<std::uint16_t>(~triggers);
        pad.b.cur &= static_cast<std::uint16_t>(~triggers);
    }
    const auto pressed = [&](std::uint16_t bit) {
        return pad_button_test(pad, PadEdge::Pressed, PadMatch::Any, bit);
    };
    const auto repeat = [&](std::uint16_t bit) {
        return pad_button_test(pad, PadEdge::Repeat, PadMatch::Any, bit);
    };
    const bool kind1 = fe.shop.name.kind == 1;

    // the pointer: a cell of the page or a tab under it moves the cursor there
    const NameEntryLayout l = name_entry_layout(ui);
    PointerRect tabs[3];
    for (int k = 0; k < 3; ++k) {
        tabs[k] = sprite_rect(ui, static_cast<int>(l.at(5)), static_cast<float>(l.at(6)) + k * 170.0f,
                              static_cast<float>(l.at(7)), true);
    }
    if (ui.pointer.moved && ui.pointer.visible) {
        const int tab = pointer_pick(ui.pointer, tabs, 3);
        if (tab >= 0 && !(kind1 && tab == 1) && (!n.on_tabs || tab != n.tab)) {
            if (!n.on_tabs) {
                n.saved_row = n.row;
            }
            n.on_tabs = 1;
            n.row = 6;
            n.tab_row = 6;
            n.tab = tab;
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            menu_input_mark(ui);
        } else if (tab < 0 && n.page != 2) {
            for (int row = 0; row < 6; ++row) {
                for (int col = 0; col < 15; ++col) {
                    if (pointer_inside(ui.pointer, cell_rect(row, col)) &&
                        (n.on_tabs || row != n.row || col != n.col)) {
                        n.on_tabs = 0;
                        n.tab_row = 6;
                        n.row = row;
                        n.col = col;
                        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
                        menu_input_mark(ui);
                    }
                }
            }
        }
    }

    if (!n.on_tabs) {
        n.saved_row = n.row;
        menu_cursor(ui, n.col, 0, 15, 0x17, 0, 4);
        menu_cursor(ui, n.row, 0, 7, 0x12, 0, 3);
    } else {
        menu_cursor(ui, n.tab, 0, 3, 0x17, 0, 4);
        menu_cursor(ui, n.tab_row, 0, 7, 0x12, 0, 3);
        if (n.page != n.tab) {
            page_to(ui, n, n.tab);
        }
    }
    // the triggers turn the page; at the ends only on a new press
    if (n.tab == 2) {
        if (pressed(pad_right_trigger)) {
            n.tab = 0;
            page_to(ui, n, 0);
        } else if (!repeat(pad_right_trigger) && repeat(pad_left_trigger)) {
            n.tab = 1;
            page_to(ui, n, 1);
        }
    } else if (n.tab == 0) {
        if (repeat(pad_right_trigger)) {
            n.tab = 1;
            page_to(ui, n, 1);
        } else if (pressed(pad_left_trigger)) {
            n.tab = 2;
            n.on_tabs = 1;
            page_to(ui, n, 2);
        }
    } else if (repeat(pad_right_trigger)) {
        n.tab += 1;
        page_to(ui, n, n.tab);
        if (n.page == 2) {
            n.on_tabs = 1;
        }
    } else if (repeat(pad_left_trigger)) {
        n.tab -= 1;
        page_to(ui, n, n.tab);
    }
    n.was_on_tabs = n.on_tabs;
    if (!n.on_tabs && n.row == 6) {
        n.on_tabs = 1;
    } else if (n.on_tabs == 1 && n.tab_row != 6) {
        n.on_tabs = 0;
        if (n.page == 2) {
            n.tab = n.page = n.prev_page;
            n.row = n.saved_row;
        } else {
            n.row = n.tab_row;
        }
        n.tab_row = 6;
    }
    if (n.page != 2 && n.row != 6) {
        n.on_tabs = 0;
    }
    if (menu_cancel(ui) && n.page != 2) {
        ui_sound(ui, bank_sys, se_page_switch);
        n.prev_page = n.page;
        n.page = 2;
        n.on_tabs = 1;
        // retail parks the tab cursor at tab 4, off screen, until a move brings it back; the port
        // puts it on the end tab it stands for (docs/bugs.md)
        n.tab = 2;
        n.tab_row = 6;
        n.saved_row = n.was_on_tabs != 1 ? n.row : 5;
        n.row = 6;
    }
    // garage names skip the symbols page
    if (kind1 && n.page == 1) {
        if (n.prev_page == 0) {
            n.page = 2;
            n.on_tabs = 1;
            n.tab = 2;
            n.tab_row = 6;
        } else if (n.prev_page == 2) {
            n.tab = n.page = 0;
        }
    }
    const PointerRect* click = nullptr;
    PointerRect here;
    if (!n.on_tabs) {
        here = cell_rect(n.row, n.col);
        click = &here;
    } else {
        click = &tabs[n.tab >= 0 && n.tab < 3 ? n.tab : 2];
    }
    std::int16_t* name = target(fe);
    if (menu_accept(ui, click)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        menu_input_mark(ui);
        if (!n.on_tabs) {
            n.length = n.length + 1 > 8 ? 8 : n.length + 1;
            const int glyph = name_entry_cell_glyph(ui, n.page, n.row, n.col);
            if (glyph != -1) {
                name[n.length - 1] = static_cast<std::int16_t>(glyph);
            }
        } else if (n.page == 2) {
            accept_name(ui, fe, n);
        }
        return;
    }
    if (repeat(pad_x)) {
        if (n.length > 0) {
            --n.length;
            name[n.length] = -1;
            ui_sound(ui, bank_sys, se_name_entry_backspace);
        }
        return;
    }
    if (repeat(pad_y)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        n.length = n.length + 1 > 8 ? 8 : n.length + 1;
        name[n.length - 1] = 0;
    }
}

} // namespace

void name_entry_spawn(FrontEnd& fe, Ui&) {
    fe.screen.name_entry.from = fe.previous;
}

void name_entry_step(FrontEnd& fe, Ui& ui) {
    NameEntryScreen& n = fe.screen.name_entry;
    // from the new game's opening (0x30) or Club Wired (8): the shuffled Sonic Amp pool
    // (music_shuffle_task_start, music_type0_update every update) until the name is accepted
    if ((n.from == 0x30 || n.from == app_club_wired) && fe.screen.step >= 1 &&
        fe.screen.step <= 3) {
        music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
    }
    switch (fe.screen.step) {
    case 0: // name_entry_screen_update
        texture_set_load(ui, common_asset);
        texture_set_load(ui, name_asset);
        level_meter_spawn(ui, fe.screen.meter);
        reset(ui, fe, n);
        for (int i = 0; i < 3; ++i) {
            n.boxes[i] = msg_box_from(ui, "name_entry", "name_entry_message_boxes", i);
        }
        // the music by where it came from (0x30 and 8: the shuffled pool, pumped above)
        if (n.from == app_assemble || n.from == app_dealer || n.from == app_wild_machines ||
            n.from == app_hangar) {
            state_music_play(fe, ui, n.from);
        } else if (n.from == app_media_playlist) {
            ui_music(ui, 0xf, true);
        }
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
        n.frames = static_cast<int>(half_second(ui));
        fade_in_start(ui, static_cast<float>(n.frames));
        fe.screen.step = 1;
        break;
    case 1: // step2
        menu_input_mark(ui);
        name_entry_draw(fe, ui, n, false);
        if (--n.frames < 0) {
            n.timer = task_timer_get(ui);
            fe.screen.step = 2;
        }
        break;
    case 2: // step3: the animations, the input, the cursors
        if (n.grid_alpha >= 1.0f) {
            n.page_fade = n.page_fade < 1.0f ? n.page_fade + 0.1f : 1.0f;
        }
        n.grid_alpha = n.grid_alpha + 0.05f > 1.0f ? 1.0f : n.grid_alpha + 0.05f;
        if (n.grid_alpha > 0.5f) {
            if (n.title_x > 289.0f) {
                n.title_x -= 30.0f;
            } else {
                n.title_x = 289.0f;
                n.title_alpha = n.title_alpha < 1.0f ? n.title_alpha + 0.1f : 1.0f;
            }
        }
        if (n.title_x < 350.0f && n.pop_step < 10) {
            ++n.pop_step;
        }
        input(fe, ui, n);
        n.pulse += 0.05f;
        if (n.pulse > 3.1415927f) {
            n.pulse -= 3.1415927f;
        }
        name_entry_draw(fe, ui, n, true);
        break;
    case 3: { // name_entry_message_update: an empty name
        const int kind = fe.shop.name.kind;
        PointerRect answers[2];
        msg_box_choice_rects(ui, n.boxes[1], answers);
        // retail's range of three gives a third, unlit "no" one step right of no; the port
        // keeps the two answers (docs/bugs.md)
        menu_cursor(ui, n.choice, 0, 2, 0x17, 0, 3,
                    kind == 1 && n.boxes[1].row.done ? answers : nullptr);
        name_entry_draw(fe, ui, n, true);
        if (kind == 3) {
            leave(ui, fe, n, 6);
            break;
        }
        MsgBox& box = n.boxes[kind == 1 ? 1 : kind == 2 ? 2 : 0];
        if (kind == 1) {
            msg_box_update_choice(ui, box, n.choice);
        } else {
            msg_box_update_ok(ui, box);
        }
        const PointerRect ok = msg_box_ok_rect(ui, box);
        if (menu_accept(ui, kind == 1 ? &answers[n.choice] : &ok)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            msg_box_open(box);
            if (kind == 1 && n.choice == 0) {
                leave(ui, fe, n, 2);
            } else {
                if (kind != 1) {
                    n.length = 0;
                }
                fe.screen.step = 2;
            }
        }
        break;
    }
    case 4: // name_entry_screen_fade_out
        name_entry_draw(fe, ui, n, false);
        if (--n.frames < 0) {
            task_header_set(fe, n.result);
            fe.screen.step = 5;
        }
        break;
    default:
        name_entry_draw(fe, ui, n, false);
        break;
    }
}

void name_entry_destroy(FrontEnd& fe, Ui& ui) {
    ui.clear_colour = 0xff000000;
    talk_pilot_name_update(fe);
}
