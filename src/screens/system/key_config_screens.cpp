// Key config (key_config_screen_* state 0x10, decomp src/o1/u_0201ab.c 0x27C21..0x283C8) and
// screen adjust (adjust_screen_* state 0, src/o1/u_012ff5.c 0x13004..0x1334B). Key config edits
// the original's button config (GameState +0x6018); the PC's key bindings are gubsy's settings.
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/ui/screen_header.hpp"
#include "world/world.hpp"

namespace {

constexpr int common_asset = 0x690;
constexpr int key_config_asset = 0x677; // key_config_tex.bin
constexpr int adjust_asset = 0x666;     // adjust_tex.bin
// key_config_buttons D_001DD470: the pad bit of each button index (A B X Y LT RT white black)
constexpr std::uint16_t button_bits[8] = {0x100, 0x200, 0x400, 0x800, 0x4000, 0x8000, 0x2000, 0x1000};

float half_second(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

std::string key_help_line(Ui& ui, int row) {
    const UiTable* lines = ui_table(ui.data, "key_config", "key_config_help_lines");
    return lines && row < static_cast<int>(lines->strings.size())
               ? lines->strings[static_cast<std::size_t>(row)]
               : "";
}

int table_value(Ui& ui, const char* table, int index) {
    const UiTable* t = ui_table(ui.data, "key_config", table);
    return t && index >= 0 && index < static_cast<int>(t->values.size())
               ? static_cast<int>(t->values[static_cast<std::size_t>(index)])
               : -1;
}

// the rows' plates (key_config_sprites_2[0..12]) as the pointer's items
void key_rects(Ui& ui, PointerRect out[13]) {
    const SpriteSlot* rows = ui_slots(ui.data, "key_config", "key_config_sprites_2");
    for (int i = 0; i < 13; ++i) {
        out[i] = rows ? sprite_rect(ui, i == 12 ? 0x646 : 0x63d, rows[i].x, rows[i].y)
                      : PointerRect{};
    }
}

// decomp: key_config_screen_draw 0x27CF4
void key_config_draw(FrontEnd& fe, Ui& ui) {
    KeyConfigScreen& k = fe.screen.keys;
    sprite_draw_at(ui, 0x638, 0, 0, 1);
    sprite_draw_at(ui, 0x63a, 0, 0, 2);
    sprite_draw_at(ui, 0x639, 0, 376, 2);
    const SpriteSlot* rows = ui_slots(ui.data, "key_config", "key_config_sprites_2");
    if (rows) {
        // the thirteen plates grey and their labels; retail rewrites the table rows each frame
        for (int i = 0; i < 13; ++i) {
            SpriteSlot plate = rows[i];
            plate.color = 0xffa0a0a0;
            plate.blend = 2;
            plate.sprite = i == 12 ? 0x646 : 0x63d;
            plate.flags = 0;
            sprite_row_draw(ui, plate);
            sprite_row_draw(ui, rows[14 + i]);
        }
        SpriteSlot lit = rows[k.row];
        lit.color = 0xffffffff;
        lit.blend = 3;
        lit.flags = 0x2000;
        lit.sprite = k.row == 12 ? 0x647 : 0x63e;
        sprite_row_draw(ui, lit);
    }
    // the values: control type, view change, Newbie mode, the eight buttons
    sprite_draw_at(ui, table_value(ui, "key_config_control_sprites", k.config.control), 216, 104,
                   0x103);
    sprite_draw_at(ui, table_value(ui, "key_config_view_sprites", k.config.view_change), 216, 130,
                   0x103);
    sprite_draw_at(ui, table_value(ui, "key_config_newbie_sprites", k.config.beginner), 216, 156,
                   0x103);
    for (int i = 0; i < button_action_count; ++i) {
        sprite_draw_at(ui, table_value(ui, "key_config_button_icons", k.config.keys[i]), 276,
                       182.0f + 26.0f * static_cast<float>(i), 0x103);
    }
    // the pad diagram cue: the arrows on rows 0..2, the button's highlight on rows 3..10
    if (k.row != 11 && k.row != 12) {
        if (k.row >= 3) {
            const SpriteSlot* pad = ui_slots(ui.data, "key_config", "key_config_sprites");
            const int key = k.config.keys[k.row - 3];
            if (pad && key >= 0 && key < 8) {
                sprite_row_draw(ui, pad[key]);
            }
        } else if (rows) {
            const std::vector<double> arrow_x = ui_values(ui.data, "key_config", "key_config_arrow_x", 8);
            const int which = k.row == 2 ? 1 : 0;
            for (int a = 0; a < 4; ++a) {
                SpriteSlot arrow = rows[28 + a];
                const std::size_t ix = static_cast<std::size_t>(which * 4 + a);
                arrow.x = ix < arrow_x.size() ? static_cast<float>(arrow_x[ix]) : arrow.x;
                arrow.y = static_cast<float>((k.row + 4) * 0x1a);
                sprite_row_draw(ui, arrow);
            }
        }
    }
    if (fe.options_entry_state != app_title) {
        day_draw(ui, *fe.world, 0x108);
        money_draw(ui, *fe.world, 0x108);
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// the frame moves with the shift, three sprites drawn at minus it
// stay put
// decomp: adjust_screen_draw 0x13004
void adjust_draw(FrontEnd& fe, Ui& ui) {
    AdjustScreen& a = fe.screen.adjust;
    const SpriteSlot* rows = ui_slots(ui.data, "adjust", "adjust_sprites");
    if (!rows) {
        return;
    }
    sprite_rows_draw(ui, rows);
    const float base[3][2] = {{67, 384}, {274, 404}, {226, 54}};
    for (int i = 0; i < 3; ++i) {
        SpriteSlot row = rows[2 + i];
        if (row.sprite < 0) {
            break;
        }
        row.x = base[i][0] - static_cast<float>(a.x);
        row.y = base[i][1] - static_cast<float>(a.y);
        sprite_row_draw(ui, row);
    }
}

} // namespace

void key_config_step(FrontEnd& fe, Ui& ui) {
    KeyConfigScreen& k = fe.screen.keys;
    switch (fe.screen.step) {
    case 0: // key_config_screen_update
        k.config = fe.world->profile.options.buttons;
        k.row = 0;
        texture_set_load(ui, common_asset);
        texture_set_load(ui, key_config_asset);
        for (int i = 0; i < 13; ++i) {
            help_strip_init(ui, k.help[i], key_help_line(ui, i));
        }
        // the eight "this button is assigned to ..." strips retail builds are never drawn
        level_meter_spawn(ui, fe.screen.meter);
        state_music_play(fe, ui, 0x10);
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
        fade_in_start(ui, half_second(ui));
        fe.screen.step = 1;
        break;
    case 1: // step2
        menu_input_mark(ui);
        key_config_draw(fe, ui);
        if (fade_is_idle(ui)) {
            fe.screen.step = 2;
        }
        break;
    case 2: { // step3
        PointerRect rects[13];
        key_rects(ui, rects);
        if (menu_cursor(ui, k.row, 0, 13, 0x16, 0, 3, rects)) {
            help_strip_init(ui, k.help[k.row], key_help_line(ui, k.row));
        }
        const bool clicked = ui.pointer.left || ui.pointer.right;
        if (k.row <= 2) {
            int* value = k.row == 0 ? &k.config.control
                         : k.row == 1 ? &k.config.view_change
                                      : &k.config.beginner;
            if (!menu_cursor(ui, *value, 0, 2, 0x17, 0, 3) && ui.pointer.left &&
                pointer_inside(ui.pointer, rects[k.row])) {
                // a click on the row flips its two-way value
                *value = 1 - *value;
                ui_sound(ui, bank_sys, se_menu_cursor_vertical);
                menu_input_mark(ui);
            }
        } else if (k.row <= 10 && !clicked) {
            // the first of the eight buttons pressed (the auto-repeat word) goes to this row; the
            // row holding it takes this row's old one. The mouse buttons, which press the
            // triggers in menus, don't bind here.
            const int slot = k.row - 3;
            for (int i = 0; i < 8; ++i) {
                if (pad_button_test(ui.pad, PadEdge::Repeat, PadMatch::Any, button_bits[i])) {
                    for (int j = 0; j < button_action_count; ++j) {
                        if (j != slot && k.config.keys[j] == i) {
                            k.config.keys[j] = k.config.keys[slot];
                            k.config.keys[slot] = i;
                            break;
                        }
                    }
                    break;
                }
            }
        }
        text_strip_draw_scroll(ui, k.help[k.row], 0xfff0f0f0);
        key_config_draw(fe, ui);
        // accept acts on rows 11 and 12 only; there is no cancel (B is a button here)
        const bool accept =
            k.row >= 11 && (menu_accept_pressed(ui.pad) ||
                            (ui.pointer.left && pointer_inside(ui.pointer, rects[k.row])));
        if (accept && k.row == 11) {
            ui_sound(ui, bank_sys, se_menu_accept);
            for (int i = 0; i < button_action_count; ++i) {
                k.config.keys[i] = default_keys[i];
            }
        } else if (accept && k.row == 12) {
            ui_sound(ui, bank_sys, se_menu_accept);
            fade_out_start(ui, half_second(ui));
            fe.screen.step = 3;
        }
        break;
    }
    case 3: // key_config_screen_fade_out
        menu_input_mark(ui);
        key_config_draw(fe, ui);
        if (fade_is_black(ui)) {
            task_header_set(fe, 0);
            fe.screen.step = 4;
        }
        break;
    default:
        key_config_draw(fe, ui);
        break;
    }
}

void key_config_destroy(FrontEnd& fe, Ui& ui) {
    ui_music_stop(ui);
    // the working copy back to GameState +0x6018 and committed to player 0
    if (fe.screen.step > 0) {
        fe.world->profile.options.buttons = fe.screen.keys.config;
        player_config_commit(*fe.world, 0);
    }
}

void adjust_spawn(FrontEnd& fe, Ui&) {
    fe.screen.adjust.from = fe.previous;
}

void adjust_step(FrontEnd& fe, Ui& ui) {
    AdjustScreen& a = fe.screen.adjust;
    MachineOptions& options = game_options(fe);
    switch (fe.screen.step) {
    case 0: // adjust_screen_update
        texture_set_load(ui, adjust_asset);
        a.x = a.entry_x = options.screen_shift_x;
        a.y = a.entry_y = options.screen_shift_y;
        a.box = msg_box_from(ui, "adjust", "adjust_boxes", 0);
        state_music_play(fe, ui, 0);
        fade_in_start(ui, half_second(ui));
        fe.screen.step = 1;
        break;
    case 1: // step2
        adjust_draw(fe, ui);
        if (fade_is_idle(ui)) {
            fe.screen.step = 2;
        }
        break;
    case 2: { // step3: x by left and right (-32..30, sound 4), y by up and down (-16..14, 3)
        menu_cursor(ui, a.x, -32, 63, 0x13, 0, 4);
        menu_cursor(ui, a.y, -16, 31, 0x12, 0, 3);
        if (pad_button_test(ui.pad, PadEdge::Repeat, PadMatch::Any, 0x400)) {
            a.x = 0; // X centres it, silently
            a.y = 0;
        }
        adjust_draw(fe, ui);
        if (menu_accept(ui)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            a.answer = 1;
            // retail never reopens this box, so later visits show it full size at once and
            // silent (docs/bugs.md); the port opens it each time
            msg_box_open(a.box);
            fe.screen.step = 3;
        } else {
            options.screen_shift_x = a.x; // live
            options.screen_shift_y = a.y;
        }
        break;
    }
    case 3: { // step4: apply?
        PointerRect answers[2];
        msg_box_choice_rects(ui, a.box, answers);
        menu_cursor(ui, a.answer, 0, 2, 0x17, 0, 3, a.box.row.done ? answers : nullptr);
        adjust_draw(fe, ui);
        msg_box_update_choice(ui, a.box, a.answer);
        if (menu_accept(ui, &answers[a.answer])) {
            ui_sound(ui, bank_sys, se_menu_accept);
            if (a.answer != 0) {
                options.screen_shift_x = a.entry_x;
                options.screen_shift_y = a.entry_y;
            }
            fade_out_start(ui, half_second(ui));
            fe.screen.step = 4;
        } else if (menu_cancel(ui) && a.answer != 1) {
            a.answer = 1;
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        }
        break;
    }
    case 4: // step5
        adjust_draw(fe, ui);
        msg_box_update_choice(ui, a.box, a.answer);
        if (fade_is_black(ui)) {
            task_header_set(fe, 0);
            fe.screen.step = 5;
        }
        break;
    default:
        adjust_draw(fe, ui);
        break;
    }
}

void adjust_destroy(FrontEnd&, Ui& ui) {
    ui_music_stop(ui);
}
