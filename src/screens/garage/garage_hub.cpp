// The garage hub (garage_screen_* state 0xD, decomp src/o1/u_01c789.c 0x1C789..0x1CC78): a wheel
// of eight items, the boarded garage's banner, day and money.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

namespace {

constexpr int common_asset = 0x690;
constexpr int garage_asset = 0x674;

std::vector<int> ids(Ui& ui, const char* table) {
    const std::vector<double> v = ui_values(ui.data, "garage", table, 8);
    return {v.begin(), v.end()};
}

// D_001DB49C + 4: the item labels; D_001DB4C0 their strip's position
std::string label(Ui& ui, int i) {
    const UiTable* t = ui_table(ui.data, "garage", "garage_menu_help");
    return t && i + 1 < static_cast<int>(t->strings.size())
               ? t->strings[static_cast<std::size_t>(i + 1)]
               : std::string();
}

void label_init(Ui& ui, GarageHubScreen& h, int i) {
    const std::vector<double> at = ui_values(ui.data, "garage", "garage_menu_help_at", 2);
    const int x = static_cast<int>(at[0]), y = static_cast<int>(at[1]);
    text_strip_init(ui, h.labels[i], x, y, label(ui, i), x, y, x + 0xb4, y + 0x12);
}

// decomp: garage_menu_screen_draw 0x1C789
void draw(FrontEnd& fe, Ui& ui, GarageHubScreen& h) {
    text_strip_draw_scroll(ui, h.labels[h.cursor], 0xff3c3c3c);
    garage_banner_draw(ui, *fe.world, h.timer, 399, 0x102);
    const std::vector<int> items = ids(ui, "garage_menu_items");
    menu_wheel_draw(ui, items.data(), 8, h.shown, h.cursor, h.turn);
    if (!h.turning) {
        sprite_draw_at(ui, ids(ui, "garage_menu_lit")[static_cast<std::size_t>(h.cursor)], 262, 139,
                       4);
    }
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    // garage_menu_sprites with row 0 the item's header
    const UiTable* rows = ui_table(ui.data, "garage", "garage_menu_sprites");
    if (rows && !rows->slots.empty()) {
        std::vector<SpriteSlot> slots = rows->slots;
        slots[0].sprite = ids(ui, "garage_hub_headers")[static_cast<std::size_t>(h.cursor)];
        sprite_rows_draw(ui, slots.data());
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

} // namespace

void garage_hub_spawn(FrontEnd& fe, Ui&) {
    fe.screen.hub.from = fe.previous;
}

void garage_hub_step(FrontEnd& fe, Ui& ui) {
    GarageHubScreen& h = fe.screen.hub;
    switch (fe.screen.step) {
    case 0: { // garage_screen_update
        texture_set_load(ui, common_asset);
        texture_set_load(ui, garage_asset);
        level_meter_spawn(ui, fe.screen.meter, "level_meter_b");
        const int from[6][2] = {{0xe, 0}, {0x18, 0}, {0x1d, 3}, {0x20, 4}, {0x21, 1}, {0x26, 2}};
        h.cursor = h.from == app_system ? 6 : 0;
        for (const auto& f : from) {
            if (h.from == f[0]) {
                h.cursor = f[1];
            }
        }
        h.shown = h.cursor;
        for (int i = 0; i < 8; ++i) {
            label_init(ui, h, i);
        }
        state_music_play(fe, ui, 0xd);
        h.frames = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
        h.timer = -1;
        fade_in_start(ui, static_cast<float>(h.frames));
        if (h.from == app_town_map) {
            ui_sound(ui, bank_sys, se_shop_arrive);
        }
        fe.screen.step = 1;
        break;
    }
    case 1: // garage_screen_step2
        menu_input_mark(ui);
        draw(fe, ui, h);
        if (--h.frames < 0) {
            h.timer = task_timer_get(ui);
            now_playing_spawn(ui, fe.screen.strip, 0, 0x153, 0x17);
            fe.screen.step = 2;
        }
        break;
    case 2: { // garage_screen_step3
        if (!h.turning) {
            if (menu_cursor(ui, h.cursor, 0, 8, 0x17, -1, -1) == 0) {
                menu_wheel_pointer(ui, h.cursor, 8, ids(ui, "garage_menu_items")[0]);
            }
        }
        if (h.cursor != h.shown) {
            if (!h.turning) {
                ui_sound(ui, bank_sys, se_carousel_turn);
                h.turning = true;
                h.turn = 0;
                label_init(ui, h, h.cursor);
            } else if (++h.turn > 8) {
                h.turn = 0;
                h.turning = false;
                h.shown = h.cursor;
            }
        } else if (h.turning && ++h.turn > 8) {
            h.turn = 0;
            h.turning = false;
            h.shown = h.cursor;
        }
        draw(fe, ui, h);
        const int item = ids(ui, "garage_menu_items")[0];
        if (menu_accept_pressed(ui.pad) || (ui.pointer.left && menu_wheel_pick(ui, item) == 0)) {
            ui_sound(ui, bank_sys, h.cursor == 7 ? se_shop_leave : se_menu_accept);
            fade_out_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
            fe.screen.step = 3;
        } else if (menu_cancel(ui)) {
            if (h.cursor != 7) {
                ui_sound(ui, bank_sys, se_carousel_turn);
            }
            h.turn = 0;
            h.shown = h.cursor;
            h.cursor = 7;
        } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_x)) {
            h.cursor = 0;
        } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_y)) {
            h.cursor = 5;
        }
        break;
    }
    case 3: // garage_screen_step4
        draw(fe, ui, h);
        if (fade_is_black(ui)) {
            fe.screen.strip.active = false;
            task_header_set(fe, h.cursor);
            fe.screen.step = 4;
        }
        break;
    default:
        draw(fe, ui, h);
        break;
    }
}

void garage_hub_destroy(FrontEnd&, Ui& ui) {
    ui_music_stop(ui);
}
