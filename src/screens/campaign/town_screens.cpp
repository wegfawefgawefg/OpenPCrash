// The town map (state 0x29, decomp src/o1/u_03c67e.c town_map_screen_* 0x3E956..0x3F414) and the
// night between days (0x18, src/o1/u_02e2fc.c day_advance_* 0x2E32D..0x2E3FC; docs/port/screens.md
// "Town map", "The day flow").
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/extra_screens.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/shops/music_player.hpp"
#include "story/rumble.hpp"
#include "talk/draw2d.hpp"
#include "world/world.hpp"

#include <cmath>

namespace {

constexpr int common_asset = 0x690; // scr_common_tex
constexpr int map_asset = 0x68a;    // town_map_tex
constexpr int marker_sprite = 0xa6d;
constexpr float two_pi = 6.2831855f;
// the spin pays 10 (100 NY) a full turn of the right stick, up to 100 a day (map_spin_money)
constexpr int spin_pay = 10;
constexpr int spin_cap = 100;

// town_map_spot_sprites [8 spots][3 rows]{sprite, x, y}
struct SpotRow {
    int sprite = 0, x = 0, y = 0;
};
SpotRow spot_row(Ui& ui, int spot, int row) {
    const std::vector<double> v = ui_values(ui.data, "town_map", "town_map_spot_sprites", 72);
    const std::size_t at = static_cast<std::size_t>((spot * 3 + row) * 3);
    return {static_cast<int>(v[at]), static_cast<int>(v[at + 1]), static_cast<int>(v[at + 2])};
}
// town_map_popup_offsets: (ox, oy) by spot
void popup_offset(Ui& ui, int spot, float& ox, float& oy) {
    const std::vector<double> v = ui_values(ui.data, "town_map", "town_map_popup_offsets", 16);
    ox = static_cast<float>(v[static_cast<std::size_t>(spot * 2)]);
    oy = static_cast<float>(v[static_cast<std::size_t>(spot * 2 + 1)]);
}
// town_map_marker_spots (MapSpot {x, y, flipped} by spot): the ui family doesn't write
// this table yet, so its values are the decomp's (src/data/ui_b2.c, docs/port/screens.md "Town
// map")
struct MapSpot {
    float x = 0, y = 0;
    bool flipped = false;
};
MapSpot marker_spot(Ui&, int spot) {
    constexpr MapSpot spots[8] = {{300, 103, false}, {204, 146, false}, {90, 145, false},
                                  {75, 279, true},   {315, 251, true},  {478, 243, true},
                                  {503, 135, false}, {434, 64, false}};
    return spots[spot & 7];
}
std::string spot_label(Ui& ui, int spot) {
    const UiTable* labels = ui_table(ui.data, "town_map", "town_map_spot_labels");
    return labels && spot < static_cast<int>(labels->strings.size())
               ? labels->strings[static_cast<std::size_t>(spot)]
               : std::string();
}
void label_init(Ui& ui, TownMapScreen& t, int spot) {
    text_strip_init(ui, t.labels[spot], 0x118, 0x2d, spot_label(ui, spot), 0, 0, 0x280, 0x1e0);
}

int ticks_per_update(const Ui& ui) {
    return 60 / ui.frame_cadence;
}

// decomp: town_map_marker_draw 0x3EA5C
void marker_draw(Ui& ui, TownMapScreen& t) {
    const int eight = ticks_per_update(ui) << 3;
    const SpotRow row = spot_row(ui, t.spot, 0);
    float f = static_cast<float>(t.flight) / static_cast<float>(eight);
    const float mw = static_cast<float>(sprite_width(ui, marker_sprite));
    const float mh = static_cast<float>(sprite_height(ui, marker_sprite));
    const MapSpot to = marker_spot(ui, t.spot);
    const MapSpot from = marker_spot(ui, t.prev_spot);
    int x = static_cast<int>(to.x * f + from.x * (1.0f - f) + mw * 0.5f);
    int y = static_cast<int>(to.y * f + from.y * (1.0f - f) + mh * 0.5f);
    const int left = row.x - static_cast<int>(static_cast<float>(sprite_width(ui, row.sprite)) * -0.5f);
    const int top = row.y - static_cast<int>(static_cast<float>(sprite_height(ui, row.sprite)) * -0.5f);
    if (t.landed_at != -1) {
        // the bob once landed
        const float phase = static_cast<float>(t.frame - t.landed_at) * two_pi;
        x = static_cast<int>(std::sin(phase / static_cast<float>(ticks_per_update(ui) * 100)) * 4.0f +
                             static_cast<float>(x));
        y = static_cast<int>(std::sin(phase / static_cast<float>(ticks_per_update(ui) << 6)) * 4.0f +
                             static_cast<float>(y));
    }
    std::uint32_t flip;
    if (from.flipped) {
        flip = t.mirrored ? 0 : 0x400;
    } else {
        flip = t.mirrored ? 0x400 : 0;
    }
    // the pop: out of the label after the fade in, back in after the accept
    float s;
    if (t.shown_at == -1) {
        s = 0.0f;
    } else {
        const int since = t.frame - t.shown_at;
        if (since < eight) {
            s = static_cast<float>(since) / static_cast<float>(eight);
        } else if (t.left_at == -1) {
            s = 1.0f;
        } else {
            s = 1.0f - static_cast<float>(t.frame - t.left_at) / static_cast<float>(eight);
            s = s < 0.0f ? 0.0f : s > 1.0f ? 1.0f : s;
        }
    }
    Sprite sprite = sprite_of(ui, marker_sprite);
    sprite.pos[0] = static_cast<float>(static_cast<int>(static_cast<float>(left) * (1.0f - s) +
                                                        static_cast<float>(x) * s));
    sprite.pos[1] = static_cast<float>(static_cast<int>(static_cast<float>(top) * (1.0f - s) +
                                                        static_cast<float>(y) * s));
    sprite.scale[0] = s;
    sprite.scale[1] = s;
    sprite.flags |= flip | 8;
    sprite.rot[0] = 0.0f;
    sprite.rot[1] = mh * -0.25f;
    sprite.rot[2] = -t.stick_angle;
    sprite.blend = 0x102;
    sprite_submit(ui, sprite);
}

// decomp: town_map_draw 0x3ED52
void town_map_draw(FrontEnd& fe, Ui& ui, TownMapScreen& t) {
    const int eight = ticks_per_update(ui) << 3;
    for (int i = 0; i < 3; ++i) {
        const SpotRow row = spot_row(ui, t.spot, i);
        const float w = static_cast<float>(sprite_width(ui, row.sprite));
        const float h = static_cast<float>(sprite_height(ui, row.sprite));
        Sprite sprite = sprite_of(ui, row.sprite);
        if (i == 2) {
            // the place's picture grows out of its point over 8 updates after a move
            float g = 1.0f;
            if (t.changed_at == -1) {
                g = 0.0f;
            } else if (t.frame - t.changed_at < eight) {
                g = static_cast<float>(t.frame - t.changed_at) / static_cast<float>(eight);
            }
            float ox = 0;
            float oy = 0;
            popup_offset(ui, t.spot, ox, oy);
            sprite.scale[0] = g;
            sprite.scale[1] = g;
            sprite.pos[0] = ox * (1.0f - g) + w * g * 0.5f + static_cast<float>(row.x);
            sprite.pos[1] = oy * (1.0f - g) + h * g * 0.5f + static_cast<float>(row.y);
            sprite.flags |= 8;
            sprite.blend = 0x103;
        } else {
            sprite.flags |= sprite_blink_soft | 0x8;
            sprite.blend = 0x101;
            sprite.pos[0] = static_cast<float>(row.x) + w * 0.5f;
            sprite.pos[1] = static_cast<float>(row.y) + h * 0.5f;
        }
        sprite_submit(ui, sprite);
    }
    day_draw(ui, *fe.world, 0x109);
    money_draw(ui, *fe.world, 0x109);
    sprite_rows_draw(ui, ui_slots(ui.data, "town_map", "town_map_sprites"));
    marker_draw(ui, t);
    garage_banner_draw(ui, *fe.world, t.banner_timer, 0x55, 0x102);
    text_strip_draw_scroll(ui, t.labels[t.spot], 0xff000000);
}

// back on the spot the player came from
// decomp: town_map_cursor_from_state 0x3E956
int spot_from_state(const FrontEnd& fe, int state) {
    switch (state) {
    case app_sonic_amp:
    case 0x84:
    case 0x99:
        return 4;
    case app_wild_machines:
    case 0x8c:
        return 2;
    case app_plus_tech:
    case 0x8f:
        return 3;
    case app_club_wired:
    case 0x32:
    case 0x98:
        return 1;
    case app_dealer:
        return fe.menu.dealer_index == 0 ? 7 : fe.menu.dealer_index == 1 ? 6 : 5;
    default:
        return 0;
    }
}

// the spin of the right stick (town_map_screen_step3's second half)
void stick_spin(FrontEnd& fe, Ui& ui, TownMapScreen& t) {
    const PadStick& stick = ui.pad.stick[1];
    if (stick.x == 0 && stick.y == 0) {
        t.stick_held = false;
        t.stick_start = 0.0f;
        t.stick_angle = 0.0f;
        return;
    }
    const float at = std::atan2(static_cast<float>(stick.y), static_cast<float>(stick.x));
    if (!t.stick_held) {
        t.stick_held = true;
        t.stick_start = at;
        t.stick_angle = 0.0f;
        t.visit_spin = 0;
        t.spin_sum = 0.0f;
        return;
    }
    const float last = t.stick_angle;
    const float now = t.stick_start - at;
    t.stick_angle = now;
    float d = now - last;
    if (d > 3.1415927f) {
        d -= two_pi;
    } else if (d < -3.1415927f) {
        d += two_pi;
    }
    t.spin_sum += d;
    Story& story = fe.world->story;
    if ((t.spin_sum > two_pi || t.spin_sum < -two_pi) &&
        static_cast<unsigned>(story.map_spin_money) < static_cast<unsigned>(spin_cap)) {
        story.map_spin_money += spin_pay;
        t.spin_sum = 0.0f;
        t.visit_spin += spin_pay;
        const int before = story.money;
        money_add(story, spin_pay);
        if (before != story.money) {
            ui_sound(ui, bank_battle,
                     se_money_tick); // the battle bank's score sound (sound_start_flag4)
        }
    }
}

} // namespace

void town_map_spawn(FrontEnd& fe, Ui&) {
    TownMapScreen& t = fe.screen.campaign.town;
    t = TownMapScreen{};
    t.from = fe.previous;
}

void town_map_step(FrontEnd& fe, Ui& ui) {
    TownMapScreen& t = fe.screen.campaign.town;
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
    const int eight = ticks_per_update(ui) << 3;
    switch (fe.screen.step) {
    case 0: // town_map_screen_update
        texture_set_load(ui, common_asset);
        texture_set_load(ui, map_asset);
        level_meter_spawn(ui, fe.screen.meter);
        t.spot = t.prev_spot = spot_from_state(fe, t.from);
        t.flight = eight;
        for (int i = 0; i < 8; ++i) {
            label_init(ui, t, i);
        }
        state_music_play(fe, ui, 0x29);
        t.fade_count = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
        fade_in_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
        t.landed_at = 0;
        fe.screen.step = 1;
        break;
    case 1: // step2: the fade counted by the screen
        town_map_draw(fe, ui, t);
        t.frame += 1;
        if (--t.fade_count < 0) {
            now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
            t.changed_at = t.shown_at = t.landed_prev = t.frame;
            t.banner_timer = static_cast<int>(ui.task_timer);
            fe.screen.step = 2;
        }
        break;
    case 2: { // step3: the ring (only once the marker has landed), the spin, accept
        if (t.flight < eight) {
            t.flight += 1;
            if (t.flight == eight) {
                t.landed_at = t.landed_prev;
            }
        } else {
            int cursor = 7 - t.spot;
            t.prev_spot = t.spot;
            if (menu_cursor(ui, cursor, 0, 8, cursor_ring, 0, 4)) {
                ui_sound(ui, bank_sys, se_town_map_cursor);
                t.landed_at = -1;
                const int area = 7 - cursor;
                t.changed_at = t.frame;
                const int moved = area - t.prev_spot;
                t.spot = area;
                t.flight = 0;
                t.mirrored = moved >= 0 ? moved >= 2 : t.prev_spot - area < 2;
                label_init(ui, t, area);
            }
        }
        stick_spin(fe, ui, t);
        town_map_draw(fe, ui, t);
        t.frame += 1;
        if (menu_accept(ui)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            t.left_at = t.frame;
            t.fade_count = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
            fade_out_start(ui, static_cast<float>(t.fade_count));
            fe.screen.step = 3;
        }
        break;
    }
    case 3: // town_map_leave
        town_map_draw(fe, ui, t);
        t.frame += 1;
        if (--t.fade_count < 0) {
            fe.screen.strip.active = false;
            int next = t.spot;
            if (t.spot >= 5) {
                dealer_index_set(fe, t.spot); // 5 Ventuno 2, 6 Kojima 1, 7 American Stars 0
                next = 5;
            }
            task_header_set(fe, next);
            fe.screen.step = 4;
        }
        break;
    default:
        town_map_draw(fe, ui, t);
        break;
    }
}

void town_map_destroy(FrontEnd&, Ui& ui) {
    ui_music_stop(ui);
    ui.clear_colour = 0xff000000;
}

// the night: no sheet, nothing drawn (day_advance_screen_update, day_advance_end)
void night_step(FrontEnd& fe, Ui& ui) {
    NightScreen& n = fe.screen.campaign.night;
    switch (fe.screen.step) {
    case 0:
        ui_sound(ui, bank_sys, se_day_advance);
        // music_shop_generate_stock: tonight's Sonic Amp stock (src/screens/shops/music_player.cpp)
        music_shop_generate_stock(fe.music, fe.world->profile.music);
        // a cut in (screen_fade_in_start(0)) and four seconds
        ui.fade.state = 0;
        ui.fade.timer = 0;
        n.counter = ui.frame_cadence * 4;
        fe.screen.step = 1;
        break;
    case 1:
        if (menu_accept(ui)) {
            n.counter = 0;
        }
        if (--n.counter < 0) {
            // day + 1, the line-up refilled with drawn opponents, the spin money zeroed
            World& world = *fe.world;
            day_advance(world.story, fe.story_tables, 0);
            tournaments_refill(world, fe.story_tables, fe.campaign.roster, world.story.day);
            std::printf("night: day %d, line-up %d %d %d\n", world.story.day,
                        world.story.lineup[0], world.story.lineup[1], world.story.lineup[2]);
            // a cut out (screen_fade_out_start(0)): black at once
            ui.fade.state = 2;
            ui.fade.timer = 0;
            fe.screen.step = 2;
        }
        break;
    case 2: // day_advance_end_next: one more update
        task_header_set(fe, 0);
        fe.screen.step = 3;
        break;
    default:
        break;
    }
}

void night_destroy(FrontEnd&, Ui& ui) {
    if (ui.audio) {
        stop_sound(*ui.audio, bank_sys, se_day_advance);
    }
}
