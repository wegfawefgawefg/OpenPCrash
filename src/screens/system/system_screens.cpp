// The system menu (system_screen_* state 0x27, decomp src/o1/u_03c67e.c 0x3E1F6..0x3E586) and
// the save menu (save_menu_screen_* 0x1F, src/od/u_034f60.c) on src/save's three slots.
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/system/save_slots.hpp"
#include "screens/ui/screen_header.hpp"
#include "talk/draw2d.hpp"
#include "world/world.hpp"

#include <cstdio>

namespace {

constexpr int common_asset = 0x690;
constexpr int system_asset = 0x688;    // system_tex.bin
constexpr int save_load_asset = 0x681; // save_load_tex.bin
// 5GARAGE.WAV, which the save menu plays itself (music_play(0xC, 1, 1.0))
constexpr int save_track = 0xc;

float half_second(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

// system_item_sprites (an IdPair a row, lit first; decomp src/data/ui_b2.c). The ui
// family writes no IdPair tables yet, so its ten ids are here until it does.
constexpr double system_item_ids[10] = {2637, 2636, 2633, 2632, 2640, 2639, 2635, 2634, 2631, 2630};

// the items' sprites and system_item_positions (x, y a row)
struct SystemItems {
    std::vector<double> ids, at;
};
SystemItems system_items(Ui& ui) {
    return {std::vector<double>(system_item_ids, system_item_ids + 10),
            ui_values(ui.data, "system", "system_item_positions", 10)};
}

std::string system_help_line(Ui& ui, int item) {
    if (item == 4) {
        const UiTable* garage = ui_table(ui.data, "system", "system_help_line_garage");
        return garage && !garage->strings.empty() ? garage->strings[0] : "";
    }
    const UiTable* lines = ui_table(ui.data, "system", "system_help_lines");
    return lines && item < static_cast<int>(lines->strings.size())
               ? lines->strings[static_cast<std::size_t>(item)]
               : "";
}

void system_rects(Ui& ui, const SystemItems& items, PointerRect out[5]) {
    for (int i = 0; i < 5; ++i) {
        const std::size_t k = static_cast<std::size_t>(i * 2);
        out[i] = items.ids.size() > k + 1 && items.at.size() > k + 1
                     ? sprite_rect(ui, static_cast<int>(items.ids[k + 1]),
                                   static_cast<float>(items.at[k]),
                                   static_cast<float>(items.at[k + 1]))
                     : PointerRect{};
    }
}

// system_screen_draw: the help line, the garage banner, the five items (pass 0), the lit one, day
// and money, the frame
void system_draw(FrontEnd& fe, Ui& ui) {
    SystemScreen& s = fe.screen.system;
    text_strip_draw_scroll(ui, s.help[s.cursor], 0xffffffff);
    garage_banner_draw(ui, *fe.world, s.banner, 0x17b, 0x102);
    const SystemItems items = system_items(ui);
    if (items.ids.size() >= 10 && items.at.size() >= 10) {
        for (int i = 0; i < 5; ++i) {
            const std::size_t k = static_cast<std::size_t>(i * 2);
            Sprite sprite = sprite_of(ui, static_cast<int>(items.ids[k + 1]));
            sprite.pos[0] = static_cast<float>(items.at[k]);
            sprite.pos[1] = static_cast<float>(items.at[k + 1]);
            sprite.blend = 6;
            sprite_submit(ui, sprite);
        }
        const std::size_t k = static_cast<std::size_t>(s.cursor * 2);
        Sprite lit = sprite_of(ui, static_cast<int>(items.ids[k]));
        lit.flags |= sprite_blink_soft;
        lit.pos[0] = static_cast<float>(items.at[k]);
        lit.pos[1] = static_cast<float>(items.at[k + 1]);
        lit.blend = 0x101;
        sprite_submit(ui, lit);
    }
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    sprite_rows_draw(ui, ui_slots(ui.data, "system", "system_sprites"));
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// --- the save menu ---

void save_menu_rects(Ui& ui, PointerRect out[2]) {
    out[0] = sprite_rect(ui, 2313, 256, 107);
    out[1] = sprite_rect(ui, 2323, 400, 318);
}

// decomp: save_menu_screen_draw 0x35613
void save_menu_draw(FrontEnd& fe, Ui& ui) {
    SaveScreen& s = fe.screen.save;
    sprite_rows_draw(ui, ui_slots(ui.data, "save", "save_menu_sprites"));
    text_strip_draw_scroll(ui, s.help, 0xfff0f0f0);
    if (s.menu != 1) {
        save_slot_card_draw(fe, ui, s);
    }
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    const SpriteSlot* rows = ui_slots(ui.data, "save", "save_menu_slot_sprites");
    if (rows) {
        if (s.menu != 1) {
            // pulsing in the slot menu, steady under a box
            SpriteSlot lit = rows[1];
            lit.flags =
                s.menu == 0 ? (lit.flags | sprite_blink_soft) : (lit.flags & ~sprite_blink_soft);
            sprite_row_draw(ui, lit);
        }
        sprite_row_draw(ui, rows[0]);
        if (s.menu == 1) {
            sprite_row_draw(ui, rows[2]);
        }
        sprite_row_draw(ui, rows[3]);
    }
    if (s.menu == 2) {
        msg_box_update_choice(ui, s.box, s.choice);
    } else if (s.menu == 3 || s.menu == 4) {
        msg_box_update_ok(ui, s.box);
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// save_write_slot: ok -> box 2 ("saved"), menu 3; failed -> box 1, menu 4
void save_menu_write(FrontEnd& fe, Ui& ui) {
    SaveScreen& s = fe.screen.save;
    std::string error;
    if (save_slot_write(fe.save_dir, s.slot, *fe.world, *fe.machine, error)) {
        save_slots_read(fe, s);
        s.box = msg_box_from(ui, "save", "save_menu_boxes", 2);
        s.menu = 3;
    } else {
        std::fprintf(stderr, "save: slot %d: %s\n", s.slot, error.c_str());
        s.box = msg_box_from(ui, "save", "save_menu_boxes", 1);
        s.menu = 4;
    }
    msg_box_open(s.box);
}

} // namespace

void system_spawn(FrontEnd& fe, Ui&) {
    fe.screen.system.from = fe.previous;
}

void system_step(FrontEnd& fe, Ui& ui) {
    SystemScreen& s = fe.screen.system;
    switch (fe.screen.step) {
    case 0: // system_screen_update
        texture_set_load(ui, common_asset);
        texture_set_load(ui, system_asset);
        level_meter_spawn(ui, fe.screen.meter);
        s.cursor = s.from == app_save_load   ? 1
                   : s.from == app_options   ? 3
                   : s.from == app_save_menu ? 0
                   : s.from == app_keep_sv   ? 2
                                             : (s.from == app_save_files ? 1 : 0);
        for (int i = 0; i < 5; ++i) {
            help_strip_init(ui, s.help[i], system_help_line(ui, i));
        }
        state_music_play(fe, ui, 0x27);
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
        s.banner = -1;
        fade_in_start(ui, half_second(ui));
        fe.screen.step = 1;
        break;
    case 1: // step2
        system_draw(fe, ui);
        if (fade_is_idle(ui)) {
            s.banner = static_cast<int>(ui.task_timer);
            fe.screen.step = 2;
        }
        break;
    case 2: { // step3
        const SystemItems items = system_items(ui);
        PointerRect rects[5];
        system_rects(ui, items, rects);
        if (menu_cursor(ui, s.cursor, 0, 5, 0x16, 0, 3, rects)) {
            help_strip_init(ui, s.help[s.cursor], system_help_line(ui, s.cursor));
        }
        system_draw(fe, ui);
        if (menu_accept(ui, &rects[s.cursor])) {
            ui_sound(ui, bank_sys, se_menu_accept);
            fe.screen.strip.active = false; // music_title_strip_kill
            fade_out_start(ui, half_second(ui));
            fe.screen.step = 3;
        } else if (menu_cancel(ui)) {
            // cancel moves to "Return to garage"; it never leaves
            if (s.cursor != 4) {
                help_strip_init(ui, s.help[4], system_help_line(ui, 4));
                ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            }
            s.cursor = 4;
        }
        break;
    }
    case 3: // step4
        system_draw(fe, ui);
        if (fade_is_black(ui)) {
            task_header_set(fe, s.cursor);
            fe.screen.step = 4;
        }
        break;
    default:
        system_draw(fe, ui);
        break;
    }
}

void system_destroy(FrontEnd&, Ui& ui) {
    ui_music_stop(ui);
    ui.clear_colour = 0xff000000;
}

void save_menu_spawn(FrontEnd& fe, Ui&) {
    fe.screen.save.from = fe.previous;
}

void save_menu_step(FrontEnd& fe, Ui& ui) {
    SaveScreen& s = fe.screen.save;
    switch (fe.screen.step) {
    case 0: { // save_menu_screen_update
        texture_set_load(ui, save_load_asset);
        texture_set_load(ui, common_asset);
        s.slot = fe.menu.selected_save;
        s.menu = 0;
        s.choice = 1;
        level_meter_spawn(ui, fe.screen.meter);
        ui_music(ui, save_track, true);
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
        const UiTable* help = ui_table(ui.data, "save", "save_menu_help_lines"); // "Save play data"
        text_strip_init(ui, s.help, 0, 0x48,
                        help && !help->strings.empty() ? help->strings[0] : "", 0, 0, 0x280,
                        0x1e0);
        // retail reads the one file here; a damaged file ends the screen with status 1 and no
        // room shows the dashboard box. A file per slot has neither: every slot can be written
        save_slots_read(fe, s);
        s.status = 2;
        fade_in_start(ui, half_second(ui));
        fe.screen.step = 1;
        break;
    }
    case 1: // the fade in
        save_menu_draw(fe, ui);
        if (fade_is_idle(ui)) {
            fe.screen.step = 2;
        }
        break;
    case 2: { // save_menu_screen_step3
        PointerRect rects[2];
        save_menu_rects(ui, rects);
        PointerRect answers[2];
        msg_box_choice_rects(ui, s.box, answers);
        const PointerRect ok = msg_box_ok_rect(ui, s.box);
        switch (s.menu) {
        case 0: // the slot: left and right pick one of the three (retail's range of one)
            if (!menu_cursor(ui, s.slot, 0, save_slot_count, 0x17, 0, 4)) {
                menu_cursor(ui, s.menu, 0, 2, 0x16, 0, 3, rects);
            }
            if (s.menu == 0 && menu_accept(ui, &rects[0])) {
                ui_sound(ui, bank_sys, se_menu_accept);
                if (s.reads[s.slot] != SaveRead::Missing) {
                    // a present slot, or a damaged one, asks first, No picked
                    s.box = msg_box_from(ui, "save", "save_menu_boxes", 0);
                    msg_box_open(s.box);
                    s.choice = 1;
                    s.menu = 2;
                } else {
                    save_menu_write(fe, ui);
                }
            } else if (s.menu == 0 && menu_cancel(ui)) {
                s.menu = 1;
                ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            }
            break;
        case 1: // the back button
            menu_cursor(ui, s.menu, 0, 2, 0x16, 0, 3, rects);
            if (s.menu == 1 && menu_accept(ui, &rects[1])) {
                ui_sound(ui, bank_sys, se_menu_accept);
                s.result = 0;
                fade_out_start(ui, half_second(ui));
                fe.screen.step = 3;
            }
            break;
        case 2: // overwrite?
            menu_cursor(ui, s.choice, 0, 2, 0x17, 0, 4, s.box.row.done ? answers : nullptr);
            if (menu_accept(ui, &answers[s.choice])) {
                ui_sound(ui, bank_sys, se_menu_accept);
                if (s.choice == 0) {
                    save_menu_write(fe, ui);
                } else {
                    s.menu = 0;
                }
            } else if (menu_cancel(ui)) {
                s.choice = 1; // sound 4 even when already on No, as retail
                ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            }
            break;
        case 3: // saved
        case 4: // failed
            if (menu_accept(ui, &ok)) {
                ui_sound(ui, bank_sys, se_menu_accept);
                if (s.menu == 3) {
                    fe.menu.selected_save = s.slot;
                }
                s.menu = 0;
            }
            break;
        default:
            break;
        }
        save_menu_draw(fe, ui);
        break;
    }
    case 3: // save_menu_screen_leave, _fade_out
        save_menu_draw(fe, ui);
        if (fade_is_black(ui)) {
            task_header_set(fe, s.result);
            fe.screen.step = 4;
        }
        break;
    default:
        save_menu_draw(fe, ui);
        break;
    }
}

void save_menu_destroy(FrontEnd&, Ui& ui) {
    ui_music_stop(ui);
}
