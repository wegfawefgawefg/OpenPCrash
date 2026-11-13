// Club Wired (state 8, decomp src/o1/u_012ff5.c club_wired_screen_* 0x1B847..0x1BEC1;
// docs/port/screens.md "Club Wired"): the six-item wheel over the jobs, the help menu, records,
// schedule, the alias and the way out to the map.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/shops/music_player.hpp"
#include "world/world.hpp"

namespace {

constexpr int common_asset = 0x690;
constexpr int club_asset = 0x66b; // club_wird_tex
constexpr float quarter_pi = 0.7853982f;

std::vector<double> item_sprites(Ui& ui) { // club_wired_item_sprites
    return ui_values(ui.data, "club_wired", "club_wired_item_sprites", 6);
}
std::vector<double> lit_sprites(Ui& ui) { // club_wired_item_lit_sprites
    return ui_values(ui.data, "club_wired", "club_wired_item_lit_sprites", 6);
}
std::vector<double> headers(Ui& ui) { // club_wired_headers
    return ui_values(ui.data, "club_wired", "club_wired_headers", 6);
}

void line_init(Ui& ui, ClubWiredScreen& c, int item) {
    // 11-character windows at club_wired_label_pos, clipped to 180 x 18
    const std::vector<double> at = ui_values(ui.data, "club_wired", "club_wired_label_pos", 2);
    const int x = static_cast<int>(at[0]);
    const int y = static_cast<int>(at[1]);
    const UiTable* lines = ui_table(ui.data, "club_wired", "club_wired_item_lines");
    const std::string text = lines && item < static_cast<int>(lines->strings.size())
                                 ? lines->strings[static_cast<std::size_t>(item)]
                                 : std::string();
    text_strip_init(ui, c.lines[item], x, y, text, x, y, x + 0xb4, y + 0x12);
}

// decomp: club_wired_screen_draw 0x1B847
void club_draw(FrontEnd& fe, Ui& ui, ClubWiredScreen& c) {
    garage_banner_draw(ui, *fe.world, c.banner_timer, 399, 0x102);
    text_strip_draw_scroll(ui, c.lines[c.cursor], 0xfff2e8e8);
    const std::vector<double> items = item_sprites(ui);
    for (int i = c.shown - 1; i < c.shown + 7; ++i) {
        const float a = static_cast<float>(i - c.shown) * quarter_pi;
        float d;
        if (c.shown == 0 && c.cursor == 5) {
            d = quarter_pi;
        } else if (c.shown == 5 && c.cursor == 0) {
            d = -quarter_pi;
        } else {
            d = static_cast<float>(i - c.cursor) * quarter_pi - a;
        }
        const int k = i < 0 ? 5 : i % 6;
        Sprite s = sprite_of(ui, static_cast<int>(items[static_cast<std::size_t>(k)]));
        s.rot[2] = static_cast<float>(c.turn_frame) * d * 0.125f + a;
        s.pos[0] = 262.0f;
        s.pos[1] = 139.0f;
        s.blend = 3;
        s.rot[0] = 159.5f;
        s.rot[1] = 80.5f;
        sprite_submit(ui, s);
    }
    if (!c.turning) {
        Sprite s = sprite_of(ui, static_cast<int>(lit_sprites(ui)[static_cast<std::size_t>(c.cursor)]));
        s.pos[0] = 262.0f;
        s.pos[1] = 139.0f;
        s.blend = 4;
        sprite_submit(ui, s);
    }
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    // club_wired_sprites with its first row's sprite the cursor's header (a mutated row)
    const SpriteSlot* rows = ui_slots(ui.data, "club_wired", "club_wired_sprites");
    if (rows) {
        SpriteSlot header = rows[0];
        header.sprite = static_cast<int>(headers(ui)[static_cast<std::size_t>(c.cursor)]);
        sprite_row_draw(ui, header);
        sprite_rows_draw(ui, rows + 1);
    }
}

} // namespace

void club_wired_spawn(FrontEnd& fe, Ui&) {
    ClubWiredScreen& c = fe.screen.campaign.club;
    c = ClubWiredScreen{};
    c.from = fe.previous;
}

void club_wired_step(FrontEnd& fe, Ui& ui) {
    ClubWiredScreen& c = fe.screen.campaign.club;
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
    switch (fe.screen.step) {
    case 0: { // club_wired_screen_update: no cabinet wait; the start item by where we came from
        texture_set_load(ui, common_asset);
        texture_set_load(ui, club_asset);
        level_meter_spawn(ui, fe.screen.meter, "level_meter_b");
        switch (c.from) {
        case app_pepper_talk:
            c.cursor = c.shown = 1;
            break;
        case app_name_entry:
            c.cursor = c.shown = 4;
            break;
        case app_record:
            c.cursor = c.shown = 2;
            break;
        case app_schedule:
            c.cursor = c.shown = 3;
            break;
        default: // 3, 0x18, the map, a talk
            c.cursor = c.shown = 0;
            break;
        }
        for (int i = 0; i < 6; ++i) {
            line_init(ui, c, i);
        }
        music_shuffle_catalogue(fe.music);
        music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
        c.fade_count = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
        c.banner_timer = -1;
        fade_in_start(ui, static_cast<float>(c.fade_count));
        if (c.from == app_town_map) {
            ui_sound(ui, bank_sys, se_shop_arrive);
        }
        fe.screen.step = 1;
        break;
    }
    case 1: // step2: the fade
        club_draw(fe, ui, c);
        music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
        if (--c.fade_count < 0) {
            c.banner_timer = static_cast<int>(ui.task_timer);
            now_playing_spawn(ui, fe.screen.strip, 0, 0x153, 0x17);
            fe.screen.step = 2;
        }
        break;
    case 2: { // step3: the wheel (silent cursor, read while still), accept mid-turn, cancel to 5
        if (!c.turning) {
            menu_cursor(ui, c.cursor, 0, 6, cursor_menu_horizontal, -1, -1);
        }
        if (c.cursor != c.shown) {
            if (!c.turning) {
                ui_sound(ui, bank_sys, se_carousel_turn);
                c.turning = true;
                c.turn_frame = 0;
                line_init(ui, c, c.cursor);
            } else if (++c.turn_frame > 8) {
                c.turn_frame = 0;
                c.turning = false;
                c.shown = c.cursor;
            }
        } else if (c.turning && ++c.turn_frame > 8) {
            c.turn_frame = 0;
            c.turning = false;
            c.shown = c.cursor;
        }
        club_draw(fe, ui, c);
        music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
        if (menu_accept(ui)) {
            ui_sound(ui, bank_sys, c.cursor == 5 ? se_shop_leave : se_menu_accept);
            c.fade_count = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
            fade_out_start(ui, static_cast<float>(c.fade_count));
            fe.screen.step = 3;
        } else if (menu_cancel(ui)) {
            if (c.cursor != 5) {
                ui_sound(ui, bank_sys, se_carousel_turn);
            }
            c.turn_frame = 0;
            c.shown = c.cursor;
            c.cursor = 5;
        }
        break;
    }
    case 3: // step4
        club_draw(fe, ui, c);
        if (--c.fade_count < 0) {
            fe.screen.strip.active = false;
            task_header_set(fe, c.cursor);
            fe.screen.step = 4;
        }
        break;
    default:
        club_draw(fe, ui, c);
        break;
    }
}

void club_wired_destroy(FrontEnd&, Ui& ui) {
    ui_music_stop(ui);
    ui.clear_colour = 0xff000000;
}
