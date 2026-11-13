// Wild Machines' used Scoobees (decomp src/o1/u_0201ab.c): the day's machines, their trade
// submenu and the garage wheel a bought machine goes to.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/shops/wild_steps.hpp"
#include "world/world.hpp"

namespace wild {

namespace {

// an empty garage, and room for every part
// decomp: wild_machines_garage_can_take 0x229A2
bool garage_can_take(FrontEnd& fe, const ShopBuild& b, int g) {
    const Garages& gs = fe.world->garages;
    if (!(garage_status(fe.shop.tables, gs, g) & garage_empty)) {
        return false;
    }
    int needed = 0;
    const int parts[7] = {b.body,      b.leg,       b.weapon[0], b.weapon[1],
                          b.weapon[2], b.weapon[3], b.chip};
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

// the help line of the used steps: the list's (or its leave row's), the submenu item's, the
// garage wheel's (D_001DCE04, D_001DCE0C, D_001DCE18); a strip starts over when its line changes
void used_help(Ui& ui, WildScreen& w, int step) {
    const int leave = w.used_cursor == static_cast<int>(w.used.size()) ? 1 : 0;
    const int key = step == s_submenu ? 10 + w.submenu : step == s_garages ? 20 : leave;
    if (key == w.help_for) {
        return;
    }
    w.help_for = key;
    std::string text;
    if (step == s_submenu) {
        text = ui_strings(ui.data, "wild_machines", "D_001DCE0C", w.submenu, 1)[0];
    } else if (step == s_garages) {
        const UiTable* t = ui_table(ui.data, "wild_machines", "wild_machines_menu_rows");
        text = t && !t->row_strings.empty() && !t->row_strings[0].empty() ? t->row_strings[0][0]
                                                                          : std::string();
    } else {
        text = leave ? line(ui, "wild_machines_chip_leave_help", 0)
                     : line(ui, "wild_machines_used_help", 0);
    }
    help_strip_init(ui, w.help, text);
}

} // namespace

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
                w.used.push_back(
                    {b, cheat_shop_price(fe.world->story.cheats, shop_scoobee_total(t, b))});
            }
        }
    }
    const std::vector<double> at =
        ui_values(ui.data, "wild_machines", "wild_machines_used_places", 14);
    for (int slot = 0; slot < 7; ++slot) {
        w.used_x[slot] =
            static_cast<int>(at[static_cast<std::size_t>(slot * 2)]) - (420 - 60 * slot) - 800;
    }
    w.used_cursor = 0;
    w.frames = 0;
    w.help_for = -1;
    used_help(ui, w, s_used);
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
    PointerRect plates[7];
    wild_used_rects(ui, w, plates);
    if (ready && (menu_cursor(ui, w.used_cursor, 0, count + 1, 0x17, 0, 4, plates) != 0 ||
                  menu_wheel(ui, w.used_cursor, 0, count + 1, true, 0, 4) != 0)) {
        w.frames = 0;
    }
    used_help(ui, w, s_used);
    used_stand(fe, ui, w);
    view_draw(fe, ui, w, s_used);
    if (!ready) {
        return;
    }
    if (menu_accept(ui, &plates[w.used_cursor])) {
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
    PointerRect items[3];
    wild_submenu_rects(ui, items);
    if (menu_cursor(ui, w.submenu, 0, 3, 0x17, 0, 4, items) == 0) {
        menu_wheel(ui, w.submenu, 0, 3, true, 0, 4);
    }
    used_help(ui, w, s_submenu);
    used_stand(fe, ui, w);
    view_draw(fe, ui, w, s_submenu);
    const UsedMachine& m = w.used[static_cast<std::size_t>(w.used_cursor)];
    if (menu_accept(ui, &items[w.submenu])) {
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
                empty_hangar = empty_hangar || (garage_status(fe.shop.tables, world.garages, g) &
                                                garage_empty) != 0;
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
                // retail starts on garage 1, or where the wheel was left; the port starts on
                // the first garage that can take the machine (docs/bugs.md)
                w.garage = 0;
                while (!garage_can_take(fe, m.build, w.garage)) {
                    ++w.garage;
                }
                w.garage_before = w.garage;
                w.garage_wait = 8;
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

// decomp: wild_machines_used_garage_select_update 0x250A6
void garages_step(FrontEnd& fe, Ui& ui, WildScreen& w) {
    World& world = *fe.world;
    // the wheel takes a step only at rest, then slides 8 frames
    PointerRect marks[5];
    wild_garage_rects(ui, marks);
    if (w.garage_wait == 8) {
        w.garage_before = w.garage;
        bool moved = menu_cursor(ui, w.garage, 0, 10, 0x17, 0, 4) != 0 ||
                     menu_wheel(ui, w.garage, 0, 10, true, 0, 4) != 0;
        // PC: a click on a side label or an arrow turns the wheel a place
        if (!moved && ui.pointer.left) {
            const int k = pointer_pick(ui.pointer, marks, 5) - 2;
            if (k >= -2 && k != 0) {
                w.garage = (w.garage + (k > 0 ? 1 : 9)) % 10;
                ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
                menu_input_mark(ui);
                ui.pointer.left = false;
                moved = true;
            }
        }
        if (moved) {
            w.garage_wait = 0;
        }
    } else {
        ++w.garage_wait;
    }
    used_help(ui, w, s_garages);
    used_stand(fe, ui, w);
    view_draw(fe, ui, w, s_garages);
    if (menu_accept(ui, &marks[2])) {
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
