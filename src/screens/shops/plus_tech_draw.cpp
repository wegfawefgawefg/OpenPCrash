// Plus Tech's drawing (decomp src/o1/u_02edc5.c): the hub's wheel (plus_tech_menu_draw),
// the module view's frame (plus_tech_frame_draw) and drifting layers
// (plus_tech_overlay_draw), the part list (plus_tech_part_list_draw) and the
// tune card (plus_tech_tune_card_draw, src/hand/shop.c).
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

namespace {

// the tabs' slide (0x19B9E4 starts, 0x19B9D0 targets: retail .rdata)
constexpr int tab_start[5] = {-304, -235, -166, -97, -28};
constexpr int tab_x[5] = {116, 160, 204, 248, 292};

int value_at(Ui& ui, const char* table, int i, int count) {
    const std::vector<double> v = ui_values(ui.data, "plus_tech", table, count);
    return i >= 0 && i < count ? static_cast<int>(v[static_cast<std::size_t>(i)]) : -1;
}

void tune_sign(Ui& ui, int tune, float x, float y, int blend) {
    const int sign = tune < 0 ? 0xc0d : tune == 0 ? 0xc0e : 0xc0c;
    const std::uint32_t colour = tune < 0 ? 0xff00d3ff : tune == 0 ? 0xffffd300 : 0xffff0000;
    sprite_draw_at(ui, sign, x, y, blend, colour);
}

// plus_tech_overlay_draw: three layers drifting down behind the stand, each drawn twice
void overlay_draw(Ui& ui, PlusTechScreen& p) {
    const std::vector<double> at = ui_values(ui.data, "plus_tech", "plus_tech_overlay_at", 6);
    const std::vector<double> speed = ui_values(ui.data, "plus_tech", "plus_tech_overlay_speed", 3);
    for (int layer = 0; layer < 3; ++layer) {
        const int sprite = value_at(ui, "plus_tech_overlay_sprites", layer, 4);
        const int alpha = value_at(ui, "plus_tech_overlay_alpha", layer, 3);
        const float height = static_cast<float>(value_at(ui, "plus_tech_overlay_height", layer, 3));
        p.scroll[layer] += static_cast<float>(speed[static_cast<std::size_t>(layer)]);
        if (p.scroll[layer] > height) {
            p.scroll[layer] = 0.0f;
        }
        const float x = static_cast<float>(at[static_cast<std::size_t>(layer * 2)]);
        const float y = static_cast<float>(at[static_cast<std::size_t>(layer * 2 + 1)]);
        const std::uint32_t colour = static_cast<std::uint32_t>(alpha) << 24 | 0xffffff;
        sprite_draw_at(ui, sprite, x, y + p.scroll[layer], 2 + layer, colour);
        sprite_draw_at(ui, sprite, x, y + p.scroll[layer] - height, 2 + layer, colour);
    }
}

} // namespace

const GarageEntry* plus_tech_entry(const PlusTechScreen& p) {
    if (p.kind < 0 || p.kind > 5) {
        return nullptr;
    }
    const GaragePage& page = p.editor.pages[p.kind];
    const int row = p.scroll_row + p.cursor_row;
    return row >= 0 && row < static_cast<int>(page.entries.size())
               ? &page.entries[static_cast<std::size_t>(row)]
               : nullptr;
}

// decomp: plus_tech_menu_draw 0x2EDC5
void plus_tech_menu_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    text_strip_draw_scroll(ui, p.labels[p.menu], 0xff3c3c3c);
    garage_banner_draw(ui, *fe.world, p.banner, 399, 0x102);
    const int ids[3] = {value_at(ui, "plus_tech_menu_items", 0, 3),
                        value_at(ui, "plus_tech_menu_items", 1, 3),
                        value_at(ui, "plus_tech_menu_items", 2, 3)};
    menu_wheel_draw(ui, ids, 3, p.menu_before, p.menu, p.menu_frames);
    if (!p.turning) {
        sprite_draw_at(ui, value_at(ui, "plus_tech_menu_lit", p.menu, 3), 262, 139, 4);
    }
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    if (const UiTable* rows = ui_table(ui.data, "plus_tech", "plus_tech_menu_sprites")) {
        std::vector<SpriteSlot> slots = rows->slots;
        if (!slots.empty()) {
            slots[0].sprite = value_at(ui, "plus_tech_menu_icons", 1 + p.menu, 4);
        }
        sprite_rows_draw(ui, slots.data());
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// decomp: plus_tech_frame_draw 0x2F077
void plus_tech_frame_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p, bool pulse) {
    garage_banner_draw(ui, *fe.world, p.banner, 378, 4);
    sprite_rows_draw(ui, ui_slots(ui.data, "plus_tech", "plus_tech_frame_sprites"));
    sprite_draw_at(ui, 0x462, 44, 111, 3);
    int ids[5];
    for (int i = 0; i < 5; ++i) {
        ids[i] = value_at(ui, "plus_tech_frame_ids", i, 5);
    }
    if (p.tabs.started == -1) {
        slide_init(p.tabs, 5, tab_start);
        slide_start(ui, p.tabs);
    }
    slide_draw(ui, p.tabs, 5, 24, 4, 263, tab_x, ids);
    sprite_draw_at(ui, value_at(ui, "plus_tech_tab_sprites", p.tab, 5),
                   static_cast<float>(p.tabs.pos[p.tab]), 263, 5, 0xffffd300, pulse ? 0x2000 : 0);
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// decomp: plus_tech_part_list_draw 0x30357
void plus_tech_part_list_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p, bool listing) {
    const PartTables& t = fe.shop.tables;
    const World& w = *fe.world;
    overlay_draw(ui, p);
    if (p.kind < 0 || p.kind > 5) {
        return;
    }
    sprite_draw_at(ui, 0x485, 116, 263, 6, 0x64ffffff);
    const auto centred = [&](int sprite, int x, int y) {
        return std::pair<int, int>{x + sprite_width(ui, sprite) / 2, y + sprite_height(ui, sprite) / 2};
    };
    const auto [lx, ly] = centred(0x48a, 40, 194);
    const bool popping = pop_timer_draw(ui, p.timers[0], lx, ly, 0x101);
    if (!popping) {
        sprite_draw_at(ui, value_at(ui, "plus_tech_card_left", p.tab, 5), 117, 198, 0x102);
        sprite_draw_at(ui, 0x48b, 43, 220.0f + 38.0f * p.cursor_row, 0x102, 0xffffffff,
                       listing ? 0x2000 : 0);
        if (listing) {
            icon_on_plate_draw(ui, 281, 381, 0x104, 0xc3d, 0x13);
        }
    }
    const auto [sx, sy] = centred(0x48e, 379, 100);
    pop_timer_draw(ui, p.timers[2], sx, sy, 0x101);
    const auto [cx, cy] = centred(0x48d, 379, 100);
    const bool card_popping = pop_timer_draw(ui, p.timers[1], cx, cy, 0x101);
    if (!card_popping) {
        sprite_draw_at(ui, value_at(ui, "plus_tech_card_right", p.tab, 5), 378, 244, 6);
    }
    // the tab's hook (plus_tech_stand_draw): the fitted part growing in on the stand
    stand_draw(ui, fe.shop.stand);
    if (popping) {
        return;
    }
    const GaragePage& page = p.editor.pages[p.kind];
    const int count = static_cast<int>(page.entries.size());
    if (count > 4) {
        sprite_draw_at(ui, 0x464, 184, 212, 0x102);
        sprite_draw_at(ui, 0x463, 184, 371, 0x102);
    }
    for (int i = 0; i < 4 && i < count; ++i) {
        const GarageEntry& e =
            page.entries[static_cast<std::size_t>((p.scroll_row + i) % (count > 0 ? count : 1))];
        const int y = 232 + 38 * i;
        text_draw(ui, e.name ? e.name->glyphs : GlyphText{-1}, 81, y, 0, 18, 18, 0xffc0c0c0, 45);
        if (e.klass >= 0 && e.klass < 8) {
            sprite_draw_at(ui, value_at(ui, "plus_tech_class_marks", e.klass, 8), 50,
                           228.0f + 38.0f * i, 0x103);
        }
        if (e.garage >= 0 && e.garage < 10) {
            sprite_draw_at(ui, value_at(ui, "plus_tech_garage_marks", e.garage, 10), 321,
                           static_cast<float>(y), 0x103, 0xffffd300);
            if (p.kind == 2 || p.kind == 3) {
                sprite_draw_at(ui, e.slot == 0 ? 0x483 : 0x47d, 297, static_cast<float>(y), 0x103);
            }
        }
    }
    // the row's card at the tune being looked at, and the garage it is on with that tune
    const GarageEntry* e = plus_tech_entry(p);
    if (!e || card_popping) {
        return;
    }
    InventoryItem item = w.garages.inventory[e->item];
    item.tune = static_cast<std::int8_t>(p.tune);
    part_card_draw(ui, t, item, 560, 246, 21, 452, 127, 0x101, false);
    if (e->garage >= 0 && e->garage < garage_count) {
        // loadout_weight_tuned: the garage's weight with the new tune in this part's
        // slot, and its legs' capacity (retail reads inventory[-1] when the garage has no legs;
        // the port shows 0 there: docs/bugs.md)
        Garages g = w.garages;
        g.inventory[e->item].tune = static_cast<std::int8_t>(p.tune);
        const Garage& l = g.garages[e->garage];
        const float weight = loadout_weight(t, g, l) / 100.0f;
        const float capacity = loadout_leg_capacity(t, g, l) / 100.0f;
        sprite_draw_at(ui, 0x498, 384, 177, 0x101);
        sprite_draw_at(ui, 0x499, 568, 177, 0x101);
        number_draw_full(ui, 0x68, static_cast<long long>(weight), weight, 571, 180, 0x101,
                         0xfff0f0f0, 3, 3, 1, 0);
        number_draw_full(ui, 0x68, static_cast<long long>(capacity), capacity, 571, 199, 0x101,
                         0xfff0f0f0, 3, 3, 1, 0);
    }
}

// decomp: plus_tech_tune_card_draw 0x2F37C
void plus_tech_tune_card_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p, bool tuning) {
    sprite_draw_at(ui, 0x48c, 43, 220.0f + 38.0f * p.cursor_row, 0x104);
    const int x = 42 + sprite_width(ui, 0x490) / 2, y = 228 + sprite_height(ui, 0x490) / 2;
    if (pop_timer_draw(ui, p.timers[3], x, y, 0x105)) {
        return;
    }
    if (tuning) {
        icon_on_plate_draw(ui, 254, 355, 0x106, 0xc3d, 0x13);
    }
    const auto level = [&](int tune, float sign_x, int num_x) {
        tune_sign(ui, tune, sign_x, 256, 0x106);
        const int a = tune < 0 ? -tune : tune;
        number_draw_full(ui, 8, a, static_cast<float>(a), num_x, 256, 0x106, 0xffffffff, 2, 0, 1, 3);
    };
    level(p.tune_was, 102, 193);
    level(p.tune, 215, 307);
    sprite_draw_at(ui, 0x489, 203.0f + static_cast<float>(p.tune), 290, 0x106);
    const GarageEntry* e = plus_tech_entry(p);
    const int cost = e ? part_tune_price(fe.shop.tables, p.kind, e->id, p.tune_was, p.tune) : 0;
    money_amount_draw(ui, cost, 272, 335, 0x106, 9,
                      fe.world->story.money < cost ? 0xffff0000 : 0xfff0f0f0);
}
