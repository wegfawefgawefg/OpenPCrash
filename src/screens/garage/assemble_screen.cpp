// The parts editor, "assemble" (state 4, decomp src/hand/garage.c and src/o1/u_012ff5.c): the
// category grid, the weapon side choice, the part list, the part card, the dismantle and
// options-dropped boxes, and the way out to the hangar, the spec page or name entry. The mouse
// drives the same cursor; a list row can be dragged onto the stand or its grid cell to fit it
// (the list's accept and the card's accept in one move).
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

namespace {

constexpr int common_asset = 0x690, assemble_asset = 0x669, module_asset = 0x692;

enum Step {
    step_update,
    step_fade_in,
    step_grid,
    step_side,
    step_list,
    step_card,
    step_dropped,
    step_dismantle,
    step_back,
    step_rename,
    step_spec,
    step_exit,
    step_leave,
    step_done
};

float half_second(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

// writes the working loadout back, the marks to the inventory, and the status
void commit(FrontEnd& fe, AssembleScreen& s) {
    World& w = *fe.world;
    GarageEditor& e = s.editor;
    if (e.garage >= 0 && e.garage < garage_count) {
        w.garages.garages[e.garage] = e.working;
    }
    garage_pages_commit_inventory(e, w.garages);
    e.status = garage_status(fe.shop.tables, w.garages, e.garage);
}

// garage_stand_load_loadout: the working loadout onto the stand
void stand_reload(FrontEnd& fe, Ui& ui, AssembleScreen& s) {
    stand_show(ui, fe.shop.stand, loadout_build(fe.shop.tables, fe.world->garages, s.editor.working));
}

void rebuild(FrontEnd& fe, AssembleScreen& s) {
    garage_rebuild_working(fe.shop.tables, fe.world->garages, s.editor);
}

void help_scroll(Ui& ui, AssembleScreen& s, int index) {
    if (index >= 0 && index < 12) {
        text_strip_draw_scroll(ui, s.help[index], 0xfff0f0f0);
    }
}

void open_list(FrontEnd&, Ui& ui, AssembleScreen& s) {
    GarageEditor& e = s.editor;
    e.cursor = e.scroll = e.side = 0;
    s.strip_choice = 1;
    assemble_help_set(ui, s, e.category == 2 ? 1 : e.category == 3 ? 2 : 3);
    s.side_start = task_timer_get(ui);
    assemble_frame_timers(ui, s, 0);
}

// garage_part_card_update's accept: fit or take off the part under the cursor
void card_accept(FrontEnd& fe, Ui& ui, AssembleScreen& s) {
    bool changed = false;
    int dropped = 0;
    const bool ok =
        garage_manual_select(fe.shop.tables, fe.world->garages, s.editor, changed, dropped);
    if (changed) {
        ui_sound(ui, bank_sys, se_garage_part_fit);
        stand_reload(fe, ui, s);
    }
    ui_sound(ui, bank_sys, ok ? se_menu_accept : se_menu_cancel);
    if (ok && dropped > 0) {
        fe.screen.step = step_dropped;
    }
    commit(fe, s);
}

// the list's accept: the card opens on an admissible row
bool list_accept(FrontEnd& fe, Ui& ui, AssembleScreen& s) {
    GarageEditor& e = s.editor;
    const GaragePage& page = e.pages[e.category];
    const int row = e.cursor + e.scroll;
    const bool ok = row < static_cast<int>(page.entries.size()) &&
                    garage_admit(fe.shop.tables, fe.world->garages, e,
                                 page.entries[static_cast<std::size_t>(row)]);
    if (ok) {
        assemble_help_set(ui, s, 4);
        s.card_start = task_timer_get(ui);
        assemble_frame_timers(ui, s, 1);
        fe.screen.step = step_card;
    }
    ui_sound(ui, bank_sys, ok ? se_menu_accept : se_menu_cancel);
    return ok;
}

void grid_step(FrontEnd& fe, Ui& ui, AssembleScreen& s) {
    World& w = *fe.world;
    GarageEditor& e = s.editor;
    // list_cursor_page_move(pad, &column, &row, 2, 12): rows wrap (sound 3), columns (sound 4)
    menu_cursor(ui, s.row, 0, 2, 0x16, 0, 3);
    menu_cursor(ui, s.column, 0, 6, 0x17, 0, 4);
    if (ui.pointer.moved && ui.pointer.visible) {
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 6; ++column) {
                if ((row != s.row || column != s.column) &&
                    pointer_inside(ui.pointer, assemble_cell_rect(ui, s, row, column))) {
                    ui_sound(ui, bank_sys,
                             column != s.column ? se_menu_cursor_horizontal
                                                : se_menu_cursor_vertical);
                    menu_input_mark(ui);
                    s.row = row;
                    s.column = column;
                }
            }
        }
    }
    e.category = assemble_category(s.column, s.row);
    if (w.garages.selected == e.garage && (e.status & garage_part_missing)) {
        w.garages.selected = -1;
    }
    commit(fe, s);
    if (!(e.status & garage_whole)) {
        e.working.name[0] = -1;
    }
    for (bool& on : s.available) {
        on = true;
    }
    if (!(e.status & garage_whole)) {
        s.available[10] = s.available[8] = false;
    }
    if (e.status & garage_empty) {
        s.available[10] = s.available[8] = s.available[9] = false;
    }
    if (e.build_class >= 3 && e.build_class <= 6) {
        s.available[6] = false;
    }
    assemble_frame_draw(fe, ui, s, 1);
    help_scroll(ui, s, e.category);
    const PointerRect cell = assemble_cell_rect(ui, s, s.row, s.column);
    if (menu_accept(ui, &cell)) {
        if (!s.available[e.category]) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            return;
        }
        ui_sound(ui, bank_sys, se_menu_accept);
        open_list(fe, ui, s);
        switch (e.category) {
        case 2:
        case 3:
            fe.screen.step = step_side;
            break;
        case 8:
            fe.screen.step = step_rename;
            break;
        case 9:
            msg_box_open(s.boxes[0]); // retail never reopens it (docs/bugs.md)
            fe.screen.step = step_dismantle;
            break;
        case 10:
            fe.screen.step = step_spec;
            break;
        case 11:
            fe.screen.step = step_exit;
            break;
        default:
            fe.screen.step = step_list;
            break;
        }
    } else if (menu_cancel(ui) && e.category != 11) {
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        s.column = 5;
        s.row = 1;
        e.category = 11;
    }
}

void side_step(FrontEnd& fe, Ui& ui, AssembleScreen& s) {
    GarageEditor& e = s.editor;
    const float centre = e.category == 2 ? 117.5f : 162.5f;
    PointerRect tabs[2] = {sprite_rect(ui, 999, centre + 22.5f, 170.0f),
                           sprite_rect(ui, 997, centre - 22.5f, 170.0f)};
    menu_cursor(ui, e.side, 0, 2, 0x17, 0, 4, tabs);
    assemble_frame_draw(fe, ui, s, 0);
    help_scroll(ui, s, e.side);
    assemble_side_tabs_draw(ui, s);
    icon_on_plate_draw(ui, 0x82, 0x12d, 0x101, 0xc3d, 0x13);
    if (menu_accept(ui, &tabs[e.side])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        assemble_help_set(ui, s, 3);
        assemble_frame_timers(ui, s, 0);
        fe.screen.step = step_list;
    } else if (menu_cancel(ui)) {
        assemble_help_set(ui, s, 0);
        fe.screen.step = step_back;
        ui_sound(ui, bank_sys, se_menu_cancel);
    }
}

void list_step(FrontEnd& fe, Ui& ui, AssembleScreen& s) {
    GarageEditor& e = s.editor;
    GaragePage& page = e.pages[e.category];
    const int count = static_cast<int>(page.entries.size());
    int moved = 0;
    menu_list_move(ui.pad, e.cursor, e.scroll, 0, count, 6, 0x12, moved);
    if (moved) {
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
    }
    // the pointer: hovering a row lights it; a press starts a drag, letting go on the row opens
    // its card, letting go on the stand or the category's cell fits it
    const int visible = count < 6 ? count : 6;
    PointerRect rows[6];
    for (int i = 0; i < 6; ++i) {
        rows[i] = assemble_row_rect(i);
    }
    const int hover = pointer_pick(ui.pointer, rows, visible);
    if (ui.pointer.moved && hover >= 0 && hover != e.cursor && !s.dragging) {
        e.cursor = hover;
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
    }
    if (ui.pointer.left && hover >= 0) {
        s.dragging = true;
        s.drag_row = hover;
        e.cursor = hover;
    }
    assemble_frame_draw(fe, ui, s, 2);
    // garage_page_sort_marks_draw
    if (const UiTable* marks = ui_table(ui.data, "garage", "garage_page_sort_marks_sprites")) {
        std::vector<SpriteSlot> rows_drawn = marks->slots;
        for (int i = 0; i < 6 && i < static_cast<int>(rows_drawn.size()); ++i) {
            rows_drawn[static_cast<std::size_t>(i)].color = 0xff808080;
        }
        if (rows_drawn.size() >= 6) {
            rows_drawn[2].sprite = rows_drawn[5].sprite = 0xc63;
            const int lit = page.column == 0 ? 0 : 3;
            for (int i = lit; i < lit + 3; ++i) {
                rows_drawn[static_cast<std::size_t>(i)].color = 0xffffffff;
            }
            if (!page.descending) {
                rows_drawn[static_cast<std::size_t>(lit + 2)].sprite = 0xc62;
            }
        }
        sprite_rows_draw(ui, rows_drawn.data());
    }
    assemble_list_draw(fe, ui, s);
    help_scroll(ui, s, 0);
    bool drop_fit = false, drop_open = false;
    if (ui.pointer.released && s.dragging) {
        s.dragging = false;
        const PointerRect cell = assemble_cell_rect(ui, s, s.row, s.column);
        if (hover == s.drag_row) {
            drop_open = true;
        } else if (pointer_inside(ui.pointer, cell) || ui.pointer.x >= 340.0f) {
            drop_fit = true;
        }
    }
    if (menu_accept_pressed(ui.pad) || drop_open || drop_fit) {
        if (list_accept(fe, ui, s) && drop_fit) {
            s.card_start = task_timer_get(ui) - 8;
            card_accept(fe, ui, s);
        }
    } else if (menu_cancel(ui)) {
        s.dragging = false;
        if (e.category == 2 || e.category == 3) {
            assemble_help_set(ui, s, e.category == 2 ? 1 : 2);
            s.side_start = task_timer_get(ui);
            fe.screen.step = step_side;
        } else {
            assemble_help_set(ui, s, 0);
            fe.screen.step = step_back;
        }
        ui_sound(ui, bank_sys, se_menu_cancel);
    } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_x)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        garage_page_sort(page, 0, page.column == 1 ? true : !page.descending);
    } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_y)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        garage_page_sort(page, 1, page.column == 0 ? true : !page.descending);
    }
}

// garage_rename_leave, garage_spec_leave, garage_exit_leave: one update, then the fade out
void start_leave(FrontEnd& fe, Ui& ui, AssembleScreen& s, int result) {
    World& w = *fe.world;
    assemble_frame_draw(fe, ui, s, 0);
    if (result == 2) {
        fe.shop.editing = s.editor.garage;
        w.garages.garages[s.editor.garage] = s.editor.working;
    } else if (result == 1) {
        // the spec page's build, its name and the fitted Chip's own name
        fe.shop.spec = loadout_build(fe.shop.tables, w.garages, s.editor.working);
        glyph_name_copy(fe.shop.spec_name, s.editor.working.name);
        const int chip = s.editor.working.chip;
        if (chip >= 0 && chip < inventory_size) {
            glyph_name_copy(fe.shop.spec_chip_name, w.garages.inventory[chip].name);
        } else {
            fe.shop.spec_chip_name[0] = -1;
        }
    }
    s.frames = static_cast<int>(half_second(ui));
    fade_out_start(ui, static_cast<float>(s.frames));
    s.result = result;
    fe.screen.step = step_leave;
}

} // namespace

void assemble_spawn(FrontEnd& fe, Ui&) {
    fe.screen.assemble.from = fe.previous;
}

void assemble_step(FrontEnd& fe, Ui& ui) {
    AssembleScreen& s = fe.screen.assemble;
    GarageEditor& e = s.editor;
    World& w = *fe.world;
    if (fe.screen.step != step_update) {
        // the mouse may grab the machine while no box is up
        const int step = fe.screen.step;
        const bool menus =
            step == step_grid || step == step_side || step == step_list || step == step_card;
        stand_area_set(fe.shop.stand, menus ? PointerRect{340, 96, 640, 330} : PointerRect{});
        stand_update(ui, fe.shop.stand);
    }
    switch (fe.screen.step) {
    case step_update: { // assemble_screen_update
        texture_set_load(ui, common_asset);
        texture_set_load(ui, assemble_asset);
        texture_set_load(ui, module_asset);
        level_meter_spawn(ui, fe.screen.meter);
        garage_pages_build(fe.shop.tables, w.garages, e.pages);
        garage_pages_mark_fitted(w.garages, e.pages);
        s.row = 0;
        s.column = s.from == app_spec_scoobee ? 5 : s.from == app_name_entry ? 4 : 0;
        e.category = assemble_category(s.column, s.row);
        for (bool& on : s.available) {
            on = true;
        }
        e.garage = fe.shop.editing >= 0 && fe.shop.editing < garage_count ? fe.shop.editing : 0;
        e.status = garage_status(fe.shop.tables, w.garages, e.garage);
        e.working = w.garages.garages[e.garage];
        garage_records_check(fe.shop.tables, w.garages, e);
        stand_start(ui, fe.shop.stand, 0);
        stand_reload(fe, ui, s);
        s.banner = -1;
        assemble_grid_reset(s);
        slide_start(ui, s.top);
        slide_start(ui, s.bottom);
        s.side_start = s.card_start = -1;
        assemble_help_set(ui, s, 0);
        s.result = 0;
        s.boxes[0] = msg_box_from(ui, "garage", "garage_strip_confirm_boxes", 0);
        s.boxes[1] = msg_box_from(ui, "garage", "garage_strip_confirm_boxes", 1);
        // state_music_play(3): the town map's song, not state 4's row (read in the bytes)
        state_music_play(fe, ui, 3);
        s.frames = static_cast<int>(half_second(ui));
        fade_in_start(ui, static_cast<float>(s.frames));
        fe.screen.step = step_fade_in;
        break;
    }
    case step_fade_in: // assemble_screen_step2
        menu_input_mark(ui);
        assemble_frame_draw(fe, ui, s, 1);
        help_scroll(ui, s, e.category);
        if (--s.frames < 0) {
            now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
            s.banner = task_timer_get(ui);
            fe.screen.step = step_grid;
        }
        break;
    case step_grid: // assemble_screen_step3
        grid_step(fe, ui, s);
        break;
    case step_side: // garage_weapon_side_update
        side_step(fe, ui, s);
        break;
    case step_list: // garage_part_list_update
        list_step(fe, ui, s);
        break;
    case step_card: { // garage_part_card_update
        assemble_frame_draw(fe, ui, s, 2);
        assemble_card_draw(fe, ui, s);
        help_scroll(ui, s, 0);
        const PointerRect card = {0, 100, 640, 420};
        if (menu_accept(ui, &card)) {
            card_accept(fe, ui, s);
        } else if (menu_cancel(ui)) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            assemble_help_set(ui, s, 3);
            fe.screen.step = step_list;
        }
        break;
    }
    case step_dropped: { // garage_options_dropped_update
        menu_input_mark(ui);
        assemble_frame_draw(fe, ui, s, 2);
        assemble_card_draw(fe, ui, s);
        msg_box_update_ok(ui, s.boxes[1]);
        const PointerRect ok = msg_box_ok_rect(ui, s.boxes[1]);
        if (menu_accept(ui, &ok)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            msg_box_open(s.boxes[1]);
            fe.screen.step = step_card;
        }
        break;
    }
    case step_dismantle: { // garage_strip_confirm_update
        PointerRect answers[2];
        msg_box_choice_rects(ui, s.boxes[0], answers);
        menu_cursor(ui, s.strip_choice, 0, 2, 0x11, 0, 4,
                    s.boxes[0].row.done ? answers : nullptr);
        assemble_frame_draw(fe, ui, s, 2);
        msg_box_update_choice(ui, s.boxes[0], s.strip_choice);
        if (menu_accept(ui, &answers[s.strip_choice])) {
            if (s.strip_choice == 0) {
                loadout_reset(e.working);
                for (GaragePage& page : e.pages) {
                    for (GarageEntry& entry : page.entries) {
                        if (entry.garage == e.garage) {
                            entry.garage = -1;
                        }
                    }
                }
                if (w.garages.selected == e.garage) {
                    w.garages.selected = -1;
                }
                rebuild(fe, s);
                stand_reload(fe, ui, s);
                ui_sound(ui, bank_sys, se_garage_strip);
            } else {
                ui_sound(ui, bank_sys, se_menu_accept);
            }
            assemble_help_set(ui, s, 0);
            fe.screen.step = step_back;
        } else if (menu_cancel(ui)) {
            if (s.strip_choice != 1) {
                ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            }
            s.strip_choice = 1;
        }
        break;
    }
    case step_back: // garage_commit_working_loadout_next4
        assemble_frame_draw(fe, ui, s, 0);
        rebuild(fe, s);
        fe.screen.step = step_grid;
        break;
    case step_rename:
        start_leave(fe, ui, s, 2);
        break;
    case step_spec:
        start_leave(fe, ui, s, 1);
        break;
    case step_exit:
        start_leave(fe, ui, s, 0);
        break;
    case step_leave: // garage_screen_leave
        menu_input_mark(ui);
        assemble_frame_draw(fe, ui, s, 0);
        if (--s.frames < 0) {
            if (w.garages.selected == e.garage && (e.status & garage_part_missing)) {
                w.garages.selected = -1;
            }
            commit(fe, s);
            ui_music_stop(ui);
            task_header_set(fe, s.result);
            fe.screen.step = step_done;
        }
        break;
    default:
        assemble_frame_draw(fe, ui, s, 0);
        break;
    }
}

void assemble_destroy(FrontEnd& fe, Ui& ui) {
    ui_music_stop(ui);
    stand_hide(fe.shop.stand);
    ui.clear_colour = 0xff000000;
}
