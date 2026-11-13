// Wild Machines (state 0xF, decomp src/o1/u_0201ab.c, u_01c789.c): the six-item wheel, buying and
// selling modules, colours and stickers, buying and selling Chips, used Scoobees, and the trips to
// the spec page and name entry that come back to where they left. Every box is
// wild_machines_boxes (MsgBox[31]).
#include "screens/shops/wild_machines.hpp"

#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/garage/chip_carousel.hpp"
#include "screens/shops/wild_steps.hpp"
#include "world/world.hpp"

namespace wild {

constexpr int top_start[4] = {-236, -171, -106, -41};
constexpr int bottom_start[4] = {-193, -128, -63, 2};

float half(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

std::string line(Ui& ui, const char* table, int i) {
    return ui_strings(ui.data, "wild_machines", table, i, 1)[0];
}

void box(WildScreen& w, int m, int after) {
    w.message = m;
    w.after = after;
    w.pick = 1;
    msg_box_open(w.boxes[m]);
}

bool selling(const WildScreen& w) {
    return w.mode == 4 || w.mode == 5;
}

void view_draw(FrontEnd& fe, Ui& ui, WildScreen& w, int step) {
    switch (w.mode) {
    case 0:
        wild_menu_draw(fe, ui, w);
        break;
    case 1:
    case 4:
        wild_parts_frame_draw(fe, ui, w, selling(w), step == s_tabs);
        if (step != s_tabs && w.tab < 7) {
            wild_parts_list_draw(fe, ui, w, selling(w), step == s_list);
        }
        break;
    case 2:
    case 5:
        wild_chip_draw(fe, ui, w, step == s_chips, selling(w));
        if (step == s_chip_confirm) {
            wild_card_confirm_draw(ui, w, selling(w));
        }
        break;
    case 3:
        wild_used_draw(fe, ui, w, step == s_used);
        if (step == s_submenu) {
            wild_submenu_draw(ui, w);
        } else if (step == s_garages) {
            wild_garage_wheel_draw(fe, ui, w);
        }
        break;
    }
    if (w.mode != 0 && w.mode != 2 && w.mode != 5) {
        text_strip_draw_scroll(ui, w.help, 0xfff0f0f0);
    }
}

GaragePage& rows_of(WildScreen& w) {
    return selling(w) ? w.editor.pages[w.kind] : w.stock;
}

void pages_build(FrontEnd& fe, WildScreen& w) {
    garage_pages_build(fe.shop.tables, fe.world->garages, w.editor.pages);
    garage_pages_mark_fitted(fe.world->garages, w.editor.pages);
}

void mode_enter(FrontEnd& fe, Ui& ui, WildScreen& w) {
    w.header_x = -400;
    w.header_delay = 20;
    w.cursor = w.scroll = 0;
    w.used_sound_done = false;
    switch (w.mode) {
    case 1:
    case 4:
        w.col = w.row = w.tab = 0;
        slide_init(w.top, 4, top_start);
        slide_init(w.bottom, 4, bottom_start);
        slide_start(ui, w.top);
        slide_start(ui, w.bottom);
        pop_timer_init(w.timers[0], selling(w) ? 0xb59 : 0xb2c, 5, 10);
        pop_timer_init(w.timers[1], 0xb40, 6, 12);
        pop_timer_init(w.timers[2], 0xb41, 6, 12);
        if (selling(w)) {
            pages_build(fe, w);
        }
        help_strip_init(
            ui, w.help,
            line(ui, selling(w) ? "wild_machines_sell_help" : "wild_machines_buy_help", 0));
        break;
    case 2: {
        // wild_machines_chip_stock_fill: chip_stock[day_schedule[day % 60].tournament[3]]
        w.chips = ChipStrip{};
        const int r = day_row(fe.story_tables, fe.world->story.day).tournament[3];
        w.chip_stock = r >= 0 && r < static_cast<int>(fe.shop.tables.chip_stock.size())
                           ? fe.shop.tables.chip_stock[static_cast<std::size_t>(r)]
                           : std::vector<StockItem>{};
        while (!w.chip_stock.empty() && w.chip_stock.back().id < 0) {
            w.chip_stock.pop_back();
        }
        help_strip_init(ui, w.help,
                        w.chip_stock.empty()
                            ? std::string()
                            : line(ui, "wild_machines_chip_help", w.chip_stock[0].id));
        break;
    }
    case 5:
        w.chips = ChipStrip{};
        pages_build(fe, w);
        break;
    case 3:
        used_fill(fe, ui, w);
        break;
    }
}

void go_mode(Ui& ui, WildScreen& w, int mode) {
    w.next = mode;
    fade_out_start(ui, half(ui));
}

void leave_to(FrontEnd& fe, Ui& ui, WildScreen& w, int result) {
    WildReturn& r = fe.shop.wild;
    r.menu = w.menu;
    r.cursor = w.used_cursor;
    r.next = w.mode;
    w.result = result;
    fade_out_start(ui, half(ui));
    fe.screen.step = s_leave;
}

} // namespace wild

using namespace wild;

namespace {

// an OK box's way on, and a yes/no's yes
void after(FrontEnd& fe, Ui& ui, WildScreen& w, int what) {
    World& world = *fe.world;
    const PartTables& t = fe.shop.tables;
    GaragePage& page = rows_of(w);
    const int row = page.entries.empty() ? 0
                                         : (w.scroll + w.cursor) % static_cast<int>(page.entries.size());
    switch (what) {
    case a_list:
        // nothing left to sell: say so, then the tabs
        if (selling(w) && rows_of(w).entries.empty()) {
            box(w, 0x18, a_tabs);
            return;
        }
        fe.screen.step = s_list;
        break;
    case a_tabs:
        fe.screen.step = s_tabs;
        break;
    case a_hub:
        go_mode(ui, w, 0);
        fe.screen.step = s_switch;
        break;
    case a_chips:
        fe.screen.step = s_chips;
        break;
    case a_chips_reset:
        mode_enter(fe, ui, w);
        fe.screen.step = s_chips;
        break;
    case a_used:
        fe.screen.step = s_used;
        break;
    case a_garages:
        fe.screen.step = s_garages;
        break;
    case a_buy: { // _part_buy_confirm_update 0x22B2B
        const GarageEntry& e = page.entries[static_cast<std::size_t>(row)];
        if (w.kind == 6) {
            world.garages.colors_owned[e.id] |= 1;
        } else if (w.kind == 7) {
            world.garages.stickers_owned[e.id] |= 1;
        } else {
            InventoryItem& item = world.garages.inventory[inventory_first_free(world.garages)];
            item = InventoryItem{};
            item.tune = static_cast<std::int8_t>(e.tune);
            item.kind = static_cast<std::int16_t>(w.kind);
            item.part = static_cast<std::int16_t>(e.id);
        }
        money_add(world.story, -w.price);
        ui_sound(ui, bank_sys, se_purchase);
        box(w, 0xd, a_list);
        break;
    }
    case a_sell: { // wild_machines_part_sell_confirm_update
        const GarageEntry& e = page.entries[static_cast<std::size_t>(row)];
        if (w.kind == 6) {
            world.garages.colors_owned[e.id] &= static_cast<std::uint8_t>(~1);
        } else if (w.kind == 7) {
            world.garages.stickers_owned[e.id] &= static_cast<std::uint8_t>(~1);
        } else {
            world.garages.inventory[e.item] = InventoryItem{};
            world.garages.inventory[e.item].tune = 0;
        }
        ui_sound(ui, bank_sys, se_purchase);
        money_add(world.story, w.price);
        pages_build(fe, w);
        w.cursor = w.scroll = 0;
        box(w, 0x19, a_list);
        break;
    }
    case a_chip_sell: // the sale waits for 0x1D's OK
        box(w, 0x1d, a_chip_sold);
        break;
    case a_chip_sold: {
        const GarageEntry& e = w.editor.pages[4].entries[static_cast<std::size_t>(w.chips.cursor)];
        money_add(world.story, wild_price(t, 4, e.id, e.tune, true));
        world.garages.inventory[e.item] = InventoryItem{};
        pages_build(fe, w);
        const int count = static_cast<int>(w.editor.pages[4].entries.size());
        if (w.chips.cursor >= count && w.chips.cursor > 0) {
            w.chips.cursor = w.chips.shown = count - 1;
        }
        if (count == 0) {
            box(w, 0x1a, a_hub);
            return;
        }
        fe.screen.step = s_chips;
        break;
    }
    case a_chip_name: // name the bought Chip
        name_target_set(fe, 2, world.garages.inventory[w.chip_slot].name);
        fe.shop.wild.chip_slot = w.chip_slot;
        leave_to(fe, ui, w, 3);
        break;
    case a_used_board:
        world.garages.selected = w.garage;
        box(w, 0x15, a_used_name);
        break;
    case a_used_name:
        box(w, 0x1e, -1);
        w.after = 100; // then name entry on the Chip
        break;
    default:
        break;
    }
}

void hub_step(FrontEnd& fe, Ui& ui, WildScreen& w) {
    if (!w.turning) {
        menu_cursor(ui, w.menu, 0, 6, 0x17, -1, -1);
        if (ui.pointer.wheel != 0) {
            w.menu = (w.menu + (ui.pointer.wheel > 0 ? 5 : 1)) % 6;
            menu_input_mark(ui);
        }
    }
    if (w.menu != w.menu_before && !w.turning) {
        ui_sound(ui, bank_sys, se_carousel_turn);
        text_strip_init(ui, w.labels[w.menu], 85, 211,
                        ui_strings(ui.data, "wild_machines", "wild_machines_menu_lines", w.menu, 1)[0],
                        85, 211, 265, 229);
        w.turning = true;
        w.menu_frames = 0;
    } else if (w.turning && ++w.menu_frames > 8) {
        w.turning = false;
        w.menu_frames = 0;
        w.menu_before = w.menu;
    }
    wild_menu_draw(fe, ui, w);
    const PointerRect lit = wild_menu_rect(ui, w);
    if (menu_accept(ui, &lit)) {
        if (w.menu == 5) {
            ui_sound(ui, bank_sys, se_shop_leave);
            w.result = 0;
            fade_out_start(ui, half(ui));
            fe.screen.step = s_leave;
            return;
        }
        ui_sound(ui, bank_sys, se_menu_accept);
        if (w.menu == 4) {
            pages_build(fe, w);
            if (w.editor.pages[4].entries.empty()) {
                box(w, 0x1a, a_hub);
                w.after = -2; // back to the hub's input
                fe.screen.step = s_box;
                return;
            }
        }
        go_mode(ui, w, w.menu + 1);
        fe.screen.step = s_switch;
    } else if (menu_cancel(ui)) {
        w.menu_before = w.menu;
        w.menu = 5;
    }
}

} // namespace

void wild_spawn(FrontEnd&, Ui&) {}

void wild_step(FrontEnd& fe, Ui& ui) {
    WildScreen& w = fe.screen.wild;
    // the stand: the fitted part growing in, the used machines turning and sliding; the mouse's
    // grab kept clear of this step's menus (the list's rows, the submenu, the garage wheel)
    if (fe.screen.step != s_update) {
        const int step = fe.screen.step;
        stand_area_set(fe.shop.stand, step == s_list      ? PointerRect{360, 80, 640, 330}
                                      : step == s_used    ? PointerRect{430, 80, 640, 420}
                                      : step == s_submenu ? PointerRect{470, 80, 640, 420}
                                                          : PointerRect{});
        stand_update(ui, fe.shop.stand);
    }
    switch (fe.screen.step) {
    case s_update: { // wild_machines_screen_update
        for (const int sheet : {0x690, 0x68e, 0x692, 0x693}) {
            texture_set_load(ui, sheet);
        }
        for (int i = 0; i < 6; ++i) {
            text_strip_init(ui, w.labels[i], 85, 211,
                            ui_strings(ui.data, "wild_machines", "wild_machines_menu_lines", i, 1)[0],
                            85, 211, 265, 229);
        }
        for (int i = 0; i < 31; ++i) {
            w.boxes[i] = msg_box_from(ui, "wild_machines", "wild_machines_boxes", i);
        }
        state_music_play(fe, ui, 0xf);
        stand_start(ui, fe.shop.stand, 3); // model_viewer mode 3
        // back from the spec page or name entry: where it was left
        const int from = fe.previous;
        const WildReturn& r = fe.shop.wild;
        w.mode = 0;
        if (from == app_spec_scoobee || (from == app_name_entry && r.next == 3)) {
            w.mode = 3;
            w.menu = w.menu_before = 2;
            mode_enter(fe, ui, w);
            w.used_cursor = r.cursor;
            w.header_delay = 0;
            w.header_x = 0;
        } else if (from == app_name_entry) {
            w.mode = 2;
            w.menu = w.menu_before = 1;
            mode_enter(fe, ui, w);
        }
        if (w.mode != 0) {
            level_meter_spawn(ui, fe.screen.meter);
        } else {
            level_meter_spawn(ui, fe.screen.meter, "level_meter_b");
        }
        w.frames = static_cast<int>(half(ui));
        fade_in_start(ui, static_cast<float>(w.frames));
        if (from == app_town_map) {
            ui_sound(ui, bank_sys, se_shop_arrive);
        }
        w.banner = -1;
        fe.screen.step = s_fade_in;
        break;
    }
    case s_fade_in:
        view_draw(fe, ui, w, s_fade_in);
        if (fade_is_idle(ui)) {
            w.banner = task_timer_get(ui);
            now_playing_spawn(ui, fe.screen.strip, 0, w.mode == 0 ? 0x153 : 0x1a3, 0x17);
            const int steps[6] = {s_hub, s_tabs, s_chips, s_used, s_tabs, s_chips};
            fe.screen.step = steps[w.mode];
            if (w.mode == 3 && fe.previous == app_spec_scoobee) {
                w.submenu = 0;
                fe.screen.step = s_submenu;
            }
        }
        break;
    case s_hub:
        hub_step(fe, ui, w);
        break;
    case s_switch: // wild_machines_mode_change: the fade, the mode's set up, the fade in
        view_draw(fe, ui, w, s_switch);
        if (fade_is_black(ui)) {
            w.mode = w.next;
            mode_enter(fe, ui, w);
            if (w.mode != 0) {
                level_meter_spawn(ui, fe.screen.meter);
            } else {
                level_meter_spawn(ui, fe.screen.meter, "level_meter_b");
            }
            fade_in_start(ui, half(ui));
            fe.screen.step = s_fade_in;
        }
        break;
    case s_leave: // wild_machines_screen_leave
        view_draw(fe, ui, w, s_leave);
        if (fade_is_black(ui)) {
            fe.screen.strip.active = false;
            ui_music_stop(ui);
            task_header_set(fe, w.result);
            fe.screen.step = s_done;
        }
        break;
    case s_box: { // an OK box
        view_draw(fe, ui, w, s_box);
        MsgBox& b = w.boxes[w.message];
        msg_box_update_ok(ui, b);
        const PointerRect ok = msg_box_ok_rect(ui, b);
        if (menu_accept(ui, &ok)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            msg_box_open(b);
            if (w.after == -2) {
                fe.screen.step = s_hub;
            } else if (w.after == 100) { // 0x1E closed: name the used machine's Chip
                fe.shop.wild.next = 3;
                name_target_set(fe, 2, fe.world->garages.inventory[w.chip_slot].name);
                leave_to(fe, ui, w, 2);
                fe.shop.wild.next = 3;
            } else if (w.after == 101) { // the Chip bought: 0x1E, then name entry
                box(w, 0x1e, a_chip_name);
            } else if (w.after == 102) { // the used machine delivered: board it?
                box(w, 0x14, a_used_board);
                fe.screen.step = s_yes_no;
            } else {
                after(fe, ui, w, w.after);
            }
        }
        break;
    }
    case s_yes_no: {
        view_draw(fe, ui, w, s_yes_no);
        MsgBox& b = w.boxes[w.message];
        PointerRect answers[2];
        msg_box_choice_rects(ui, b, answers);
        menu_cursor(ui, w.pick, 0, 2, 0x13, 0, 4, b.row.done ? answers : nullptr);
        msg_box_update_choice(ui, b, w.pick);
        if (menu_accept(ui, &answers[w.pick])) {
            msg_box_open(b);
            fe.screen.step = s_box;
            if (w.pick == 0) {
                after(fe, ui, w, w.after);
            } else if (w.after == a_used_board) {
                box(w, 0x1e, -1);
                w.after = 100;
            } else {
                fe.screen.step = w.after == a_chip_sell ? s_chips : s_list;
            }
        } else if (menu_cancel(ui) && w.pick != 1) {
            w.pick = 1;
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        break;
    }
    case s_tabs:
        tabs_step(fe, ui, w);
        break;
    case s_list:
        list_step(fe, ui, w);
        break;
    case s_chips:
        chips_step(fe, ui, w);
        break;
    case s_chip_confirm:
        chip_confirm_step(fe, ui, w);
        break;
    case s_used:
        used_step(fe, ui, w);
        break;
    case s_submenu:
        submenu_step(fe, ui, w);
        break;
    case s_garages:
        garages_step(fe, ui, w);
        break;
    default:
        view_draw(fe, ui, w, s_done);
        break;
    }
}

void wild_destroy(FrontEnd& fe, Ui& ui) {
    stand_hide(fe.shop.stand);
    ui_music_stop(ui);
    ui.clear_colour = 0xff000000;
}
