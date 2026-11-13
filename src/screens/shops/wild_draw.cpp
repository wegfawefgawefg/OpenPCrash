// Wild Machines' drawing (decomp src/o1/u_01c789.c wild_machines_menu_draw,
// _part_buy_tabs_draw, _part_buy_card_draw; src/hand/hud_draw_o1.c _used_screen_draw,
// _part_buy_frame_draw, _part_sell_frame_draw; src/o1/u_0201ab.c the lists).
#include "screens/flow/front_end.hpp"
#include "screens/garage/chip_carousel.hpp"
#include "screens/shops/wild_machines.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cmath>

namespace {

int value_at(Ui& ui, const char* table, int i, int count) {
    const std::vector<double> v = ui_values(ui.data, "wild_machines", table, count);
    return i >= 0 && i < count ? static_cast<int>(v[static_cast<std::size_t>(i)]) : -1;
}

// the hub's wheel ids and lit ids (D_001DCE18 row: the help line, the label's place, then
// D_001DCE24 and D_001DCE3C)
int wheel_id(Ui& ui, int i, bool lit) {
    const UiTable* t = ui_table(ui.data, "wild_machines", "wild_machines_menu_rows");
    const std::size_t at = static_cast<std::size_t>((lit ? 9 : 3) + i);
    return t && !t->rows.empty() && at < t->rows[0].size() ? static_cast<int>(t->rows[0][at]) : -1;
}

// the three frames turning 0.01 rad a frame behind the parts and used screens (DRAW_FRAMES)
void turning_frames_draw(Ui& ui, WildScreen& w) {
    w.turn += 0.01f;
    if (w.turn > 6.2831855f) {
        w.turn = 0.0f;
    }
    const struct {
        int id;
        float x, y;
        int blend;
    } frames[3] = {{0xba2, 102, 179, 2}, {0xba3, 347, 182, 2}, {0xba4, 374, 155, 3}};
    for (const auto& f : frames) {
        Sprite s = sprite_of(ui, f.id);
        s.pos[0] = f.x;
        s.pos[1] = f.y;
        s.blend = f.blend;
        s.rot[2] = w.turn;
        sprite_submit(ui, s);
    }
}

} // namespace

void wild_menu_draw(FrontEnd& fe, Ui& ui, WildScreen& w) {
    text_strip_draw_scroll(ui, w.labels[w.menu], 0xff3c3c3c);
    garage_banner_draw(ui, *fe.world, w.banner, 399, 0x102);
    int ids[6];
    for (int i = 0; i < 6; ++i) {
        ids[i] = wheel_id(ui, i, false);
    }
    menu_wheel_draw(ui, ids, 6, w.menu_before, w.menu, w.menu_frames);
    if (!w.turning) {
        sprite_draw_at(ui, wheel_id(ui, w.menu, true), 262, 139, 4);
    }
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    if (const UiTable* rows = ui_table(ui.data, "wild_machines", "wild_machines_menu_sprites")) {
        std::vector<SpriteSlot> slots = rows->slots;
        if (!slots.empty()) {
            slots[0].sprite = value_at(ui, "wild_machines_menu_icons", w.menu, 6);
        }
        sprite_rows_draw(ui, slots.data());
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

int wild_menu_item(Ui& ui, const WildScreen& w) {
    return wheel_id(ui, w.menu, false);
}

void wild_parts_frame_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool selling, bool tabs) {
    garage_banner_draw(ui, *fe.world, w.banner, 378, 2);
    sprite_rows_draw(ui, ui_slots(ui.data, "wild_machines",
                                  selling ? "wild_machines_part_sell_frame_sprites"
                                          : "wild_machines_part_buy_frame_sprites"));
    turning_frames_draw(ui, w);
    // the header slides in after its delay
    if (w.header_delay > 0) {
        --w.header_delay;
    } else if (w.header_x < 0) {
        w.header_x = std::min(0, w.header_x + 20);
    }
    sprite_draw_at(ui, 0xb44, static_cast<float>(w.header_x), 116, 4);
    // the tab rows and the lit tab
    const int targets[4] = {125, 170, 215, 260}; // D_0017E298
    int top_ids[4], bottom_ids[4];
    for (int i = 0; i < 4; ++i) {
        top_ids[i] = value_at(ui, "wild_machines_frame_top", i, 4);
        bottom_ids[i] = value_at(ui, "wild_machines_frame_bottom", i, 4);
    }
    slide_draw(ui, w.top, 4, 24, 4, 264, targets, top_ids);
    slide_draw(ui, w.bottom, 4, 24, 4, 309, targets, bottom_ids);
    const int lit_x = w.row == 0 ? w.top.pos[w.col] : w.bottom.pos[w.col];
    sprite_draw_at(ui, value_at(ui, "wild_machines_frame_lit", w.col + 4 * w.row, 8),
                   static_cast<float>(lit_x), w.row == 0 ? 264.0f : 309.0f, 5, 0xffffd300,
                   tabs ? 0x2000 : 0);
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// the tab grid's monitor (wild_machines_part_buy_tabs_draw, _part_sell_tabs_draw): only while
// the tabs are up; the list and its boxes draw over the bare frame
void wild_parts_monitor_draw(Ui& ui, WildScreen& w, bool selling, bool advance) {
    if (selling) {
        sprite_draw_at(ui, 0xb4b, 405, 176, 2);
        sprite_draw_at(ui, 0xb4c, 426, 304, 4, 0xffffffff, 0x4000);
    } else {
        sprite_draw_at(ui, 0xb43, 407, 173, 2);
    }
    // the strip scrolls in the tab step only
    if (advance) {
        w.monitor += 0.25f;
    }
    const int picture = static_cast<int>(w.monitor / 112.0f) % 3;
    sprite_draw_at(ui, value_at(ui, "wild_machines_tab_pictures", picture, 3), 438, 206, 3);
    const float scroll = std::fmod(w.monitor, 112.0f);
    for (int k = 0; k < 2; ++k) {
        Sprite strip = sprite_of(ui, 0xb42);
        strip.pos[0] = 424;
        strip.pos[1] = 298.0f - scroll + 112.0f * k;
        strip.blend = 4;
        sprite_submit_clipped(ui, strip, 424, 202, 560, 298);
    }
}

namespace {

// the buy and sell lists' places (wild_machines_part_buy_card_draw, _part_sell_draw)
struct WildListLook {
    int head, head_x;   // the list frame popping in
    int bar, bar_x;     // the cursor bar
    float bar_y;        // its first row
    int plate_x;        // the A-plate
    int arrow_x;        // the scroll arrows
    int name_x, mark_x; // a row's name and class mark
};
constexpr WildListLook buy_look = {0xb2c, 70, 0xb2d, 73, 221, 263, 194, 116, 80};
constexpr WildListLook sell_look = {0xb59, 40, 0xb5a, 43, 220, 281, 184, 81, 50};

} // namespace

// decomp: wild_machines_part_buy_card_draw 0x1F823, wild_machines_part_sell_draw 0x21C52
void wild_parts_list_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool selling, bool listing,
                          bool bar) {
    const PartTables& t = fe.shop.tables;
    const World& world = *fe.world;
    const WildListLook& look = selling ? sell_look : buy_look;
    // the shade over the tab grid, and the list frame popping in
    sprite_draw_at(ui, 0xb26, 125, 264, 6, 0x64ffffff);
    const bool ready =
        !pop_timer_draw(ui, w.timers[0], look.head_x + sprite_width(ui, look.head) / 2,
                        194 + sprite_height(ui, look.head) / 2, 0x101);
    const std::vector<GarageEntry>& rows =
        selling ? w.editor.pages[w.kind].entries : w.stock.entries;
    const int count = static_cast<int>(rows.size());
    const std::uint32_t pulse = listing ? 0x2000 : 0;
    if (ready) {
        sprite_draw_at(ui, value_at(ui, "wild_machines_card_left", w.tab, 8), 117, 198, 0x102);
        if (bar) {
            sprite_draw_at(ui, look.bar, static_cast<float>(look.bar_x),
                           look.bar_y + 38.0f * w.cursor, 0x102, 0xffffffff, pulse);
        }
        if (listing) {
            icon_on_plate_draw(ui, look.plate_x, 381, 0x104, 0xc3d, 0x13);
        }
    }
    // the stat plates, the back at blend 5 and the framed front over it: the short pair higher up
    // for options, the tall pair for every other kind (retail sets the timers' sprites per kind)
    const bool option = w.kind == 5;
    const int plate_y = option ? 172 : 99;
    PopTimer front = w.timers[1], back = w.timers[2];
    front.sprite = option ? 0xb40 : 0xb2e;
    back.sprite = option ? 0xb41 : 0xb2f;
    pop_timer_draw(ui, back, 377 + sprite_width(ui, back.sprite) / 2,
                   plate_y + sprite_height(ui, back.sprite) / 2, 5);
    if (!pop_timer_draw(ui, front, 377 + sprite_width(ui, front.sprite) / 2,
                        plate_y + sprite_height(ui, front.sprite) / 2, 0x101)) {
        // the price plate and its blinking copy, and the paint's box
        const float py = option ? 179.0f : 197.0f;
        const bool low = selling && option;
        sprite_draw_at(ui, 0xb38, 390, py, low ? 6 : 0x101);
        sprite_draw_at(ui, 0xb38, 390, py, low ? 7 : 0x102, 0xffffd300, 0x4000);
        if (!option && (w.kind == 6 || w.kind == 7)) {
            sprite_draw_at(ui, 0xb30, 445, 112, 6);
        }
    }
    if (!ready || count == 0) {
        return;
    }
    if (count > 4) {
        const float ax = static_cast<float>(look.arrow_x);
        if (w.scroll > 0) {
            sprite_draw_at(ui, 0xb11, ax, 212, 0x102, 0xffffffff, pulse);
        }
        if (w.scroll + 4 < count) {
            sprite_draw_at(ui, 0xb10, ax, 371, 0x102, 0xffffffff, pulse);
        }
    }
    for (int i = 0; i < 4 && i < count; ++i) {
        const GarageEntry& e = rows[static_cast<std::size_t>((w.scroll + i) % count)];
        const int y = 232 + 38 * i;
        text_draw(ui, e.name ? e.name->glyphs : GlyphText{-1}, look.name_x, y, 0, 18, 18,
                  0xffc0c0c0, 45);
        if (e.klass >= 0 && e.klass < 8) {
            sprite_draw_at(ui, value_at(ui, "wild_machines_class_marks", e.klass, 8),
                           static_cast<float>(look.mark_x), 228.0f + 38.0f * i, 0x103);
        }
        if (!selling) {
            continue;
        }
        // in use: a paint any garage wears, a part's garage number
        if (w.kind == 6 || w.kind == 7) {
            if (wild_paint_in_use(world, w.kind, e.id)) {
                sprite_draw_at(ui, 0xb4d, 321, static_cast<float>(y), 0x103, 0xffffd300);
            }
        } else if (e.garage >= 0 && e.garage < 10) {
            sprite_draw_at(ui, value_at(ui, "wild_machines_sell_garage_marks", e.garage, 11), 321,
                           static_cast<float>(y), 0x103, 0xffffd300);
        }
    }
    // the cursor row's labels, card and price
    const GarageEntry& e = rows[static_cast<std::size_t>((w.scroll + w.cursor) % count)];
    InventoryItem item;
    item.kind = static_cast<std::int16_t>(w.kind);
    item.part = static_cast<std::int16_t>(e.id);
    item.tune = static_cast<std::int8_t>(e.tune);
    if (option) {
        sprite_draw_at(ui, item_option_laser_reduction(t, item) > 0.0f ? 0xb35 : 0xb34, 378, 209,
                       6);
    } else {
        sprite_draw_at(ui, value_at(ui, "wild_machines_card_right", w.tab, 8), 378, 244, 6);
    }
    // a paint's swatch fills 0xB30's box at (452, 119) (Plus Tech's card puts it at 127)
    part_card_draw(ui, t, item, 560, option ? 211 : 246, 21, 452, 119, 0x101, false);
    // wild_machines_tab_hooks: the fitted part growing in on the stand (tabs 0..3)
    if (w.tab < 4) {
        stand_draw(ui, fe.shop.stand);
    }
    const int price = wild_price(t, fe.world->story.cheats, w.kind, e.id, e.tune, selling);
    money_amount_draw(ui, price, 559, option ? 180 : 198, 0x101, 9, 0xfff0f0f0);
}

// decomp: wild_machines_used_screen_draw 0x23074
void wild_used_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool listing) {
    const PartTables& t = fe.shop.tables;
    sprite_rows_draw(ui, ui_slots(ui.data, "wild_machines", "wild_machines_used_sprites"));
    turning_frames_draw(ui, w);
    // the line-up's machines (their tasks draw themselves in retail)
    stand_draw(ui, fe.shop.stand);
    if (w.header_delay > 0) {
        --w.header_delay;
    } else if (w.header_x < 0) {
        w.header_x = std::min(0, w.header_x + 20);
    }
    sprite_draw_at(ui, 0xbd8, static_cast<float>(w.header_x), 116, 0x101);
    const int fade = static_cast<int>(static_cast<float>(20 - w.header_delay) * 12.75f);
    sprite_draw_at(ui, 0xbcd, 0, 337, 4, static_cast<std::uint32_t>(fade) << 24 | 0xffffff);
    garage_banner_draw(ui, *fe.world, w.banner, 284, 0x101);
    const std::vector<double> at =
        ui_values(ui.data, "wild_machines", "wild_machines_used_places", 14);
    const int count = static_cast<int>(w.used.size());
    for (int i = 0; i <= count; ++i) {
        const int slot = i == count ? 6 : 6 - count + i;
        const float tx = static_cast<float>(at[static_cast<std::size_t>(slot * 2)]);
        const float ty = static_cast<float>(at[static_cast<std::size_t>(slot * 2 + 1)]);
        // the rows come in from the left 20 px a frame
        if (w.used_x[slot] < static_cast<int>(tx)) {
            w.used_x[slot] = std::min(static_cast<int>(tx), w.used_x[slot] + 20);
        }
        int normal = 2977, lit = 2976; // the leave row (D_001DD090 pair 6)
        if (i < count) {
            const int leg = w.used[static_cast<std::size_t>(i)].build.leg;
            normal = value_at(ui, "wild_machines_part_sprites", leg * 2, 92);
            lit = normal - 1; // wild_machines_list_fill: the lit plate is the id before
        }
        sprite_draw_at(ui, normal, static_cast<float>(w.used_x[slot]), ty, 0x101);
        if (i == w.used_cursor) {
            sprite_draw_at(ui, lit, static_cast<float>(w.used_x[slot]), ty, 0x102, 0xffffffff,
                           listing ? 0x2000 : 0);
        }
    }
    if (w.used_cursor < count) {
        const UsedMachine& m = w.used[static_cast<std::size_t>(w.used_cursor)];
        float pop[2];
        box_pop_scale(pop, 5, 10, std::min(w.frames, 10));
        Sprite box = sprite_of(ui, 0xbce);
        box.pos[0] = 200;
        box.pos[1] = 244;
        box.scale[0] = pop[0];
        box.scale[1] = pop[1];
        box.flags |= 8;
        box.blend = 0x101;
        sprite_submit(ui, box);
        if (w.frames >= 10) {
            if (const BodyPart* body = body_part(t, m.build.body)) {
                sprite_draw_at(
                    ui, value_at(ui, "wild_machines_used_class_marks", body->body_class & 7, 8), 52,
                    212, 0x102);
            }
            text_draw(ui, m.build.name.glyphs, 129, 222, 1, 18, 18, 0xfff0f0f0, 9);
            money_amount_draw(ui, m.price, 307, 249, 0x102, 9, 0xfff0f0f0);
            sprite_draw_at(ui, 0xbd6, 156, 249, 0x102);
        }
    }
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// decomp: wild_machines_used_submenu_draw 0x202D3
void wild_submenu_draw(Ui& ui, WildScreen& w) {
    // the plate carries SV SPEC, BUY and CANCEL; only the cursor's word is lit over it
    sprite_draw_at(ui, 0xb92, 187, 228, 0x103);
    if (const SpriteSlot* rows =
            ui_slots(ui.data, "wild_machines", "wild_machines_used_submenu_sprites")) {
        sprite_row_draw(ui, rows[w.submenu]);
    }
}

// the wheel's clip (D_00199F08, D_0017BD58, D_00199F0C, D_0017AB60) and its arrows
constexpr float wheel_left = 220, wheel_top = 234, wheel_right = 418, wheel_bottom = 274;
constexpr float arrow_left_x = 201, arrow_right_x = 422, arrow_y = 243;

// decomp: wild_machines_garage_list_draw 0x2033D
void wild_garage_wheel_draw(FrontEnd& fe, Ui& ui, WildScreen& w) {
    // the frame ("Please select HANGAR.") and its two arrows
    sprite_draw_at(ui, 0xb9e, 190, 202, 0x103);
    sprite_draw_at(ui, 0xb9f, arrow_left_x, arrow_y, 0x104);
    sprite_draw_at(ui, 0xb9f, arrow_right_x, arrow_y, 0x104, 0xffffffff, sprite_mirror_u);
    // the slide from garage_before to garage, the wraps keeping its direction
    const int to = w.garage_before == 0 && w.garage == 9   ? -1
                   : w.garage_before == 9 && w.garage == 0 ? 10
                                                           : w.garage;
    const float slide =
        static_cast<float>(w.garage_before - to) * static_cast<float>(w.garage_wait) * 7.5f;
    const bool sliding = w.garage_wait != 8;
    if (sliding) {
        // the arrow on the way it turns, lit
        const bool right = w.garage - w.garage_before > 0;
        sprite_draw_at(ui, 0xb9f, right ? arrow_right_x : arrow_left_x, arrow_y, 0x105, 0xffff9400,
                       right ? sprite_mirror_u : 0);
    }
    for (int k = -2; k <= 2; ++k) {
        const int g = ((w.garage_before + k) % 10 + 10) % 10;
        const int label = value_at(ui, "wild_machines_garage_marks", g, 10);
        // an empty garage white (lit orange), a taken one dark grey (lit dark orange)
        const bool empty =
            (garage_status(fe.shop.tables, fe.world->garages, g) & garage_empty) != 0;
        if (!sliding && g == w.garage) {
            sprite_draw_at(ui, label, 283, wheel_top, 0x105, empty ? 0xffff9400 : 0xff331e00,
                           sprite_blink_soft);
        }
        Sprite s = sprite_of(ui, label);
        s.pos[0] = 283.0f + 60.0f * static_cast<float>(k) + slide;
        s.pos[1] = wheel_top;
        s.color = empty ? 0xffffffff : 0xff333333;
        s.blend = 0x104;
        sprite_submit_clipped(ui, s, wheel_left, wheel_top, wheel_right, wheel_bottom);
    }
    icon_on_plate_draw(ui, 335, 279, 0x104, 0xc3d, 0x17);
}

void wild_chip_draw(FrontEnd& fe, Ui& ui, WildScreen& w, bool input, bool selling) {
    const PartTables& t = fe.shop.tables;
    garage_banner_draw(ui, *fe.world, w.banner, 378, 2);
    sprite_rows_draw(ui,
                     ui_slots(ui.data, "wild_machines", "wild_machines_sprites", selling ? 2 : 0));
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    static const CarouselLook buy = {145,   true,  0xb79, 0xb75, 0xb68, 0xb66, 0xb6b, 0xb6e, 0xb65,
                                     0xb6a, 0xb60, 121,   284,   0xb63, 0xb64, 434,   323};
    CarouselLook look = buy;
    look.ring = !selling;
    std::vector<CarouselCard> cards;
    if (selling) {
        for (const GarageEntry& e : w.editor.pages[4].entries) {
            const InventoryItem& item = fe.world->garages.inventory[e.item];
            CarouselCard card;
            card.part = e.id;
            card.tune = e.tune;
            card.item = e.item;
            card.name = item.name[0] != -1 ? glyph_name_text(item.name, glyph_name_length)
                                           : (e.name ? e.name->glyphs : GlyphText{-1});
            card.price = wild_price(t, fe.world->story.cheats, 4, e.id, e.tune, true);
            cards.push_back(card);
        }
    } else {
        for (const StockItem& s : w.chip_stock) {
            CarouselCard card;
            card.part = s.id;
            card.tune = s.tune;
            card.name = chip_part(t, s.id) ? chip_part(t, s.id)->name.glyphs : GlyphText{-1};
            card.price =
                cheat_shop_price(fe.world->story.cheats, shop_item_price(t, 4, s.id, s.tune));
            cards.push_back(card);
        }
    }
    carousel_draw(ui, t, look, w.chips, cards, input, true);
    text_strip_draw_scroll(ui, w.help, 0xffc0c0c0);
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

void wild_card_confirm_draw(Ui& ui, const WildScreen& w, bool selling) {
    // the on-card answers (D_001DD2F8..D_001DD31C)
    const int yes = selling ? 2924 : 2907, yes_lit = selling ? 2925 : 2908;
    sprite_draw_at(ui, w.pick == 0 ? yes_lit : yes, 358, 323, 0x105);
    sprite_draw_at(ui, w.pick == 1 ? 2910 : 2909, 468, 323, 0x105);
}

void wild_tab_rects(Ui& ui, const WildScreen& w, PointerRect (&rects)[8]) {
    for (int col = 0; col < 4; ++col) {
        rects[col * 2] = sprite_rect(ui, value_at(ui, "wild_machines_frame_top", col, 4),
                                     static_cast<float>(w.top.pos[col]), 264);
        rects[col * 2 + 1] = sprite_rect(ui, value_at(ui, "wild_machines_frame_bottom", col, 4),
                                         static_cast<float>(w.bottom.pos[col]), 309);
    }
}

void wild_list_rects(Ui& ui, bool selling, PointerRect (&rows)[4], PointerRect& up,
                     PointerRect& down) {
    const WildListLook& look = selling ? sell_look : buy_look;
    for (int i = 0; i < 4; ++i) {
        rows[i] = sprite_rect(ui, look.bar, static_cast<float>(look.bar_x),
                              look.bar_y + 38.0f * static_cast<float>(i));
    }
    up = sprite_rect(ui, 0xb11, static_cast<float>(look.arrow_x), 212);
    down = sprite_rect(ui, 0xb10, static_cast<float>(look.arrow_x), 371);
}

int wild_used_rects(Ui& ui, const WildScreen& w, PointerRect (&rects)[7]) {
    const std::vector<double> at =
        ui_values(ui.data, "wild_machines", "wild_machines_used_places", 14);
    const int count = std::min(static_cast<int>(w.used.size()), 6);
    for (int i = 0; i <= count; ++i) {
        const int slot = i == count ? 6 : 6 - count + i;
        const int plate = i == count
                              ? 2977
                              : value_at(ui, "wild_machines_part_sprites",
                                         w.used[static_cast<std::size_t>(i)].build.leg * 2, 92);
        rects[i] =
            sprite_rect(ui, plate, static_cast<float>(at[static_cast<std::size_t>(slot * 2)]),
                        static_cast<float>(at[static_cast<std::size_t>(slot * 2 + 1)]));
    }
    return count + 1;
}

void wild_submenu_rects(Ui& ui, PointerRect (&rects)[3]) {
    const SpriteSlot* rows =
        ui_slots(ui.data, "wild_machines", "wild_machines_used_submenu_sprites");
    for (int i = 0; i < 3; ++i) {
        rects[i] = rows ? sprite_rect(ui, rows[i].sprite, rows[i].x, rows[i].y) : PointerRect{};
    }
}

void wild_garage_rects(Ui& ui, PointerRect (&rects)[5]) {
    // the arrows outside, the labels beside the cursor's inside (clipped to the wheel), the
    // cursor's label in the middle
    rects[0] = sprite_rect(ui, 0xb9f, arrow_left_x, arrow_y);
    rects[4] = sprite_rect(ui, 0xb9f, arrow_right_x, arrow_y);
    for (int k = -1; k <= 1; ++k) {
        PointerRect r = sprite_rect(ui, value_at(ui, "wild_machines_garage_marks", 0, 10),
                                    283.0f + 60.0f * static_cast<float>(k), wheel_top);
        r.left = std::max(r.left, wheel_left);
        r.right = std::min(r.right, wheel_right);
        rects[k + 2] = r;
    }
}
