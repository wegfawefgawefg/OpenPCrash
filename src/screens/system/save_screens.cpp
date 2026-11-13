// The save prompts on the port's saves (src/save, docs/saves.md): the records check (state 9,
// records_check_* decomp src/od/u_057040.c), the save files check (0x2E, save_files_*
// src/od/u_09f1d0.c) and the load screen (0x11, save_load_screen_* src/od/u_028400.c). Three
// slots, a file each; a slot reads as there, missing or damaged on its own (retail failed the
// whole file on one bad slot and only ever showed slot 0: docs/bugs.md).
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/system/save_slots.hpp"
#include "screens/ui/screen_header.hpp"
#include "talk/draw2d.hpp"
#include "world/world.hpp"

#include <cstdio>
#include <string>

namespace {

constexpr int common_asset = 0x690;
constexpr int save_load_asset = 0x681;
// 5GARAGE.WAV, which the load screen plays itself (music_play(0xC, 1, 1.0))
constexpr int load_track = 0xc;

float half_second(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

// the slot card and the exit button as the pointer's two items
void save_menu_rects(Ui& ui, PointerRect out[2]) {
    out[0] = sprite_rect(ui, 2313, 256, 107);
    out[1] = sprite_rect(ui, 2323, 400, 318);
}

// save_load_screen_draw: the help line, the frame, day and money unless from the title, the slot
// card and the exit, the open box
void save_load_draw(FrontEnd& fe, Ui& ui) {
    SaveScreen& s = fe.screen.save;
    text_strip_draw_scroll(ui, s.help, 0xfff0f0f0);
    sprite_rows_draw(ui, ui_slots(ui.data, "save", "save_load_sprites"));
    if (s.from != app_title) {
        day_draw(ui, *fe.world, 0x108);
        money_draw(ui, *fe.world, 0x108);
    }
    if (s.status == 2) {
        const SpriteSlot* rows = ui_slots(ui.data, "save", "save_menu_slot_sprites");
        if (s.menu != 1) {
            save_slot_card_draw(fe, ui, s);
        }
        if (rows) {
            if (s.menu != 1) {
                SpriteSlot lit = rows[1];
                lit.flags = s.menu == 0 ? (lit.flags | sprite_blink_soft)
                                        : (lit.flags & ~sprite_blink_soft);
                sprite_row_draw(ui, lit);
            }
            sprite_row_draw(ui, rows[0]);
            if (s.menu == 1) {
                sprite_row_draw(ui, rows[2]); // D_001FC488: the exit lit
            }
            sprite_row_draw(ui, rows[3]); // D_001FC4A4
        }
        if (s.menu == 2) {
            msg_box_update_ok(ui, s.box);
        } else if (s.menu == 4) {
            msg_box_update_choice(ui, s.box, s.choice);
        }
    } else {
        msg_box_update_ok(ui, s.box);
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

} // namespace

void records_check_step(FrontEnd& fe, Ui& ui) {
    SaveScreen& s = fe.screen.save;
    if (fe.screen.step == 0) {
        // records_check_begin
        s.status = 0;
        s.result = 0;
        texture_set_load(ui, common_asset);
        fe.screen.step = 1;
        return;
    }
    // records_check_update: retail creates the file and reads it; a file per slot needs no
    // creating, so this reads the slots and says so when one is damaged
    switch (s.status) {
    case 0: {
        save_slots_read(fe, s);
        bool damaged = false;
        for (int slot = 0; slot < save_slot_count; ++slot) {
            damaged = damaged || s.reads[slot] == SaveRead::Damaged;
        }
        if (damaged) {
            // "Unable to load [Game Records]... may be corrupt. Press A to continue."
            s.box = msg_box_from(ui, "save", "save_dialogs", 1);
            msg_box_open(s.box);
            fade_in_start(ui, half_second(ui));
            s.status = 4;
        } else {
            s.status = 5;
        }
        break;
    }
    case 4: {
        msg_box_update_ok(ui, s.box);
        const PointerRect ok = msg_box_ok_rect(ui, s.box);
        if (menu_accept(ui, &ok)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            s.status = 5;
            fade_out_start(ui, half_second(ui));
        }
        break;
    }
    case 5:
        if (fade_is_black(ui)) {
            task_header_set(fe, s.result);
            s.status = 6;
        }
        break;
    default:
        break;
    }
}

void save_files_step(FrontEnd& fe, Ui& ui) {
    SaveScreen& s = fe.screen.save;
    if (fe.screen.step == 0) {
        // save_files_begin: result 1 when entered from the load screen
        menu_input_mark(ui);
        s.status = 0;
        s.result = s.from == app_save_load ? 1 : 0;
        texture_set_load(ui, common_asset);
        fe.screen.step = 1;
        return;
    }
    switch (s.status) {
    case 0:
        // the 4096 save games and the free blocks are the Xbox's limits: no box here, only the
        // fade out, which does nothing from black
        s.status = 4;
        fade_out_start(ui, half_second(ui));
        break;
    case 4:
        if (fade_is_black(ui)) {
            task_header_set(fe, s.result);
            s.status = 5;
        }
        break;
    default:
        break;
    }
}

void save_load_step(FrontEnd& fe, Ui& ui) {
    SaveScreen& s = fe.screen.save;
    switch (fe.screen.step) {
    case 0: // save_load_screen_update: waits for the cabinet cache
        fe.screen.step = 1;
        break;
    case 1: { // step2
        texture_set_load(ui, save_load_asset);
        texture_set_load(ui, common_asset);
        level_meter_spawn(ui, fe.screen.meter);
        s.menu = 0;
        // the help line save_load_help_lines ("Load play data")
        const UiTable* help = ui_table(ui.data, "save", "save_load_help_lines");
        text_strip_init(ui, s.help, 0, 0x48,
                        help && !help->strings.empty() ? help->strings[0] : "", 0, 0, 0x280,
                        0x1e0);
        ui_music(ui, load_track, true);
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
        s.result = s.from == app_title ? 1 : 0;
        save_slots_read(fe, s);
        int newest = -1, damaged = 0;
        std::int64_t newest_time = 0;
        for (int slot = 0; slot < save_slot_count; ++slot) {
            if (save_slot_present(s, slot) &&
                (newest < 0 || s.images[slot].written > newest_time)) {
                newest = slot;
                newest_time = s.images[slot].written;
            }
            damaged += s.reads[slot] == SaveRead::Damaged ? 1 : 0;
        }
        if (newest >= 0) {
            s.status = 2; // the slot list, starting on the newest
            s.slot = newest;
        } else if (damaged > 0) {
            // the damaged-file message, "return to title" or "to system menu"
            s.box = msg_box_from(ui, "save", "save_dialogs", s.from == app_title ? 2 : 3);
            s.status = 1;
        } else {
            s.box = msg_box_from(ui, "save", "save_load_boxes", 2); // no Game Records
            s.status = 0;
        }
        msg_box_open(s.box);
        fade_in_start(ui, half_second(ui));
        fe.screen.step = 2;
        break;
    }
    case 2: // step3
        save_load_draw(fe, ui);
        if (fade_is_idle(ui)) {
            fe.screen.step = 3;
        }
        break;
    case 3: { // step4
        if (s.status == 2) {
            // the menu: 0 the slot (left and right pick it, sound 4), 1 exit; 2 the done box, 4
            // the system menu's "load over the current game?" box
            PointerRect rects[2];
            save_menu_rects(ui, rects);
            PointerRect answers[2];
            msg_box_choice_rects(ui, s.box, answers);
            if (s.menu == 0) {
                if (!menu_cursor(ui, s.slot, 0, save_slot_count, 0x17, 0, 4)) {
                    menu_cursor(ui, s.menu, 0, 2, 0x16, 0, 3, rects);
                }
            } else if (s.menu == 1) {
                menu_cursor(ui, s.menu, 0, 2, 0x16, 0, 3, rects);
            } else if (s.menu == 4) {
                menu_cursor(ui, s.choice, 0, 2, 0x17, 0, 4, s.box.row.done ? answers : nullptr);
            }
            const PointerRect ok = msg_box_ok_rect(ui, s.box);
            const PointerRect* hit = s.menu == 2   ? &ok
                                     : s.menu == 4 ? &answers[s.choice]
                                                   : &rects[s.menu == 1 ? 1 : 0];
            if (menu_accept(ui, hit)) {
                ui_sound(ui, bank_sys, se_menu_accept);
                if (s.menu == 1) {
                    fade_out_start(ui, half_second(ui));
                    fe.screen.step = 4;
                } else if (s.menu == 0 && save_slot_present(s, s.slot)) {
                    if (s.from == app_title) {
                        // from the title: load and say so
                        save_commit_load(fe, ui, s.slot, s.images[s.slot]);
                        s.box = msg_box_from(ui, "save", "save_load_boxes", 0);
                        msg_box_open(s.box);
                        s.menu = 2;
                    } else {
                        // from the system menu: ask first, No picked
                        s.box = msg_box_from(ui, "save", "save_load_boxes", 3);
                        msg_box_open(s.box);
                        s.menu = 4;
                        s.choice = 1;
                    }
                } else if (s.menu == 2) {
                    s.result = s.from == app_title ? 2 : 0;
                    fade_out_start(ui, half_second(ui));
                    fe.screen.step = 4;
                } else if (s.menu == 4) {
                    if (s.choice == 0) {
                        save_commit_load(fe, ui, s.slot, s.images[s.slot]);
                        s.box = msg_box_from(ui, "save", "save_load_boxes", 0);
                        msg_box_open(s.box);
                        s.menu = 2;
                    } else {
                        s.menu = 0;
                    }
                }
            } else if (menu_cancel(ui)) {
                if (s.menu == 0) {
                    s.menu = 1;
                    ui_sound(ui, bank_sys, se_menu_cursor_vertical);
                } else if (s.menu == 4) {
                    s.choice = 1; // sound 4 even when already on No, as retail
                    ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
                }
            }
        } else {
            const PointerRect ok = msg_box_ok_rect(ui, s.box);
            if (menu_accept(ui, &ok)) {
                ui_sound(ui, bank_sys, se_menu_accept);
                fade_out_start(ui, half_second(ui));
                fe.screen.step = 4;
            }
        }
        save_load_draw(fe, ui);
        break;
    }
    case 4: // save_load_screen_fade_out
        save_load_draw(fe, ui);
        if (fade_is_black(ui)) {
            task_header_set(fe, s.result);
            fe.screen.step = 5;
        }
        break;
    default:
        save_load_draw(fe, ui);
        break;
    }
}

void save_load_destroy(FrontEnd&, Ui& ui) {
    ui_music_stop(ui);
}
