// "Press start" and the title menu: title_return_screen_* (state 0x28, decomp src/o1/u_03c67e.c)
// and title_screen_* (0x15, src/o1/u_028e51.c), over the shared backdrop (title_backdrop_draw).
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "save/save.hpp"
#include "screens/flow/front_end.hpp"
#include "story/rumble.hpp"
#include "talk/draw2d.hpp"
#include "world/world.hpp"

#include <vector>

namespace {

// title_tex.bin and scr_common_tex.bin
constexpr int title_asset = 0x689;
constexpr int common_asset = 0x690;
// D_00199F70 640.0, D_0017AB54 54.0: the two bands stretched across the screen
constexpr float band_width = 640.0f;
constexpr float band_height = 54.0f;

// a column of 20 copies of 0xA54 at x 320 scrolling down by 0.2 an update
// decomp: title_logo_draw 0x3E5C1
void title_logo_draw(Ui& ui, float& clock) {
    for (int k = 0; k < 0x1e0; k += 0x18) {
        Sprite sprite = sprite_of(ui, 0xa54);
        sprite.pos[0] = 320.0f;
        sprite.pos[1] = static_cast<float>((static_cast<int>(clock) + k) % 0x1e0);
        sprite.flags |= 8;
        sprite.blend = 4;
        sprite_submit(ui, sprite);
    }
    clock += 0.2f;
    if (clock > 640.0f) {
        clock = 0.0f;
    }
}

// the rows, the top and bottom bands, the scrolling column
// decomp: title_backdrop_draw 0x3E64D
void title_backdrop_draw(Ui& ui, float& clock) {
    sprite_rows_draw(ui, ui_slots(ui.data, "title", "title_backdrop_sprites"));
    const float sx = band_width / static_cast<float>(sprite_width(ui, 0xa55));
    const float sy = band_height / static_cast<float>(sprite_height(ui, 0xa55));
    for (float y : {0.0f, 427.0f}) {
        Sprite band = sprite_of(ui, 0xa55);
        band.pos[1] = y;
        band.blend = 0x103;
        band.scale[0] = sx;
        band.scale[1] = sy;
        sprite_submit(ui, band);
    }
    title_logo_draw(ui, clock);
}

// title_return_screen_draw: "press start" blinking (0x4000) at (225, 335), then the backdrop
void title_return_draw(Ui& ui, TitleScreen& t) {
    Sprite caption = sprite_of(ui, 0xe91);
    caption.pos[0] = 225.0f;
    caption.pos[1] = 335.0f;
    caption.flags |= sprite_blink_hard;
    caption.blend = 0x108;
    sprite_submit(ui, caption);
    title_backdrop_draw(ui, t.logo_clock);
}

// the five items: D_001F93DC (lit, normal) id pairs and D_001F9404 positions; 0..2 the title's,
// 3..4 New Game and Load
struct TitleItems {
    std::vector<double> ids;
    std::vector<double> at;
};
TitleItems title_items(Ui& ui) {
    return {ui_values(ui.data, "title", "title_menu_sprites", 10),
            ui_values(ui.data, "title", "title_menu_at", 10)};
}

void item_rects(Ui& ui, const TitleItems& items, int first, int count, PointerRect* out) {
    for (int i = 0; i < count; ++i) {
        const int k = first + i;
        out[i] = sprite_rect(ui, static_cast<int>(items.ids[static_cast<std::size_t>(k * 2 + 1)]),
                             static_cast<float>(items.at[static_cast<std::size_t>(k * 2)]),
                             static_cast<float>(items.at[static_cast<std::size_t>(k * 2 + 1)]));
    }
}

void item_draw(Ui& ui, const TitleItems& items, int k, bool lit, int blend, std::uint32_t color,
               std::uint32_t flags) {
    Sprite sprite =
        sprite_of(ui, static_cast<int>(items.ids[static_cast<std::size_t>(k * 2 + (lit ? 0 : 1))]));
    sprite.pos[0] = static_cast<float>(items.at[static_cast<std::size_t>(k * 2)]);
    sprite.pos[1] = static_cast<float>(items.at[static_cast<std::size_t>(k * 2 + 1)]);
    sprite.blend = blend;
    sprite.color = color;
    sprite.flags |= flags;
    sprite_submit(ui, sprite);
}

// title_screen_draw: the three items at layer 6 (VS greyed with one pad), the chosen one lit at 7
void title_draw(Ui& ui, TitleScreen& t) {
    const TitleItems items = title_items(ui);
    for (int i = 0; i < 3; ++i) {
        const bool grey = i == 1 && ui.pads_connected <= 1;
        item_draw(ui, items, i, false, 6, grey ? 0xff404040 : 0xffffffff, 0);
        if (t.cursor == i) {
            item_draw(ui, items, i, true, 7, 0xffffffff, 0x2000);
        }
    }
    title_backdrop_draw(ui, t.logo_clock);
}

// New Game and Load on the plate, the chosen one lit (0x2000)
// decomp: title_menu_draw 0x2BFDA
void title_menu_draw(Ui& ui, TitleScreen& t) {
    const TitleItems items = title_items(ui);
    icon_on_plate_draw(ui, 0x11c, 0x177, 0x101, 0xc3d, 0x14);
    for (int i = 3; i < 5; ++i) {
        item_draw(ui, items, i, false, 0x101, 0xffffffff, 0);
        if (t.sub_cursor == i) {
            item_draw(ui, items, i, true, 0x101, 0xffffffff, 0x2000);
        }
    }
    title_backdrop_draw(ui, t.logo_clock);
}

float half_second(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

} // namespace

void title_return_spawn(FrontEnd& fe, Ui& ui) {
    TitleScreen& t = fe.screen.title;
    t.from = fe.previous;
    t.timer = ui.frame_cadence * 0x1e;
    t.leave = -1;
    if (t.from == app_title) {
        title_return_draw(ui, t);
    }
}

void title_return_step(FrontEnd& fe, Ui& ui) {
    TitleScreen& t = fe.screen.title;
    switch (fe.screen.step) {
    case 0: // title_return_screen_update
        if (t.from != app_title) {
            texture_set_load(ui, common_asset);
            texture_set_load(ui, title_asset);
        }
        t.logo_clock = 0.0f;
        if (t.from != app_title) {
            state_music_play(fe, ui, 0x28);
            fade_in_start(ui, half_second(ui));
            fe.screen.step = 1;
        } else {
            title_return_draw(ui, t);
            fe.screen.step = 2;
        }
        break;
    case 1: // step2: the fade in
        title_return_draw(ui, t);
        if (fade_is_idle(ui)) {
            fe.screen.step = 2;
        }
        break;
    case 2: { // step3: START on any pad (or a click); 30 s without it, the attract demo. The
        // port takes A too, so Space confirms here as Enter (Start) does: the PC's two confirm
        // keys (the pad record can't tell Space from a pad's A, so the pads' A works as well)
        const bool start = menu_accept_pressed(ui.pad) || ui.pointer.left;
        title_return_draw(ui, t);
        if (start) {
            // input_mask: the pad that pressed START is the one the lost-controller box watches
            pad_watch_title_start(fe);
            t.leave = 0x15;
            ui_sound(ui, bank_sys, se_press_start);
            task_header_set(fe, 1);
            fe.screen.step = 4;
        } else if (--t.timer <= 0) {
            t.leave = 1;
            fade_out_start(ui, half_second(ui));
            fe.screen.step = 3;
        }
        break;
    }
    case 3: // step4
        title_return_draw(ui, t);
        if (fade_is_black(ui)) {
            task_header_set(fe, 0);
        }
        break;
    default:
        title_return_draw(ui, t);
        break;
    }
}

void title_return_destroy(FrontEnd& fe, Ui& ui) {
    TitleScreen& t = fe.screen.title;
    if (t.leave != 0x15) {
        ui_music_stop(ui);
    } else {
        title_return_draw(ui, t); // no blank frame on the way to the title
    }
}

void title_spawn(FrontEnd& fe, Ui& ui) {
    TitleScreen& t = fe.screen.title;
    t.from = fe.previous;
    t.leave = -1;
    t.cursor = (t.from == app_battle || t.from == app_vs_area_select) ? 1
               : t.from == app_options                                ? 2
                                                                      : 0;
    // 4 (Load) when a slot holds a save
    t.sub_cursor = 3;
    for (int slot = 0; slot < save_slot_count; ++slot) {
        SaveImage image;
        std::string error;
        if (save_slot_read(fe.save_dir, slot, image, error) == SaveRead::Ok && image.present) {
            t.sub_cursor = 4;
        }
    }
    if (t.from == app_title_return) {
        title_draw(ui, t);
    }
}

void title_step(FrontEnd& fe, Ui& ui) {
    TitleScreen& t = fe.screen.title;
    const TitleItems items = title_items(ui);
    switch (fe.screen.step) {
    case 0: // title_screen_update
        if (t.from != app_title_return) {
            texture_set_load(ui, common_asset);
            texture_set_load(ui, title_asset);
        }
        t.timer = static_cast<int>(static_cast<float>(ui.frame_cadence) * 75.0f);
        t.logo_clock = 0.0f;
        if (t.from != app_title_return) {
            state_music_play(fe, ui, 0x15);
            fade_in_start(ui, half_second(ui));
            fe.screen.step = 1;
        } else {
            title_draw(ui, t);
            fe.screen.step = 2;
        }
        break;
    case 1: // step2: the fade in, back into the New Game / Load menu from the load screen
        if (t.from != app_save_load) {
            title_draw(ui, t);
        } else {
            title_menu_draw(ui, t);
        }
        if (fade_is_idle(ui)) {
            fe.screen.step = t.from != app_save_load ? 2 : 3;
        }
        break;
    case 2: { // step3: the three items
        --t.timer;
        PointerRect rects[3];
        item_rects(ui, items, 0, 3, rects);
        menu_cursor(ui, t.cursor, 0, 3, 0x16, 0, 3, rects);
        title_draw(ui, t);
        if (menu_accept(ui, &rects[t.cursor])) {
            switch (t.cursor) {
            case 0:
                t.timer = static_cast<int>(static_cast<float>(ui.frame_cadence) * 75.0f);
                fe.screen.step = 3;
                ui_sound(ui, bank_sys, se_menu_accept);
                break;
            case 1: // VS needs a second controller
                if (ui.pads_connected > 1) {
                    t.result = 2;
                    fade_out_start(ui, half_second(ui));
                    fe.screen.step = 4;
                    ui_sound(ui, bank_sys, se_menu_accept);
                } else {
                    ui_sound(ui, bank_sys, se_menu_cancel);
                }
                break;
            case 2:
                t.result = 3;
                fade_out_start(ui, half_second(ui));
                fe.screen.step = 4;
                ui_sound(ui, bank_sys, se_menu_accept);
                break;
            }
            t.leave = -1;
        } else if (menu_cancel(ui) || t.timer < 0) {
            if (t.timer >= 0) {
                ui_sound(ui, bank_sys, se_menu_cancel);
            }
            // back to "press start" without a fade
            t.leave = 0x28;
            task_header_set(fe, 4);
            fe.screen.step = 5;
        }
        break;
    }
    case 3: { // step4: New Game or Load
        PointerRect rects[2];
        item_rects(ui, items, 3, 2, rects);
        const bool accept = menu_accept(ui, &rects[t.sub_cursor - 3]);
        if (accept) {
            ui_sound(ui, bank_sys, se_menu_accept);
        }
        --t.timer;
        menu_cursor(ui, t.sub_cursor, 3, 2, 0x16, 0, 3, rects);
        title_menu_draw(ui, t);
        if (accept) {
            if (t.sub_cursor == 3) {
                // game_state_new, then new_game_reset (the Burning Chrome roll only matters
                // once the game is beaten): retail runs only the second, which carries an earlier
                // game over once the title can be reached again (docs/bugs.md)
                game_state_new(*fe.world, fe.story_tables);
                *fe.machine = MachineOptions{}; // game_state_new's options defaults
                music_state_new(fe.music, fe.world->story, fe.world->profile.music);
                rumble_contexts_reset(*fe.world, fe.story_tables, fe.campaign.roster);
                story_new_game(fe.world->story, fe.story_tables, 0);
                // new_game_reset's tournaments_refill: today's line-up with drawn opponents
                tournaments_refill(*fe.world, fe.story_tables, fe.campaign.roster, 0);
                talk_pilot_name_update(fe);
                dev_world_seed(fe); // headless runs' garages and money
                t.result = 0;
            } else {
                t.result = 1;
            }
            fade_out_start(ui, half_second(ui));
            fe.screen.step = 4;
        } else if (menu_cancel(ui)) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            // retail keeps counting the idle time here and checks it only back in the three
            // items, so a player who waited 75 s and cancels is thrown out to "press start" at
            // once; the port gives the menu its full 75 s again (docs/bugs.md)
            t.timer = static_cast<int>(static_cast<float>(ui.frame_cadence) * 75.0f);
            fe.screen.step = 2;
        }
        break;
    }
    case 4: // step5
        if (t.result == 0 || t.result == 1) {
            title_menu_draw(ui, t);
        } else {
            title_draw(ui, t);
        }
        if (fade_is_black(ui)) {
            task_header_set(fe, t.result);
        }
        break;
    default:
        title_draw(ui, t);
        break;
    }
}

void title_destroy(FrontEnd& fe, Ui& ui) {
    TitleScreen& t = fe.screen.title;
    if (t.leave != 0x28) {
        ui_music_stop(ui);
    } else {
        title_draw(ui, t);
    }
}
