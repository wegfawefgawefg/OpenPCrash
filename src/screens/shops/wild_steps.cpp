// Wild Machines' modes, their input steps (decomp src/o1/u_0201ab.c): the parts tabs and list
// for buying and selling modules, colours and stickers, the Chips carousel and its confirm card,
// and the used Scoobees with their submenu and the garage wheel. The screen around them, its boxes
// and mode changes, is wild_machines.cpp.
#include "screens/shops/wild_steps.hpp"

#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/garage/chip_carousel.hpp"
#include "world/world.hpp"

namespace wild {

namespace {

constexpr int tab_kinds[7] = {0, 1, 2, 3, 5, 6, 7}; // D_001DD328
constexpr int tab_of_cell[8] = {0, 1, 2, 5, 3, 6, 4, 7}; // D_0017E278 by col * 2 + row

// an empty garage, and room for every part
// decomp: wild_machines_garage_can_take 0x229A2
bool garage_can_take(FrontEnd& fe, const ShopBuild& b, int g) {
    const Garages& gs = fe.world->garages;
    if (!(garage_status(fe.shop.tables, gs, g) & 2)) {
        return false;
    }
    int needed = 0;
    const int parts[7] = {b.body, b.leg, b.weapon[0], b.weapon[1], b.weapon[2], b.weapon[3], b.chip};
    for (const int p : parts) {
        needed += p != -1;
    }
    for (const int o : b.option) {
        needed += o != -1;
    }
    int free = 0;
    for (const InventoryItem& item : gs.inventory) {
        free += item.part == -1;
    }
    return needed <= free;
}

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

// wild_machines_list_fill: the day's used machines, the high classes once the First
// Ranker is beaten
void used_fill(FrontEnd& fe, Ui& ui, WildScreen& w) {
    const PartTables& t = fe.shop.tables;
    const int r = day_row(fe.story_tables, fe.world->story.day).scoobee;
    w.used.clear();
    const bool ranker = tournament_won(fe.world->story, fe.story_tables.first_ranker_tournament);
    if (r >= 0 && r < static_cast<int>(t.scoobee_stock.size())) {
        for (const StockItem& s : t.scoobee_stock[static_cast<std::size_t>(r)]) {
            if (s.id < 0 || s.id >= static_cast<int>(t.builds.size()) || w.used.size() >= 6) {
                continue;
            }
            const ShopBuild& b = t.builds[static_cast<std::size_t>(s.id)];
            const BodyPart* body = body_part(t, b.body);
            if ((body && (body->body_class & 0xff) < 3) || ranker) {
                w.used.push_back({b, shop_scoobee_total(t, b)});
            }
        }
    }
    const std::vector<double> at =
        ui_values(ui.data, "wild_machines", "wild_machines_used_places", 14);
    for (int slot = 0; slot < 7; ++slot) {
        w.used_x[slot] = static_cast<int>(at[static_cast<std::size_t>(slot * 2)]) -
                         (420 - 60 * slot) - 800;
    }
    w.used_cursor = 0;
    w.frames = 0;
    help_strip_init(ui, w.help, line(ui, "wild_machines_used_help", 0));
    // the machines onto the stand's line-up (rows by slot, as retail's six)
    ShopBuild line[6];
    for (std::size_t i = 0; i < w.used.size(); ++i) {
        line[i] = w.used[i].build;
    }
    stand_line_set(fe.shop.stand, line, static_cast<int>(w.used.size()));
}

// _used_shown_machine_update, _used_model_slot_pick: the cursor's machine on the stand
void used_stand(FrontEnd& fe, Ui& ui, const WildScreen& w) {
    const int count = static_cast<int>(w.used.size());
    stand_line_follow(ui, fe.shop.stand, w.used_cursor < count ? w.used_cursor : -1);
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
    const PointerRect rect = {125, 250, 300, 340};
    if (menu_accept(ui, &rect)) {
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
    const PointerRect rows = {73, 221, 340, 375};
    if (menu_accept(ui, &rows)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        w.price = wild_price(t, w.kind, e.id, e.tune, selling(w));
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
    if (carousel_input(ui, w.chips, count, !sell) != 0 && count > 0) {
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
    const PointerRect leave = sprite_rect(ui, 0xb63, 434, 323);
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
            const GarageEntry& e = w.editor.pages[4].entries[static_cast<std::size_t>(w.chips.cursor)];
            if (e.garage != -1) {
                box(w, 0x1c, a_chips);
            } else {
                box(w, 0x1b, a_chip_sell);
                fe.screen.step = s_yes_no;
            }
            return;
        }
        const StockItem& s = w.chip_stock[static_cast<std::size_t>(w.chips.cursor)];
        const int price = shop_item_price(t, 4, s.id, s.tune);
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

void used_step(FrontEnd& fe, Ui& ui, WildScreen& w) {
    const int count = static_cast<int>(w.used.size());
    ++w.frames;
    // the header first (20 frames' delay, then its slide); retail plays 0x2C on every frame here
    // after a trip to the spec page or name entry, the port once (docs/bugs.md)
    const bool ready = w.header_delay == 0 && w.header_x >= 0;
    if (ready && !w.used_sound_done) {
        ui_sound(ui, bank_sys, se_panel_slide_in);
        w.used_sound_done = true;
    }
    if (ready && menu_cursor(ui, w.used_cursor, 0, count + 1, 0x17, 0, 4)) {
        w.frames = 0;
    }
    used_stand(fe, ui, w);
    view_draw(fe, ui, w, s_used);
    if (!ready) {
        return;
    }
    const PointerRect rows = {40, 300, 420, 400};
    if (menu_accept(ui, &rows)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        if (w.used_cursor == count) {
            stand_line_hide(fe.shop.stand); // every used machine's task hidden
            go_mode(ui, w, 0);
            fe.screen.step = s_switch;
        } else {
            w.submenu = 0;
            fe.screen.step = s_submenu;
        }
    } else if (menu_cancel(ui) && w.used_cursor != count) {
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        w.used_cursor = count;
        w.frames = 0;
    }
}

void submenu_step(FrontEnd& fe, Ui& ui, WildScreen& w) {
    World& world = *fe.world;
    menu_cursor(ui, w.submenu, 0, 3, 0x17, 0, 4);
    used_stand(fe, ui, w);
    view_draw(fe, ui, w, s_submenu);
    const UsedMachine& m = w.used[static_cast<std::size_t>(w.used_cursor)];
    const PointerRect rect = {187, 228, 460, 260};
    if (menu_accept(ui, &rect)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        if (w.submenu == 0) {
            // the spec page shows this machine
            fe.shop.spec = m.build;
            for (int i = 0; i < glyph_name_length; ++i) {
                fe.shop.spec_name[i] = -1;
                fe.shop.spec_chip_name[i] = -1;
            }
            for (int i = 0; i < 8 && m.build.name.glyphs[static_cast<std::size_t>(i)] != -1; ++i) {
                fe.shop.spec_name[i] = m.build.name.glyphs[static_cast<std::size_t>(i)];
            }
            stand_line_hide(fe.shop.stand);
            leave_to(fe, ui, w, 1);
        } else if (w.submenu == 1) {
            bool any = false, empty_hangar = false;
            for (int g = 0; g < garage_count; ++g) {
                any = any || garage_can_take(fe, m.build, g);
                empty_hangar =
                    empty_hangar || (garage_status(fe.shop.tables, world.garages, g) & 2) != 0;
            }
            fe.screen.step = s_box;
            if (world.story.money < m.price) {
                box(w, 0x10, a_used);
            } else if (inventory_first_free(world.garages) < 0 || (!any && empty_hangar)) {
                // retail gives 0xE ("no empty hangar") when a hangar is free but the inventory
                // can't take every part; the port says the inventory is full (docs/bugs.md)
                box(w, 0xf, a_used);
            } else if (!any) {
                box(w, 0xe, a_used);
            } else {
                w.garage = 0;
                fe.screen.step = s_garages;
            }
        } else {
            fe.screen.step = s_used;
        }
    } else if (menu_cancel(ui)) {
        if (w.submenu != 2) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        w.submenu = 2;
    }
}

void garages_step(FrontEnd& fe, Ui& ui, WildScreen& w) {
    World& world = *fe.world;
    menu_cursor(ui, w.garage, 0, 10, 0x17, 0, 4);
    used_stand(fe, ui, w);
    view_draw(fe, ui, w, s_garages);
    const PointerRect rect = {200, 220, 600, 300};
    if (menu_accept(ui, &rect)) {
        const UsedMachine& m = w.used[static_cast<std::size_t>(w.used_cursor)];
        fe.screen.step = s_box;
        if (garage_can_take(fe, m.build, w.garage)) {
            w.chip_slot = used_deliver(fe.shop.tables, world.garages, m.build, w.garage);
            if (w.chip_slot >= 0) {
                chip_name_from_record(fe, w.chip_slot);
            }
            ui_sound(ui, bank_sys, se_purchase);
            money_add(world.story, -m.price);
            // with a Chip: board it?, then name the Chip; without, back to the list (retail
            // names a stale slot there: docs/bugs.md)
            box(w, 0x13, a_used);
            if (w.chip_slot >= 0) {
                w.after = 102;
            }
        } else if (!garage_unlocked(world.garages, w.garage)) {
            box(w, 0x12, a_garages);
        } else {
            box(w, 0x11, a_garages);
        }
    } else if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        fe.screen.step = s_submenu;
    }
}

} // namespace wild
