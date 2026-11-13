// Plus Tech (state 0x1B, decomp src/o1/u_02edc5.c, src/hand/shop.c): the hub's three-item wheel,
// the module tuning view (tabs, the part list, the tune, its price and the confirm) and the Chip
// Uplift view (plus_tech_chips.cpp). Every message box is plus_tech_boxes.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

namespace {

enum Step {
    step_update,
    step_fade_in,  // step2
    step_hub,      // step3
    step_switch,   // step4
    step_leave,    // step5
    step_view_in,  // step8
    step_tabs,     // step9
    step_list_in,  // step12
    step_boxes,    // step13
    step_list,     // step14
    step_tune,     // step15
    step_confirm,  // plus_tech_part_confirm_update
    step_chips,    // the Chip view: plus_tech_chip_step's 0..3 at step_chips + n
    step_done = step_chips + 4
};

constexpr int sheets[6][2] = {{0x690, 0}, {0x67d, 0}, {0x692, 0}, {0x693, 0}, {0x66c, 0}, {0x68b, 0}};

float half(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

std::string line_of(Ui& ui, const char* table, int i, int count) {
    const std::vector<std::string> lines = ui_strings(ui.data, "plus_tech", table, i, 1);
    return i >= 0 && i < count ? lines[0] : std::string();
}

void label_init(Ui& ui, PlusTechScreen& p, int i) {
    text_strip_init(ui, p.labels[i], 85, 211, line_of(ui, "plus_tech_labels", i, 3), 85, 211, 265,
                    229);
}

void tab_help(Ui& ui, PlusTechScreen& p) {
    help_strip_init(ui, p.help, line_of(ui, "plus_tech_tab_help", p.tab, 5));
}

// a part's blurb: D_001FADDC[kind] is where its kind's lines start
void blurb_init(Ui& ui, PlusTechScreen& p, int part) {
    const int starts[6] = {0, 0, 4, 0, 0, 0};
    const char* tables[6] = {"plus_tech_blurbs_body", "plus_tech_blurbs_legs",
                             "plus_tech_blurbs_arm",  "plus_tech_blurbs_back",
                             "plus_tech_blurbs_chip", "plus_tech_blurbs_option"};
    const int counts[6] = {16, 43, 76, 69, 96, 107};
    if (p.kind < 0 || p.kind > 5 || part < 0 || part >= counts[p.kind]) {
        return;
    }
    help_strip_init(ui, p.help,
                    ui_strings(ui.data, "plus_tech", tables[p.kind], starts[p.kind] + part, 1)[0]);
}

void pages_build(FrontEnd& fe, PlusTechScreen& p) {
    garage_pages_build(fe.shop.tables, fe.world->garages, p.editor.pages);
    garage_pages_mark_fitted(fe.world->garages, p.editor.pages);
}

// plus_tech_stand_fit_part: the part under the list's cursor onto the stand
void stand_fit(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    const GarageEntry* e = plus_tech_entry(p);
    stand_fit_part(ui, fe.shop.stand, p.kind, e ? e->id : -1);
}

void message(PlusTechScreen& p, int m) {
    p.message = m;
    msg_box_open(p.boxes[m]);
}

void view_draw(FrontEnd& fe, Ui& ui, PlusTechScreen& p, int step) {
    if (p.view == 2 || (step >= step_chips && step < step_done)) {
        plus_tech_chip_list_draw(fe, ui, p, step == step_chips);
        if (step == step_chips + 1 || (step == step_chips + 2 && p.message == 6)) {
            plus_tech_chip_card_draw(fe, ui, p);
        }
        return;
    }
    plus_tech_frame_draw(fe, ui, p, step == step_tabs);
    if (step != step_tabs && step != step_view_in && !(step == step_switch && p.next == 0 &&
                                                       p.view == 1 && p.kind < 0)) {
        plus_tech_part_list_draw(fe, ui, p, step == step_list);
        if (step == step_tune || step == step_confirm ||
            (step == step_boxes && (p.message == 0 || p.message == 2 || p.message == 3))) {
            plus_tech_tune_card_draw(fe, ui, p, step == step_tune);
        }
    }
}

// step3: the hub
void hub_step(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    if (!p.turning) {
        menu_cursor(ui, p.menu, 0, 3, 0x17, -1, -1);
        if (ui.pointer.wheel != 0) {
            p.menu = (p.menu + (ui.pointer.wheel > 0 ? 2 : 1)) % 3;
            menu_input_mark(ui);
        }
    }
    if (p.menu != p.menu_before && !p.turning) {
        ui_sound(ui, bank_sys, se_carousel_turn);
        label_init(ui, p, p.menu);
        p.turning = true;
        p.menu_frames = 0;
    } else if (p.turning && ++p.menu_frames > 8) {
        p.turning = false;
        p.menu_frames = 0;
        p.menu_before = p.menu;
    }
    plus_tech_menu_draw(fe, ui, p);
    const std::vector<double> lit = ui_values(ui.data, "plus_tech", "plus_tech_menu_lit", 3);
    const PointerRect rect = menu_wheel_rect(ui, static_cast<int>(lit[static_cast<std::size_t>(p.menu)]));
    if (menu_accept(ui, &rect)) {
        fade_out_start(ui, half(ui));
        if (p.menu == 2) {
            ui_sound(ui, bank_sys, se_shop_leave);
            fe.screen.step = step_leave;
        } else {
            ui_sound(ui, bank_sys, se_menu_accept);
            p.next = p.menu + 1;
            fe.screen.step = step_switch;
        }
    } else if (menu_cancel(ui)) {
        p.menu_before = p.menu;
        p.menu = 2;
    }
}

// step4: the fade out of a view, then the next one's set up
void switch_step(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    if (p.next != 0 || p.view == 0) {
        plus_tech_menu_draw(fe, ui, p);
    } else {
        view_draw(fe, ui, p, step_switch);
    }
    if (!fade_is_black(ui)) {
        return;
    }
    p.banner = task_timer_get(ui);
    if (p.next != 0) {
        level_meter_spawn(ui, fe.screen.meter);
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
    } else {
        level_meter_spawn(ui, fe.screen.meter, "level_meter_b");
        now_playing_spawn(ui, fe.screen.strip, 0, 0x153, 0x17);
    }
    fade_in_start(ui, half(ui));
    if (p.next == 0) {
        p.view = 0;
        fe.screen.step = step_hub;
    } else if (p.next == 1) {
        // step6: the module view's set up
        p.view = 1;
        p.tab = 0;
        p.kind = 0;
        p.scroll[0] = p.scroll[1] = p.scroll[2] = 0;
        pages_build(fe, p);
        p.tabs.started = -1;
        pop_timer_init(p.timers[0], 0x48a, 5, 10);
        pop_timer_init(p.timers[1], 0x48d, 6, 12);
        pop_timer_init(p.timers[2], 0x48e, 6, 12);
        pop_timer_init(p.timers[3], 0x490, 4, 8);
        tab_help(ui, p);
        fe.screen.step = step_view_in;
    } else {
        p.view = 2;
        plus_tech_chip_setup(fe, ui, p);
        fe.screen.step = step_view_in;
    }
}

void list_in(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    // step12: rebuild, the blurbs, the plates pop again
    pages_build(fe, p);
    for (int i = 0; i < 3; ++i) {
        pop_timer_start(ui, p.timers[i], 1.0f, 1.0f, false, 0, 0);
    }
    if (const GarageEntry* e = plus_tech_entry(p)) {
        p.tune = p.tune_was = e->tune;
        p.blurb_part = e->id;
        blurb_init(ui, p, e->id);
    }
    stand_fit(fe, ui, p);
    fe.screen.step = step_list;
}

void tabs_step(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    PointerRect rects[5];
    for (int i = 0; i < 5; ++i) {
        rects[i] =
            sprite_rect(ui,
                        static_cast<int>(ui_values(ui.data, "plus_tech", "plus_tech_frame_ids",
                                                   5)[static_cast<std::size_t>(i)]),
                        static_cast<float>(p.tabs.pos[i]), 263);
    }
    if (menu_cursor(ui, p.tab, 0, 5, 0x17, 0, 4, rects)) {
        tab_help(ui, p);
    }
    const int kinds[4] = {0, 2, 3, 1}; // D_001FB1B0
    p.kind = p.tab < 4 ? kinds[p.tab] : -1;
    view_draw(fe, ui, p, step_tabs);
    text_strip_draw_scroll(ui, p.help, 0xfff2e8e8);
    if (menu_accept(ui, &rects[p.tab])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        if (p.tab == 4) {
            p.next = 0;
            p.view = 1;
            fade_out_start(ui, half(ui));
            fe.screen.step = step_switch;
            return;
        }
        pages_build(fe, p);
        if (p.editor.pages[p.kind].entries.empty()) {
            message(p, 4);
            fe.screen.step = step_boxes;
        } else {
            p.cursor_row = p.scroll_row = 0;
            fe.screen.step = step_list_in;
        }
    } else if (menu_cancel(ui) && p.tab != 4) {
        p.tab = 4;
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
    }
}

void list_step(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    GaragePage& page = p.editor.pages[p.kind];
    const int count = static_cast<int>(page.entries.size());
    int moved = 0;
    menu_list_move(ui.pad, p.cursor_row, p.scroll_row, 0, count, 4, 0x12, moved);
    PointerRect rows[4];
    for (int i = 0; i < 4; ++i) {
        rows[i] = {43, 220.0f + 38.0f * i, 340, 258.0f + 38.0f * i};
    }
    const int hover = ui.pointer.moved ? pointer_pick(ui.pointer, rows, count < 4 ? count : 4) : -1;
    if (hover >= 0 && hover != p.cursor_row) {
        p.cursor_row = hover;
        moved = 1;
    }
    if (moved) {
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
        if (const GarageEntry* e = plus_tech_entry(p)) {
            p.tune = p.tune_was = e->tune;
            if (e->id != p.blurb_part) {
                p.blurb_part = e->id;
                blurb_init(ui, p, e->id);
            }
        }
        stand_fit(fe, ui, p);
    }
    view_draw(fe, ui, p, step_list);
    text_strip_draw_scroll(ui, p.help, 0xfff2e8e8);
    if (menu_accept(ui, &rows[p.cursor_row])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        // plus_tech_part_confirm_enter
        p.tune_was = p.tune;
        help_strip_init(ui, p.help, ui_strings(ui.data, "plus_tech", "shop_level_help", 0, 1)[0]);
        pop_timer_start(ui, p.timers[3], 1.0f, 1.0f, false, 0, 0);
        fe.screen.step = step_tune;
    } else if (menu_cancel(ui)) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        tab_help(ui, p);
        stand_hide(fe.shop.stand); // viewer.pending = -1
        fe.screen.step = step_tabs;
    } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_x)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        garage_page_sort(page, 0, page.column != 0 ? true : !page.descending);
        stand_fit(fe, ui, p);
    } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_y)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        garage_page_sort(page, 1, page.column != 1 ? true : !page.descending);
        stand_fit(fe, ui, p);
    }
}

void tune_step(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    menu_cursor(ui, p.tune, -99, 199, 0x13, 0, 4);
    if (ui.pointer.wheel != 0) {
        const int to = std::min(99, std::max(-99, p.tune + ui.pointer.wheel));
        if (to != p.tune) {
            p.tune = to;
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            menu_input_mark(ui);
        }
    }
    view_draw(fe, ui, p, step_tune);
    text_strip_draw_scroll(ui, p.help, 0xfff2e8e8);
    const GarageEntry* e = plus_tech_entry(p);
    const PointerRect card = {43, 220, 340, 400};
    if (menu_accept(ui, &card)) {
        if (!e || p.tune == p.tune_was) {
            return;
        }
        p.price = part_tune_price(fe.shop.tables, p.kind, e->id, p.tune_was, p.tune);
        ui_sound(ui, bank_sys, se_menu_accept);
        if (fe.world->story.money < p.price) {
            message(p, 0);
            fe.screen.step = step_boxes;
        } else {
            message(p, 1);
            p.pick = 1;
            fe.screen.step = step_confirm;
        }
    } else if (menu_cancel(ui)) {
        p.tune = p.tune_was;
        ui_sound(ui, bank_sys, se_menu_cancel);
        if (e) {
            blurb_init(ui, p, e->id);
        }
        fe.screen.step = step_list;
    }
}

// pay, set the tune; a body tuned below its options'
// sockets drops them
// decomp: plus_tech_part_confirm_update 0x31A62
void confirm_step(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    MsgBox& box = p.boxes[1];
    PointerRect answers[2];
    msg_box_choice_rects(ui, box, answers);
    menu_cursor(ui, p.pick, 0, 2, 0x13, 0, 4, box.row.done ? answers : nullptr);
    view_draw(fe, ui, p, step_confirm);
    msg_box_update_choice(ui, box, p.pick);
    World& w = *fe.world;
    if (menu_accept(ui, &answers[p.pick])) {
        msg_box_open(box);
        const GarageEntry* e = plus_tech_entry(p);
        if (p.pick != 0 || !e) {
            fe.screen.step = step_tune;
            return;
        }
        ui_sound(ui, bank_sys, se_purchase);
        money_add(w.story, -p.price);
        InventoryItem& item = w.garages.inventory[e->item];
        item.tune = static_cast<std::int8_t>(p.tune);
        int m = 2;
        if (p.kind == 0 && item.garage >= 0 && item.garage < garage_count) {
            Garage& l = w.garages.garages[item.garage];
            const int sockets = static_cast<int>(
                curve_value(body_part(fe.shop.tables, item.part)->sockets, item.tune));
            if (sockets < loadout_option_socket_sum(fe.shop.tables, w.garages, l)) {
                // retail frees empty slots too, writing inventory[-1] (docs/bugs.md)
                for (std::int16_t& option : l.option) {
                    if (option >= 0 && option < inventory_size) {
                        w.garages.inventory[option].garage = -1;
                    }
                    option = -1;
                }
                m = 3;
            }
        }
        message(p, m);
        fe.screen.step = step_boxes;
    } else if (menu_cancel(ui) && p.pick != 1) {
        p.pick = 1;
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
    }
}

// step13: the OK boxes
void boxes_step(FrontEnd& fe, Ui& ui, PlusTechScreen& p) {
    view_draw(fe, ui, p, step_boxes);
    MsgBox& box = p.boxes[p.message];
    msg_box_update_ok(ui, box);
    const PointerRect ok = msg_box_ok_rect(ui, box);
    if (menu_accept(ui, &ok)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        msg_box_open(box);
        if (p.message == 0) {
            fe.screen.step = step_tune;
        } else if (p.message == 2 || p.message == 3) {
            fe.screen.step = step_list_in;
        } else {
            tab_help(ui, p);
            fe.screen.step = step_tabs;
        }
    }
}

} // namespace

void plus_tech_spawn(FrontEnd&, Ui&) {}

void plus_tech_step(FrontEnd& fe, Ui& ui) {
    PlusTechScreen& p = fe.screen.plus;
    const int step = fe.screen.step;
    // the mouse may grab the part on the stand in the list and the tuning (no box up then)
    stand_area_set(fe.shop.stand, step == step_list || step == step_tune
                                      ? PointerRect{360, 96, 640, 330}
                                      : PointerRect{});
    stand_update(ui, fe.shop.stand);
    switch (step) {
    case step_update: // plus_tech_screen_update
        stand_start(ui, fe.shop.stand, 4); // model_viewer mode 4
        for (const auto& sheet : sheets) {
            texture_set_load(ui, sheet[0]);
        }
        level_meter_spawn(ui, fe.screen.meter, "level_meter_b");
        for (int i = 0; i < 3; ++i) {
            label_init(ui, p, i);
        }
        for (int i = 0; i < 10; ++i) {
            p.boxes[i] = msg_box_from(ui, "plus_tech", "plus_tech_boxes", i);
        }
        state_music_play(fe, ui, 0x1b);
        p.frames = static_cast<int>(half(ui));
        fade_in_start(ui, static_cast<float>(p.frames));
        if (fe.previous == app_town_map) {
            ui_sound(ui, bank_sys, se_shop_arrive);
        }
        p.banner = -1;
        fe.screen.step = step_fade_in;
        break;
    case step_fade_in: // step2
        plus_tech_menu_draw(fe, ui, p);
        if (--p.frames < 0) {
            p.banner = task_timer_get(ui);
            now_playing_spawn(ui, fe.screen.strip, 0, 0x153, 0x17);
            fe.screen.step = step_hub;
        }
        break;
    case step_hub:
        hub_step(fe, ui, p);
        break;
    case step_switch:
        switch_step(fe, ui, p);
        break;
    case step_leave: // step5
        plus_tech_menu_draw(fe, ui, p);
        if (fade_is_black(ui)) {
            ui_music_stop(ui);
            task_header_set(fe, 0);
            fe.screen.step = step_done;
        }
        break;
    case step_view_in: // step8
        view_draw(fe, ui, p, step_view_in);
        if (fade_is_idle(ui)) {
            if (p.view == 1) {
                fe.screen.step = step_tabs;
            } else if (fe.screen.plus.editor.pages[4].entries.empty()) {
                message(p, 8);
                fe.screen.step = step_chips + 3;
            } else {
                fe.screen.step = step_chips;
            }
        }
        break;
    case step_tabs:
        tabs_step(fe, ui, p);
        break;
    case step_list_in:
        list_in(fe, ui, p);
        view_draw(fe, ui, p, step_list);
        break;
    case step_boxes:
        boxes_step(fe, ui, p);
        break;
    case step_list:
        list_step(fe, ui, p);
        break;
    case step_tune:
        tune_step(fe, ui, p);
        break;
    case step_confirm:
        confirm_step(fe, ui, p);
        break;
    default:
        if (step >= step_chips && step < step_done) {
            const int next = plus_tech_chip_step(fe, ui, p, step - step_chips);
            if (next == 100) {
                p.next = 0;
                p.view = 2;
                fade_out_start(ui, half(ui));
                fe.screen.step = step_switch;
            } else if (next >= 0) {
                fe.screen.step = step_chips + next;
            }
        } else {
            plus_tech_menu_draw(fe, ui, p);
        }
        break;
    }
}

void plus_tech_destroy(FrontEnd& fe, Ui& ui) {
    stand_hide(fe.shop.stand);
    ui_music_stop(ui);
    ui.clear_colour = 0xff000000;
}
