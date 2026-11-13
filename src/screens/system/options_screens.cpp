// The options (options_screen_* state 0x1A, decomp src/o1/u_02e2fc.c) and the sound config
// (sound_config_screen_* 0x23, src/o1/u_0357d7.c).
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "talk/draw2d.hpp"

#include <vector>

namespace {

// option_tex.bin, sound_config_tex.bin, scr_common_tex.bin
constexpr int option_asset = 0x67c;
constexpr int sound_asset = 0x685;
constexpr int common_asset = 0x690;
// AWAKETO1.WAV, the sound config's test music (music_play(0x1C, 0, 1.0))
constexpr int test_track = 0x1c;
// D_001FDCE4 + 0x2C: each sound config row's value range (lo, count)
constexpr int value_range[4][2] = {{0, 256}, {0, 256}, {0, 150}, {0, 1}};
// D_00253710: the sound test's ids run through battle_player (51), battle_old (3), battle (48)
// and sys (48), so test bank 0 is SE bank 3 and so on
constexpr int test_bank_sizes[4] = {51, 3, 48, 48};

float half_second(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

void widgets_update(Ui& ui, Screen& screen) {
    level_meter_update(ui, screen.meter);
    now_playing_update(ui, screen.strip);
}

// options_item_sprites: a (lit, normal) pair per item; options_item_positions: (x, y) per item
struct OptionsItems {
    std::vector<double> ids, at;
};
OptionsItems options_items(Ui& ui) {
    return {ui_values(ui.data, "options", "options_item_sprites", 12),
            ui_values(ui.data, "options", "options_item_positions", 12)};
}

// the port adds PC CONFIG (docs/settings.md) as item 5, before End: seven items as far apart as
// retail's six (options_item_positions: 43, from y 121), the column centred where retail's was,
// so half a step higher: y 99.5 to 397.5, under the header's rule and over the footer's
constexpr int item_count = 7;
constexpr int item_pc = 5, item_end = 6;
// the original's row of an item, -1 for PC CONFIG
int item_row(int item) {
    return item < item_pc ? item : item == item_end ? 5 : -1;
}
float item_x(const OptionsItems& items) {
    return static_cast<float>(items.at[0]);
}
float item_y(const OptionsItems& items, int item) {
    const float top = static_cast<float>(items.at[1]);
    const float step = static_cast<float>(items.at[3] - items.at[1]);
    return top - step * 0.5f + step * static_cast<float>(item);
}
std::string item_help(Ui& ui, int item) {
    if (item == item_pc) {
        return pc_options_help();
    }
    const auto* help = ui_table(ui.data, "options", "options_help_lines");
    return help ? help->strings[static_cast<std::size_t>(item_row(item))] : std::string();
}

void options_rects(const OptionsItems& items, PointerRect out[item_count]) {
    for (int i = 0; i < item_count; ++i) {
        out[i] = pc_button_rect(item_x(items), item_y(items, i));
    }
}

// options_screen_draw: the six items at layer 3, the chosen one lit at 4 (pulsing while the menu
// takes input), day and money unless opened from the title, the frame, the help line
void options_draw(FrontEnd& fe, Ui& ui, bool pulse) {
    OptionsScreen& o = fe.screen.options;
    const OptionsItems items = options_items(ui);
    for (int i = 0; i < item_count; ++i) {
        const int row = item_row(i);
        if (row < 0) {
            pc_options_item_draw(fe, ui, item_x(items), item_y(items, i), false, 3, 0);
            continue;
        }
        Sprite sprite =
            sprite_of(ui, static_cast<int>(items.ids[static_cast<std::size_t>(row * 2 + 1)]));
        sprite.pos[0] = item_x(items);
        sprite.pos[1] = item_y(items, i);
        sprite.blend = 3;
        sprite_submit(ui, sprite);
    }
    const int lit_row = item_row(o.cursor);
    if (lit_row < 0) {
        pc_options_item_draw(fe, ui, item_x(items), item_y(items, o.cursor), true, 4,
                             pulse ? 0x2000 : 0);
    } else {
        Sprite lit =
            sprite_of(ui, static_cast<int>(items.ids[static_cast<std::size_t>(lit_row * 2)]));
        lit.pos[0] = item_x(items);
        lit.pos[1] = item_y(items, o.cursor);
        lit.blend = 4;
        lit.flags |= pulse ? 0x2000 : 0;
        sprite_submit(ui, lit);
    }
    if (fe.options_entry_state != app_title) {
        day_draw(ui, *fe.world, 0x108);
        money_draw(ui, *fe.world, 0x108);
    }
    sprite_rows_draw(ui, ui_slots(ui.data, "options", "options_sprites"));
    text_strip_draw_scroll(ui, o.help[o.cursor], 0xfff0f0f0);
    widgets_update(ui, fe.screen);
}

// vibration cleared, the volumes back to 255 and 225 (sound_volumes_default),
// the button config and the screen shift reset (the camera view kept)
// decomp: options_defaults_set 0x2E66A
void options_defaults_set(FrontEnd& fe, Ui& ui) {
    MachineOptions& options = game_options(fe);
    options = MachineOptions{};
    fe.world->profile.options.buttons = ButtonConfig{};
    if (ui.audio) {
        set_music_level(*ui.audio, options.music_volume);
        set_sound_level(*ui.audio, options.se_volume);
    }
}

// the sound config's rows: D_001FDD30 (lit, normal) pairs, the lit one at D_001FDE68
struct SoundItems {
    std::vector<double> ids, at, values_at;
};
SoundItems sound_items(Ui& ui) {
    return {ui_values(ui.data, "sound_config", "sound_config_items", 8),
            ui_values(ui.data, "sound_config", "sound_config_items_at", 8),
            ui_values(ui.data, "sound_config", "sound_config_value_positions", 8)};
}

void sound_rects(Ui& ui, const SoundItems& items, PointerRect out[4]) {
    for (int i = 0; i < 4; ++i) {
        out[i] = sprite_rect(ui, static_cast<int>(items.ids[static_cast<std::size_t>(i * 2)]),
                             static_cast<float>(items.at[static_cast<std::size_t>(i * 2)]),
                             static_cast<float>(items.at[static_cast<std::size_t>(i * 2 + 1)]));
        out[i].right = 640.0f - 32.0f; // the row runs on to its value
    }
}

// sound_config_screen_draw: the frame, the three values (the volumes with their bars), the lit
// row, day and money unless the options came from the title, the help line
void sound_draw(FrontEnd& fe, Ui& ui) {
    SoundConfigScreen& s = fe.screen.sound;
    const SoundItems items = sound_items(ui);
    sprite_rows_draw(ui, ui_slots(ui.data, "sound_config", "sound_config_sprites"));
    for (int i = 0; i < 3; ++i) {
        const int v = s.values[i];
        number_draw(ui, 0x18, v, static_cast<int>(items.values_at[static_cast<std::size_t>(i * 2)]),
                    static_cast<int>(items.values_at[static_cast<std::size_t>(i * 2 + 1)]), 0x108,
                    0xffffffff, 3, 3);
        if (i < 2) {
            // sound_config_bar_draw: 0x9DB at (312, y) stretched to 32 * v / 256
            Sprite bar = sprite_of(ui, 0x9db);
            bar.pos[0] = 312.0f;
            bar.pos[1] = i == 0 ? 148.0f : 191.0f;
            bar.scale[0] = 32.0f * (static_cast<float>(v) / 256.0f);
            bar.blend = 0x108;
            sprite_submit(ui, bar);
        }
    }
    Sprite lit = sprite_of(ui, static_cast<int>(items.ids[static_cast<std::size_t>(s.cursor * 2)]));
    lit.flags |= sprite_blink_soft;
    lit.pos[0] = static_cast<float>(items.at[static_cast<std::size_t>(s.cursor * 2)]);
    lit.pos[1] = static_cast<float>(items.at[static_cast<std::size_t>(s.cursor * 2 + 1)]);
    lit.blend = 7;
    sprite_submit(ui, lit);
    if (fe.options_entry_state != app_title) {
        day_draw(ui, *fe.world, 0x108);
        money_draw(ui, *fe.world, 0x108);
    }
    text_strip_draw_scroll(ui, s.help[s.cursor], 0xfff0f0f0);
    widgets_update(ui, fe.screen);
}

// every effect stopped, then the id's cue across the four banks
// decomp: sound_test_play 0x3B232
void sound_test_play(Ui& ui, int id) {
    if (!ui.audio) {
        return;
    }
    int bank = 0;
    while (bank < 4 && id >= test_bank_sizes[bank]) {
        id -= test_bank_sizes[bank];
        ++bank;
    }
    if (bank >= 4) {
        return;
    }
    stop_all_sounds(*ui.audio);
    const int se_bank = 3 - bank;
    const unsigned flags = sound_is_3d(*ui.audio, se_bank, id) ? static_cast<unsigned>(sound_positioned) : 0u;
    const float origin[3] = {0, 0, 0};
    play_sound(*ui.audio, se_bank, id, flags, flags ? origin : nullptr);
}

} // namespace

void options_spawn(FrontEnd& fe, Ui&) {
    OptionsScreen& o = fe.screen.options;
    o.from = fe.previous;
    if (o.from == app_title || o.from == app_system) {
        fe.options_entry_state = o.from;
    }
}

void options_step(FrontEnd& fe, Ui& ui) {
    OptionsScreen& o = fe.screen.options;
    switch (fe.screen.step) {
    case 0: // options_screen_update
        texture_set_load(ui, common_asset);
        texture_set_load(ui, option_asset);
        level_meter_spawn(ui, fe.screen.meter);
        o.cursor = o.from == app_adjust         ? 3
                   : o.from == app_key_config   ? 2
                   : o.from == app_sound_config ? 1
                   : o.from == app_pc_config    ? item_pc
                                                : 0;
        o.confirm = 1;
        o.choice = game_options(fe).vibration;
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
        for (int i = 0; i < item_count; ++i) {
            help_strip_init(ui, o.help[i], item_help(ui, i));
        }
        for (int i = 0; i < 3; ++i) {
            o.boxes[i] = msg_box_from(ui, "options", "options_boxes", i);
        }
        state_music_play(fe, ui, 0x1a);
        fade_in_start(ui, half_second(ui));
        fe.screen.step = 1;
        break;
    case 1: // options_fade_in_update
        options_draw(fe, ui, false);
        if (fade_is_idle(ui)) {
            fe.screen.step = 2;
        }
        break;
    case 2: { // options_menu_update
        const OptionsItems items = options_items(ui);
        PointerRect rects[item_count];
        options_rects(items, rects);
        if (menu_cursor(ui, o.cursor, 0, item_count, 0x16, 0, 3, rects)) {
            help_strip_init(ui, o.help[o.cursor], item_help(ui, o.cursor));
        }
        options_draw(fe, ui, true);
        if (menu_accept(ui, &rects[o.cursor])) {
            ui_sound(ui, bank_sys, se_menu_accept);
            switch (o.cursor) {
            case 0:
                // retail never reopens this box, so from the second time on it shows already
                // open, without its opening; the port opens it each time (docs/bugs.md)
                msg_box_open(o.boxes[2]);
                fe.screen.step = 3;
                break;
            case 1:
            case 2:
            case 3:
                o.result = o.cursor - 1; // sound config, key config, screen adjust
                fade_out_start(ui, half_second(ui));
                fe.screen.step = 6;
                break;
            case 4:
                fe.screen.step = 4;
                break;
            case item_pc: // the port's PC CONFIG (state 0xA0)
                o.result = 5;
                fade_out_start(ui, half_second(ui));
                fe.screen.step = 6;
                break;
            case item_end: // End: back to where options were opened from
                o.result = fe.options_entry_state == app_title    ? 3
                           : fe.options_entry_state == app_system ? 4
                                                                  : 0;
                fade_out_start(ui, half_second(ui));
                fe.screen.step = 6;
                break;
            }
        } else if (menu_cancel(ui) && o.cursor != item_end) {
            // cancel moves to End instead of leaving
            o.cursor = item_end;
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            help_strip_init(ui, o.help[item_end], item_help(ui, item_end));
        }
        break;
    }
    case 3: { // options_vibration_update
        PointerRect answers[2];
        msg_box_choice_rects(ui, o.boxes[2], answers);
        menu_cursor(ui, o.choice, 0, 2, 0x17, 0, 3, o.boxes[2].row.done ? answers : nullptr);
        options_draw(fe, ui, false);
        msg_box_update_choice(ui, o.boxes[2], o.choice);
        if (menu_accept(ui, &answers[o.choice])) {
            ui_sound(ui, bank_sys, se_menu_accept);
            game_options(fe).vibration = o.choice;
            fe.screen.step = 2;
        } else if (menu_cancel(ui) && o.choice != 1) {
            o.choice = 1;
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        }
        break;
    }
    case 4: { // options_defaults_confirm_update
        PointerRect answers[2];
        msg_box_choice_rects(ui, o.boxes[0], answers);
        menu_cursor(ui, o.confirm, 0, 2, 0x17, 0, 3, o.boxes[0].row.done ? answers : nullptr);
        options_draw(fe, ui, false);
        msg_box_update_choice(ui, o.boxes[0], o.confirm);
        if (menu_accept(ui, &answers[o.confirm])) {
            ui_sound(ui, bank_sys, se_menu_accept);
            if (o.confirm == 0) {
                options_defaults_set(fe, ui);
                o.choice = 0;
                msg_box_open(o.boxes[0]);
                fe.screen.step = 5;
            } else {
                msg_box_open(o.boxes[0]);
                fe.screen.step = 2;
            }
        }
        if (menu_cancel(ui) && o.confirm != 1) {
            o.confirm = 1;
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        }
        break;
    }
    case 5: { // options_defaults_done_update
        options_draw(fe, ui, false);
        msg_box_update_ok(ui, o.boxes[1]);
        const PointerRect ok = msg_box_ok_rect(ui, o.boxes[1]);
        if (menu_accept(ui, &ok)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            msg_box_open(o.boxes[1]);
            fe.screen.step = 2;
        }
        break;
    }
    case 6: // options_screen_leave
        options_draw(fe, ui, false);
        if (fade_is_black(ui)) {
            task_header_set(fe, o.result);
            fe.screen.step = 7;
        }
        break;
    default:
        options_draw(fe, ui, false);
        break;
    }
}

void options_destroy(FrontEnd&, Ui& ui) {
    ui_music_stop(ui);
}

void sound_config_step(FrontEnd& fe, Ui& ui) {
    SoundConfigScreen& s = fe.screen.sound;
    const auto* help = ui_table(ui.data, "sound_config", "sound_config_help_lines");
    auto help_line = [&](int i) {
        return help && i < static_cast<int>(help->strings.size())
                   ? help->strings[static_cast<std::size_t>(i)]
                   : std::string();
    };
    switch (fe.screen.step) {
    case 0: { // sound_config_screen_update
        texture_set_load(ui, common_asset);
        texture_set_load(ui, sound_asset);
        level_meter_spawn(ui, fe.screen.meter);
        s.cursor = 0;
        ui_music(ui, test_track, false);
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
        s.values[0] = game_options(fe).music_volume;
        s.values[1] = game_options(fe).se_volume;
        s.values[2] = 0;
        if (ui.audio) {
            const float origin[3] = {0, 0, 0}, front[3] = {0, 0, 1}, up[3] = {0, 1, 0};
            set_listener(*ui.audio, origin, front, up);
        }
        for (int i = 0; i < 4; ++i) {
            help_strip_init(ui, s.help[i], help_line(i));
        }
        fade_in_start(ui, half_second(ui));
        fe.screen.step = 1;
        break;
    }
    case 1: // step2
        menu_input_mark(ui);
        sound_draw(fe, ui);
        if (fade_is_idle(ui)) {
            fe.screen.step = 2;
        }
        break;
    case 2: { // step3: the row, then its value, applied live
        const SoundItems items = sound_items(ui);
        PointerRect rects[4];
        sound_rects(ui, items, rects);
        // the wheel over a volume or the test steps that value instead of the row (the wheel is
        // the d-pad's up and down in the bindings)
        const bool wheel_value = ui.pointer.wheel != 0 && s.cursor < 3 &&
                                 pointer_inside(ui.pointer, rects[s.cursor]);
        int row = s.cursor;
        if (!wheel_value && menu_cursor(ui, row, 0, 4, 0x16, 0, 3, rects)) {
            s.cursor = row;
            help_strip_init(ui, s.help[s.cursor], help_line(s.cursor));
        }
        int v = s.values[s.cursor];
        const int lo = value_range[s.cursor][0], count = value_range[s.cursor][1];
        // the volumes stop at 0 and 255 (retail's 0x17 wraps them on a fresh press, so left at 0
        // jumped to full volume: docs/bugs.md); the sound test's number still wraps
        const unsigned flags = s.cursor < 2 ? cursor_value_horizontal : cursor_menu_horizontal;
        int step = menu_cursor(ui, v, lo, count, flags, 0, 3);
        if (wheel_value) {
            v = std::min(std::max(v + ui.pointer.wheel, lo), lo + count - 1);
            step = v != s.values[s.cursor] ? 1 : 0;
            if (step) {
                ui_sound(ui, bank_sys, se_menu_cursor_vertical);
                menu_input_mark(ui);
            }
        }
        if (step) {
            if (s.cursor == 0 && ui.audio) {
                set_music_level(*ui.audio, v);
            } else if (s.cursor == 1 && ui.audio) {
                set_sound_level(*ui.audio, v);
            }
            s.values[s.cursor] = v;
        }
        sound_draw(fe, ui);
        if (menu_accept(ui, &rects[s.cursor])) {
            if (s.cursor == 2) {
                sound_test_play(ui, s.values[2]);
            } else if (s.cursor == 3) {
                ui_sound(ui, bank_sys, se_menu_accept);
                fade_out_start(ui, half_second(ui));
                fe.screen.step = 3;
            }
        } else if (menu_cancel(ui)) {
            if (s.cursor != 3) {
                ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            }
            s.cursor = 3;
        }
        break;
    }
    case 3: // step4
        menu_input_mark(ui);
        sound_draw(fe, ui);
        if (fade_is_black(ui)) {
            task_header_set(fe, 0);
            fe.screen.step = 4;
        }
        break;
    default:
        sound_draw(fe, ui);
        break;
    }
}

void sound_config_destroy(FrontEnd& fe, Ui& ui) {
    // the values become the options (GameState +0x9CB, +0x9CC); effects stopped
    game_options(fe).music_volume = fe.screen.sound.values[0];
    game_options(fe).se_volume = fe.screen.sound.values[1];
    if (ui.audio) {
        stop_all_sounds(*ui.audio);
    }
    ui.clear_colour = 0xff000000;
}
