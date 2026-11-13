// The hangar's screen (decomp src/o1/u_01c789.c hangar_screen_draw, hangar_wheel_draw,
// hangar_labels_draw, hangar_models_draw and hangar_model_place,
// hangar_garage_card_draw): the backdrop, a card per garage on the wheel with its number,
// class, name and state, the machines standing over the cards, the name plates sliding in along
// the bottom and the garage's menu card.
#include "screens/garage/hangar.hpp"

#include "screens/flow/front_end.hpp"
#include "screens/garage/garage_rules.hpp"
#include "world/world.hpp"

#include <algorithm>

namespace {

// D_001DB8E8 +0x50: the cards' row (x 149 at rest, y 121); a card every 269 px, eleven make 2959
constexpr float rest_x = 149.0f;
constexpr float card_y = 121.0f;
constexpr float card_step = 269.0f;
constexpr float wheel_span = 2959.0f;
constexpr float wheel_half = 1345.0f;
// D_001DB8A0: each garage's number; D_001DB8C8: the class marks by Loadout::common_class
constexpr int number_ids[10] = {1538, 1539, 1540, 1541, 1542, 1543, 1544, 1545, 1546, 1547};
constexpr int class_ids[8] = {1579, 1580, 1577, 1582, 1581, 1578, -1, -1};
// the name plates: D_001DB8E8 +0x28 unlit, garage_name_sprites lit (the exit's last); the slide's
// starts D_0017D5BC and targets D_0017D5E4 (.rdata, read from the XBE)
constexpr int plate_ids[10] = {1555, 1557, 1559, 1561, 1563, 1565, 1567, 1569, 1571, 1573};
constexpr int plate_lit[11] = {0x612, 0x614, 0x616, 0x618, 0x61a, 0x61c, 0x61e,
                               0x620, 0x622, 0x624, 0x60f};
constexpr int plate_targets[10] = {65, 97, 129, 161, 193, 225, 257, 289, 321, 353};
constexpr float plate_y = 346.0f;
constexpr std::uint32_t orange = 0xffff9400, white = 0xfff0f0f0, yellow = 0xffffcd00;

void put(Ui& ui, int id, float x, float y, int blend, std::uint32_t color = 0xffffffff,
         std::uint32_t flags = 0, float z = 1.0f) {
    sprite_draw_at(ui, id, x, y, blend, color, flags, z);
}

// the cards from the cursor's at `base`, easing from where the new one
// stood over the turn's 8 updates, wrapped into one turn of the wheel
// decomp: hangar_wheel_draw 0x1D05F
void wheel_place(Ui& ui, HangarScreen& h) {
    const int total = 8 * (60 / ui.frame_cadence);
    const float a = h.turn == total ? rest_x
                                    : (1.0f - static_cast<float>(h.turn) / static_cast<float>(total)) *
                                              h.turn_from +
                                          rest_x;
    for (int i = 0; i < 11; ++i) {
        h.angles[(h.sel + i) % 11] = static_cast<float>(i) * card_step + a;
    }
    const float hi = rest_x + wheel_half;
    for (float& p : h.angles) {
        if (!(p >= -hi && p <= hi)) {
            p = p >= hi ? p - wheel_span : p + wheel_span;
        }
    }
}

// one card
// decomp: hangar_labels_draw 0x1D451
void card_draw(FrontEnd& fe, Ui& ui, HangarScreen& h, int i) {
    const int bits = h.flags[i];
    const float x = h.angles[i];
    const std::uint32_t color = bits & garage_boarded ? orange : white;
    if (i == 10) {
        // the exit's sign: back to the area select, to keep SV, or to the garage
        put(ui,
            h.mode == app_area_select ? 0x5f4
            : h.mode == app_keep_sv   ? 0x5f5
                                      : 0x5fa,
            x, card_y, 0x102);
        return;
    }
    put(ui, 0x5fb, x, card_y, 0x102, color);
    put(ui, number_ids[i], x + 238.0f, card_y + 13.0f, 0x103, color);
    if (bits & garage_locked) {
        // locked: the price to open it (10,000, money_number_draw(0, ..) left-aligned)
        put(ui, 0x5f9, x + 47.0f, card_y + 58.0f, 0x103);
        number_draw_ex(ui, 4, 10000, static_cast<int>(x + 177.0f), static_cast<int>(card_y) + 100,
                       0x104, white, 9, 10, 3);
        return;
    }
    if (bits & garage_empty) {
        put(ui, 0x5f8, x + 152.0f, card_y + 136.0f, 0x103);
        return;
    }
    if (bits & garage_boarded) {
        put(ui, 0x60d, x + 176.0f, card_y + 136.0f, 0x103, color);
    }
    const int klass = h.values[i] >= 0 && h.values[i] < 8 ? class_ids[h.values[i]] : -1;
    if (klass >= 0) {
        put(ui, klass, x + 25.0f, card_y + 136.0f, 0x104);
    }
    const Garage& g = fe.world->garages.garages[i];
    text_draw(ui, glyph_name_text(g.name, glyph_name_length), static_cast<int>(x) + 0x50,
              static_cast<int>(card_y) + 0xb5, 3, 18, 18, color, 9);
    put(ui, 0x628, x + 19.0f, card_y + 187.0f, 0x104);
    if (bits & garage_part_missing) {
        put(ui, 0x631, x + 116.0f, card_y + 76.0f, 0x102); // a part missing
        return;
    }
    float y2 = card_y + 89.0f;
    if (bits & garage_no_weapon) {
        put(ui, 0x632, x + 116.0f, y2, 0x102); // no weapon
        y2 -= 48.0f;
    }
    if (bits & garage_overweight) {
        put(ui, 0x633, x + 116.0f, y2, 0x102); // over the legs' capacity
    }
}

// the cursor's garage and two either
// side, 7 units apart per card (x = 7 (card x - 149) / 269); the cursor's turns 0.25 degrees an
// update from 0.4, the others face (offset + 1) x 0.4. An incomplete build (a part missing, flag
// 0x10) is laid out as its parts with the hangar's places (scoobee_parts_draw_spread, view 6)
// decomp: hangar_models_draw 0x1DD19, hangar_model_place 0x1D83A
void models_draw(FrontEnd& fe, Ui& ui, HangarScreen& h) {
    ModelStand& stand = fe.shop.stand;
    StandPose poses[5];
    int count = 0;
    const int deltas[5] = {0, -1, 1, -2, 2};
    for (const int delta : deltas) {
        const int g = (h.sel + delta + 11) % 11;
        if (g >= 10 || (h.flags[g] & (garage_locked | garage_empty))) {
            continue;
        }
        if (!h.loaded[g]) {
            // asked again each update until its load is done (model_viewer_thread)
            const World& w = *fe.world;
            const ShopBuild build =
                loadout_build(fe.shop.tables, w.garages, w.garages.garages[g]);
            bool loading = false;
            h.mech[g] = stand_load(ui, stand, build, true, &h.complete[g], &loading);
            h.loaded[g] = !loading;
            stand_parts_present(build, h.present[g]);
            h.spin[g] = 0.0f;
        }
        if (!h.mech[g]) {
            continue;
        }
        const float v = (h.angles[g] - rest_x) * 0.0037174721f; // 1 / 269
        if (delta != 0) {
            h.heading[g] = (v + 1.0f) * 0.4f;
            h.spin[g] = 0.0f;
        } else {
            if (h.spin[g] == 0.0f) {
                h.heading[g] = 0.4f;
            }
            h.spin[g] = 0.25f;
        }
        StandPose& p = poses[count++];
        p.mech = h.mech[g];
        p.point[0] = 7.0f * v;
        p.point[1] = 0.5f;
        p.point[2] = 0.5f;
        p.heading = h.heading[g];
        if (!h.complete[g]) {
            p.spread = 6;
            std::copy(h.present[g], h.present[g] + 6, p.present);
        }
    }
    stand_draw_poses(ui, stand, poses, count);
}

} // namespace

// decomp: hangar_wheel_angle 0x1D12D
float hangar_wheel_angle(const HangarScreen& h) {
    float a = h.angles[h.sel] - rest_x;
    if (a > wheel_half) {
        a -= wheel_span;
    } else if (a < -wheel_half) {
        a += wheel_span;
    }
    return a;
}

// decomp: hangar_screen_draw 0x1DE3C
void hangar_screen_draw(FrontEnd& fe, Ui& ui, HangarScreen& h, bool highlight) {
    const World& w = *fe.world;
    if (h.turn < 8 * (60 / ui.frame_cadence)) {
        ++h.turn;
    }
    wheel_place(ui, h);
    put(ui, 0x5f6, 0, 0, 1);
    day_draw(ui, w, 0x102);
    money_draw(ui, w, 0x102);
    for (int i = 0; i < 11; ++i) {
        card_draw(fe, ui, h, i);
    }
    models_draw(fe, ui, h);
    if (h.boarded >= 0 && h.boarded < garage_count) {
        garage_name_banner_draw(ui, w.garages.garages[h.boarded], h.banner, 0x17a, 0x103);
    }
    slide_draw(ui, h.slide, 10, 0x20, 3, static_cast<int>(plate_y), plate_targets, plate_ids);
    const float base = static_cast<float>(h.slide.pos[9] - 0x161);
    put(ui, 0x611, base, 358.0f, 2);
    put(ui, 0x610, base + 346.0f, 384.0f, 3);
    // the plates' hit boxes: each 40 x 24 plate is a slanted bar about 30 wide in any row, set
    // 32 apart, so its rectangle overlapped its neighbours by 8; the middle 32 columns (the slant's
    // middle row) tile them. The tenth's plate 0x60F is drawn from 3..60 across
    for (int i = 0; i < 10; ++i) {
        h.plates[i] = pointer_rect_inset(
            sprite_rect(ui, plate_ids[i], static_cast<float>(h.slide.pos[i]), plate_y), 4, 0, 4, 1);
    }
    h.plates[10] = pointer_rect_inset(sprite_rect(ui, 0x60f, 346.0f, 384.0f), 3, 0, 4, 1);
    if (h.boarded >= 0 && h.boarded < garage_count && (h.flags[h.boarded] & garage_boarded)) {
        put(ui, plate_lit[h.boarded], static_cast<float>(h.slide.pos[h.boarded]), plate_y, 0x104,
            orange);
    }
    if (h.sel >= 0 && h.sel < 10) {
        put(ui, plate_lit[h.sel], static_cast<float>(h.slide.pos[h.sel]), plate_y, 0x105, yellow,
            highlight ? 0x2000 : 0);
    }
    if (h.sel == 10) {
        put(ui, 0x60f, 346.0f, 384.0f, 0x105, yellow, 0x2000);
    }
    Sprite left = sprite_of(ui, 0x626);
    left.pos[0] = 0.0f;
    left.pos[1] = 95.0f;
    left.blend = 0x101;
    left.scale[1] = 319.0f / static_cast<float>(sprite_height(ui, 0x626));
    sprite_submit(ui, left);
    Sprite right = sprite_of(ui, 0x627);
    right.pos[0] = 488.0f;
    right.pos[1] = 95.0f;
    right.blend = 0x101;
    right.scale[1] = 281.0f / static_cast<float>(sprite_height(ui, 0x627));
    sprite_submit(ui, right);
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// decomp: hangar_garage_card_draw 0x1D169
void hangar_card_draw(Ui& ui, HangarScreen& h) {
    const SpriteSlot* rows = ui_slots(ui.data, "hangar", "hangar_garage_card_sprites");
    if (h.mode == app_keep_sv) {
        // keep SV's: board it for VS, Specs, Close
        h.card.sprite = 0x62f;
        if (pop_timer_draw(ui, h.card, sprite_width(ui, 0x62f) / 2 + 0xff,
                           sprite_height(ui, 0x62f) / 2 + 0xb6, 0x104)) {
            return;
        }
        if (rows) {
            sprite_rows_draw(ui, rows + 6);
            const SpriteSlot* lit = ui_slots(ui.data, "hangar", "hangar_garage_card_sprites_2");
            if (lit && h.kind >= 0 && h.kind < 3) {
                sprite_row_draw(ui, lit[h.kind]);
            }
            for (int k = 0; k < 3; ++k) {
                h.rows[k] = sprite_rect(ui, rows[6 + k].sprite, rows[6 + k].x, rows[6 + k].y);
            }
        }
        return;
    }
    h.card.sprite = 0x5fc;
    if (pop_timer_draw(ui, h.card, sprite_width(ui, 0x5fc) / 2 + 0xff,
                       sprite_height(ui, 0x5fc) / 2 + 0x9d, 0x104)) {
        return;
    }
    if (!rows) {
        return;
    }
    sprite_rows_draw(ui, rows);
    for (int k = 0; k < 5; ++k) {
        h.rows[k] = sprite_rect(ui, rows[k].sprite, rows[k].x, rows[k].y);
    }
    const int bits = h.flags[h.sel < 11 ? h.sel : 10];
    // the items this garage can't take, crossed out in red: Test-drive unless it's boarded, Ride,
    // Test-drive and Specs without a whole machine
    const float crossed[3] = {169.0f, 190.0f, 211.0f};
    if (bits & garage_whole) {
        if (!(bits & garage_boarded)) {
            put(ui, 0x60c, 274.0f, 190.0f, 0x107, 0xffff4040, 0, 0.5f);
        }
    } else {
        for (const float y : crossed) {
            put(ui, 0x60c, 274.0f, y, 0x107, 0xffff4040, 0, 0.5f);
        }
    }
    if (h.kind >= 0 && h.kind < 5) {
        sprite_row_draw(ui, rows[10 + h.kind]);
    }
}
