// Wild Machines' modes, their input steps (decomp src/o1/u_0201ab.c): the parts tabs and list
// for buying and selling modules, colours and stickers, and the Chips carousel and its confirm
// card; the prices and the paints in use. The used Scoobees are wild_used.cpp; the screen around
// them, its boxes and mode changes, is wild_machines.cpp.
#include "screens/shops/wild_steps.hpp"

#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/garage/chip_carousel.hpp"
#include "world/world.hpp"

namespace wild {

namespace {

constexpr int tab_kinds[7] = {0, 1, 2, 3, 5, 6, 7};      // D_001DD328
constexpr int tab_of_cell[8] = {0, 1, 2, 5, 3, 6, 4, 7}; // D_0017E278 by col * 2 + row

} // namespace

// _part_buy_list_enter: today's stock of the tab (shop_parts_row), each at its stock tune
void stock_fill(FrontEnd& fe, WildScreen& w) {
    const PartTables& t = fe.shop.tables;
    const DayRow& day = day_row(fe.story_tables, fe.world->story.day);
    const int rows[7] = {day.body, day.leg, day.arm, day.back, day.option, day.color, day.sticker};
    w.stock = GaragePage{};
    const int r = rows[w.tab];
    if (r < 0 || r >= static_cast<int>(t.parts_stock.size())) {
        return;
    }
    for (const StockItem& s : t.parts_stock[static_cast<std::size_t>(r)]) {
        if (s.id < 0) {
            break;
        }
        GarageEntry e;
        e.id = s.id;
        e.tune = w.kind >= 6 ? 0 : s.tune;
        e.klass = part_class(t, w.kind, s.id);
        e.name = part_name(t, w.kind, s.id);
        e.value = price_value(t, w.kind, s.id, e.tune);
        w.stock.entries.push_back(e);
    }
}

void chip_name_from_record(FrontEnd& fe, int slot) {
    InventoryItem& item = fe.world->garages.inventory[slot];
    for (std::int16_t& g : item.name) {
        g = -1;
    }
    if (const ChipPart* chip = chip_part(fe.shop.tables, item.part)) {
        for (int i = 0; i < 8 && i < static_cast<int>(chip->name.glyphs.size()) &&
                        chip->name.glyphs[static_cast<std::size_t>(i)] != -1;
             ++i) {
            item.name[i] = chip->name.glyphs[static_cast<std::size_t>(i)];
        }
    }
}

// wild_machines_stand_fit_part: the row under the cursor onto the stand, by tab (0..3 are the
// body, legs, arms and backs; model_viewer_fit_part ignores the rest)
void stand_fit(FrontEnd& fe, Ui& ui, WildScreen& w) {
    const GaragePage& page = rows_of(w);
    const int count = static_cast<int>(page.entries.size());
    const int id =
        count > 0 ? page.entries[static_cast<std::size_t>((w.scroll + w.cursor) % count)].id : -1;
    stand_fit_part(ui, fe.shop.stand, w.tab, id);
}

void tabs_step(FrontEnd& fe, Ui& ui, WildScreen& w) {
    // list_cursor_page_move(&col, &row, 2, 8): rows sound 3, columns sound 4
    menu_cursor(ui, w.row, 0, 2, 0x16, 0, 3);
    menu_cursor(ui, w.col, 0, 4, 0x17, 0, 4);
    // PC: the pointer lights the tab it enters
    PointerRect cells[8];
    wild_tab_rects(ui, w, cells);
    int cell = w.col * 2 + w.row;
    if (menu_cursor(ui, cell, 0, 8, cursor_no_dpad, 0, 4, cells) != 0) {
        w.col = cell / 2;
        w.row = cell % 2;
    }
    const int tab = tab_of_cell[w.col * 2 + w.row];
    if (tab != w.tab) {
        w.tab = tab;
        if (tab < 7) {
            w.kind = tab_kinds[tab];
        }
        help_strip_init(
            ui, w.help,
            line(ui, selling(w) ? "wild_machines_sell_help" : "wild_machines_buy_help", tab));
    }
    view_draw(fe, ui, w, s_tabs);
    if (menu_accept(ui, &cells[w.col * 2 + w.row])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        if (w.tab == 7) {
            go_mode(ui, w, 0);
            fe.screen.step = s_switch;
            return;
        }
        w.cursor = w.scroll = 0;
        for (int i = 0; i < 3; ++i) {
            pop_timer_start(ui, w.timers[i], 1.0f, 1.0f, false, 0, 0);
        }
        if (selling(w)) {
            pages_build(fe, w);
            if (w.editor.pages[w.kind].entries.empty()) {
                box(w, 0x18, a_tabs);
                fe.screen.step = s_box;
                return;
            }
        } else {
            stock_fill(fe, w);
        }
        stand_fit(fe, ui, w);
        fe.screen.step = s_list;
    } else if (menu_cancel(ui) && w.tab != 7) {
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        w.col = 3;
        w.row = 1;
    }
}

void list_step(FrontEnd& fe, Ui& ui, WildScreen& w) {
    const PartTables& t = fe.shop.tables;
    World& world = *fe.world;
    GaragePage& page = rows_of(w);
    const int count = static_cast<int>(page.entries.size());
    int moved = 0;
    menu_list_move(ui.pad, w.cursor, w.scroll, 0, count, 4, 0x12, moved);
    PointerRect rows[4], up, down;
    wild_list_rects(ui, selling(w), rows, up, down);
    if (moved == 0) {
        moved = menu_list_pointer(ui, w.cursor, w.scroll, count, 4, 0x12, rows,
                                  count > 4 ? &up : nullptr, count > 4 ? &down : nullptr);
    }
    if (moved) {
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
        stand_fit(fe, ui, w);
    }
    view_draw(fe, ui, w, s_list);
    if (count == 0) {
        fe.screen.step = s_tabs;
        return;
    }
    const GarageEntry& e = page.entries[static_cast<std::size_t>((w.scroll + w.cursor) % count)];
    if (menu_accept(ui, &rows[w.cursor])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        w.price = wild_price(t, world.story.cheats, w.kind, e.id, e.tune, selling(w));
        fe.screen.step = s_box;
        if (!selling(w)) {
            const bool paint = w.kind == 6 || w.kind == 7;
            const bool owned = w.kind == 6   ? (world.garages.colors_owned[e.id] & 1) != 0
                               : w.kind == 7 ? (world.garages.stickers_owned[e.id] & 1) != 0
                                             : false;
            if (world.story.money < w.price) {
                box(w, 4, a_list);
            } else if (owned) {
                box(w, 5, a_list);
            } else if (paint) {
                box(w, 6, a_buy);
                fe.screen.step = s_yes_no;
            } else if (inventory_first_free(world.garages) < 0) {
                box(w, 3, a_list);
            } else {
                const std::vector<double> boxes =
                    ui_values(ui.data, "wild_machines", "wild_machines_box_places", 9);
                box(w, static_cast<int>(boxes[static_cast<std::size_t>(1 + (e.klass & 7))]), a_buy);
                fe.screen.step = s_yes_no;
            }
        } else if (e.garage != -1 ||
                   ((w.kind == 6 || w.kind == 7) && wild_paint_in_use(world, w.kind, e.id))) {
            box(w, 0x17, a_list);
        } else {
            box(w, 0x16, a_sell);
            fe.screen.step = s_yes_no;
        }
    } else if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        stand_hide(fe.shop.stand);
        fe.screen.step = s_tabs;
    } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_x)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        garage_page_sort(page, 0, page.column != 0 ? true : !page.descending);
        stand_fit(fe, ui, w);
    } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_y)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        garage_page_sort(page, 1, page.column != 1 ? true : !page.descending);
        stand_fit(fe, ui, w);
    }
}

void chips_step(FrontEnd& fe, Ui& ui, WildScreen& w) {
    const bool sell = selling(w);
    const int count = sell ? static_cast<int>(w.editor.pages[4].entries.size())
                           : static_cast<int>(w.chip_stock.size());
    const PointerRect leave = sprite_rect(ui, 0xb63, 434, 323);
    if (carousel_input(ui, w.chips, count, !sell, leave) != 0 && count > 0) {
        const int id = sell ? w.editor.pages[4].entries[static_cast<std::size_t>(w.chips.cursor)].id
                            : w.chip_stock[static_cast<std::size_t>(w.chips.cursor)].id;
        help_strip_init(ui, w.help,
                        w.chips.on_leave ? line(ui, "wild_machines_chip_leave_help", 0)
                                         : line(ui, "wild_machines_chip_help", id));
    }
    view_draw(fe, ui, w, s_chips);
    if (w.chips.flash[3] != 0 || w.chips.slide < 8) {
        return;
    }
    const PointerRect card = {246, 136, 396, 286};
    if (menu_accept(ui, w.chips.on_leave ? &leave : &card)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        if (w.chips.on_leave) {
            go_mode(ui, w, 0);
            fe.screen.step = s_switch;
            return;
        }
        w.pick = 1;
        fe.screen.step = s_chip_confirm;
    } else if (menu_cancel(ui) && !w.chips.on_leave) {
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        w.chips.on_leave = true;
    }
}

void chip_confirm_step(FrontEnd& fe, Ui& ui, WildScreen& w) {
    World& world = *fe.world;
    const PartTables& t = fe.shop.tables;
    PointerRect answers[2] = {sprite_rect(ui, 2907, 358, 323), sprite_rect(ui, 2909, 468, 323)};
    menu_cursor(ui, w.pick, 0, 2, 0x17, 0, 4, answers);
    view_draw(fe, ui, w, s_chip_confirm);
    if (menu_accept(ui, &answers[w.pick])) {
        if (w.pick != 0) {
            ui_sound(ui, bank_sys, se_menu_accept);
            fe.screen.step = s_chips;
            return;
        }
        fe.screen.step = s_box;
        if (selling(w)) {
            const GarageEntry& e =
                w.editor.pages[4].entries[static_cast<std::size_t>(w.chips.cursor)];
            if (e.garage != -1) {
                box(w, 0x1c, a_chips);
            } else {
                box(w, 0x1b, a_chip_sell);
                fe.screen.step = s_yes_no;
            }
            return;
        }
        const StockItem& s = w.chip_stock[static_cast<std::size_t>(w.chips.cursor)];
        const int price = cheat_shop_price(world.story.cheats, shop_item_price(t, 4, s.id, s.tune));
        const int slot = inventory_first_free(world.garages);
        if (slot < 0) {
            box(w, 0, a_chips);
        } else if (world.story.money < price) {
            box(w, 1, a_chips);
        } else {
            InventoryItem& item = world.garages.inventory[slot];
            item = InventoryItem{};
            item.tune = static_cast<std::int8_t>(s.tune);
            item.kind = item_chip;
            item.part = static_cast<std::int16_t>(s.id);
            chip_name_from_record(fe, slot);
            w.chip_slot = slot;
            ui_sound(ui, bank_sys, se_purchase);
            money_add(world.story, -price);
            box(w, 2, -1);
            w.after = 101; // then 0x1E and name entry
        }
    } else if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        fe.screen.step = s_chips;
    }
}

} // namespace wild

int wild_price(const PartTables& t, const CheatRules& rules, int kind, int id, int tune,
               bool selling_it) {
    const int price = shop_item_price(t, kind, id, tune);
    if (!selling_it || (kind == 5 && id == 0x2d)) {
        return cheat_shop_price(rules, price);
    }
    return cheat_shop_price(rules, static_cast<int>(static_cast<float>(price) * 0.5f));
}

bool wild_paint_in_use(const World& world, int kind, int id) {
    for (const Garage& g : world.garages.garages) {
        if ((kind == 6 && g.color == id) || (kind == 7 && g.sticker == id)) {
            return true;
        }
    }
    return false;
}
