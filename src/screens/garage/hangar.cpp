// The hangar's steps (decomp src/o1/u_01c789.c: hangar_screen_update, hangar_fade_in,
// hangar_select_update, hangar_garage_menu_update, the unlock hangar_unlock_confirm_update,
// the boxes hangar_message_*, hangar_screen_leave, hangar_screen_destroy). Answers
// (state_hangar_update): 0 the garage hub, 1 the area select, 2 assemble, 3 the spec page,
// 4 and 6 keep SV, 5 the test drive.
#include "screens/garage/hangar.hpp"

#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/garage/garage_rules.hpp"
#include "world/world.hpp"

namespace {

enum Step { s_setup, s_fade_in, s_select, s_menu, s_unlock, s_box, s_leave };
// hangar_boxes rows: 0 boarded, 1 keep SV's "choose a whole machine", 2 buy this hangar?, 3 thanks,
// 4 no money, 5 already on it, 6 and 7 a VS machine needs weapons
enum Box { b_boarded, b_keep_not_ready, b_unlock, b_unlocked, b_no_money, b_already, b_check,
           b_no_weapon };
constexpr int unlock_price = 10000; // units of 10 NY: 100,000 NY

// D_001DB8E8: five help lists in D_001DB838 (first line, count): the wheel, a whole machine's
// menu, an incomplete one's, the unlock's answers, keep SV's menu
constexpr int list_first[5] = {0, 11, 16, 21, 23};
constexpr int list_count[5] = {11, 5, 5, 2, 3};

// one line of a list into the strip of its row (the wheel's exit
// row says where it goes back to)
// decomp: hangar_help_line_set 0x1D360
void help_line(Ui& ui, HangarScreen& h, int list, int row) {
    std::string text;
    if (list == 0 && row == 10 && h.mode == app_area_select) {
        text = text_string(ui, "str.17dbb8"); // back to the area select
    } else if (list == 0 && row == 10 && h.mode == app_keep_sv) {
        text = text_string(ui, "str.17db28"); // back to VS save
    } else {
        const std::vector<std::string> lines =
            ui_strings(ui.data, "hangar", "hangar_list_lines", list_first[list] + row, 1);
        text = lines.empty() ? std::string() : lines[0];
    }
    help_strip_init(ui, h.help[row], text);
}

// decomp: hangar_help_set 0x1D95E
void help_list(Ui& ui, HangarScreen& h, int list) {
    for (int row = 0; row < list_count[list]; ++row) {
        help_line(ui, h, list, row);
    }
}

// decomp: hangar_status_refresh 0x1D403
void status_refresh(FrontEnd& fe, HangarScreen& h) {
    for (int i = 0; i < garage_count; ++i) {
        h.flags[i] = garage_status(fe.shop.tables, fe.world->garages, i);
    }
    h.flags[10] = 1;
}

// the spec page's machine: garage_resolve_inventory_refs, its name and its Chip's
void spec_fill(FrontEnd& fe, int i) {
    const Garages& g = fe.world->garages;
    const Garage& l = g.garages[i];
    fe.shop.spec = loadout_build(fe.shop.tables, g, l);
    glyph_name_copy(fe.shop.spec_name, l.name);
    if (l.chip >= 0 && l.chip < inventory_size) {
        glyph_name_copy(fe.shop.spec_chip_name, g.inventory[l.chip].name);
    } else {
        fe.shop.spec_chip_name[0] = -1;
    }
}

// the mode kept for the way back, the half-second fade
// decomp: hangar_leave_start 0x1E3F9
void leave_start(FrontEnd& fe, Ui& ui, HangarScreen& h, int next) {
    fe.campaign.hangar_mode = h.mode;
    h.timer = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
    fade_out_start(ui, static_cast<float>(h.timer));
    h.next = next;
    fe.screen.step = s_leave;
}

void box_open(HangarScreen& h, int box, int after, FrontEnd& fe) {
    h.box = box;
    h.box_after = after;
    fe.screen.step = s_box;
}

void card_start(Ui& ui, HangarScreen& h) {
    pop_timer_start(ui, h.card, 1.0f, 1.0f, false, 0, 0);
}

// the wheel's turn toward the new cursor (hangar_wheel_angle)
void turn_start(HangarScreen& h) {
    h.turn = 0;
    h.turn_from = hangar_wheel_angle(h);
}

// the wheel
// decomp: hangar_select_update 0x1E79B
void select_step(FrontEnd& fe, Ui& ui, HangarScreen& h) {
    World& w = *fe.world;
    if (h.turn == 8 * (60 / ui.frame_cadence)) {
        h.prev = h.sel;
        int moved = menu_cursor(ui, h.sel, 0, 11, cursor_menu_horizontal, 0, 3, h.plates);
        if (moved == 0 && ui.pointer.wheel != 0) {
            h.sel = (h.sel + (ui.pointer.wheel > 0 ? 10 : 1)) % 11;
            menu_input_mark(ui);
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            moved = 1;
        }
        if (moved != 0 && h.sel != h.prev) {
            ui_sound(ui, bank_sys, se_hangar_wheel_turn);
            turn_start(h);
        }
    }
    status_refresh(fe, h);
    hangar_screen_draw(fe, ui, h, true);
    text_strip_draw_scroll(ui, h.help[h.sel == 10 ? 10 : 0], 0xfff0f0f0);
    const PointerRect* lit = &h.plates[h.sel];
    if (menu_accept(ui, lit)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        const int sel = h.sel;
        if (sel == 10) {
            if (h.mode == app_area_select) {
                leave_start(fe, ui, h, 1);
            } else if (h.mode == app_keep_sv) {
                fe.shop.editing = 10;
                leave_start(fe, ui, h, 4);
            } else {
                leave_start(fe, ui, h, 0);
            }
        } else if (h.mode == app_keep_sv) {
            // keep SV: a whole machine with weapons
            const int bits = h.flags[sel];
            if (!garage_unlocked(w.garages, sel) || !(bits & garage_whole)) {
                box_open(h, b_keep_not_ready, s_select, fe);
            } else if (bits & garage_no_weapon) {
                box_open(h, b_no_weapon, s_select, fe);
            } else {
                help_list(ui, h, 4);
                h.kind = 0;
                card_start(ui, h);
                fe.screen.step = s_menu;
            }
        } else {
            h.kind = 0;
            if (garage_unlocked(w.garages, sel)) {
                help_list(ui, h, h.flags[sel] & garage_whole ? 1 : 2);
                card_start(ui, h);
                fe.screen.step = s_menu;
            } else {
                help_list(ui, h, 3);
                h.kind = 1;
                fe.screen.step = s_unlock;
            }
        }
    } else if (menu_cancel(ui)) {
        if (h.sel != 10) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            ui_sound(ui, bank_sys, se_hangar_wheel_turn);
        }
        h.prev = h.sel;
        h.sel = 10;
        turn_start(h);
    } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_x) && h.boarded >= 0 &&
               h.boarded < 11) {
        // X: the boarded garage
        h.prev = h.sel;
        h.sel = h.boarded;
        turn_start(h);
    }
}

// the garage's card
// decomp: hangar_garage_menu_update 0x1E453
void menu_step(FrontEnd& fe, Ui& ui, HangarScreen& h) {
    World& w = *fe.world;
    int low = 0, count = 5;
    const bool whole = (h.flags[h.sel] & garage_whole) != 0;
    if (!whole) {
        low = 3; // only Assemble and Close
        count = 2;
    } else if (h.mode == app_keep_sv) {
        count = 3;
    }
    const int old = h.kind;
    const PointerRect* rects = h.mode == app_keep_sv ? h.rows : h.rows + low;
    if (menu_cursor(ui, h.kind, low, count, cursor_menu_vertical, 0, 3, rects)) {
        const int list = h.mode == app_keep_sv ? 4 : whole ? 1 : 2;
        help_line(ui, h, list, list == 2 ? h.kind - low : h.kind);
    }
    status_refresh(fe, h);
    // Test-drive is only the boarded garage's: the cursor steps over it
    if (h.mode != app_keep_sv && !(h.flags[h.sel] & garage_boarded) && h.kind == 1) {
        h.kind = old == 0 ? 2 : 0;
        help_line(ui, h, 1, h.kind);
    }
    hangar_screen_draw(fe, ui, h, false);
    hangar_card_draw(ui, h);
    text_strip_draw_scroll(ui, h.help[h.kind], 0xfff0f0f0);
    const PointerRect* lit = h.kind >= 0 && h.kind < 5 ? &h.rows[h.kind] : nullptr;
    if (menu_accept(ui, lit)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        if (h.mode == app_keep_sv) {
            switch (h.kind) {
            case 0: // this one for VS
                fe.shop.editing = h.sel;
                leave_start(fe, ui, h, 6);
                break;
            case 1: // Specs
                fe.shop.editing = h.sel;
                spec_fill(fe, h.sel);
                leave_start(fe, ui, h, 3);
                break;
            default:
                help_list(ui, h, 0);
                fe.screen.step = s_select;
                break;
            }
            return;
        }
        switch (h.kind) {
        case 0: // Ride: only a machine whose body, legs and Chip are this garage's
            if (garage_parts_owned(w.garages, h.sel)) {
                box_open(h, h.sel != h.boarded ? b_boarded : b_already,
                         h.sel != h.boarded ? s_select : s_menu, fe);
            }
            break;
        case 1: // Test-drive the boarded machine
            if (h.sel == h.boarded) {
                fe.shop.editing = h.sel;
                leave_start(fe, ui, h, 5);
            }
            break;
        case 2: // Specs
            fe.shop.editing = h.sel;
            spec_fill(fe, h.sel);
            leave_start(fe, ui, h, 3);
            break;
        case 3: // Assemble
            fe.shop.editing = h.sel;
            leave_start(fe, ui, h, 2);
            break;
        default: // Close
            help_list(ui, h, 0);
            fe.screen.step = s_select;
            break;
        }
    } else if (menu_cancel(ui)) {
        const int back = h.mode == app_keep_sv ? 2 : 4;
        if (h.kind != back) {
            h.kind = back;
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        }
    }
}

// buy this hangar?
// decomp: hangar_unlock_confirm_update 0x1E686
void unlock_step(FrontEnd& fe, Ui& ui, HangarScreen& h) {
    World& w = *fe.world;
    MsgBox& box = h.boxes[b_unlock];
    PointerRect answers[2];
    msg_box_choice_rects(ui, box, answers);
    if (menu_cursor(ui, h.kind, 0, 2, cursor_menu_horizontal, 0, 4, answers)) {
        help_line(ui, h, 3, h.kind);
    }
    hangar_screen_draw(fe, ui, h, false);
    msg_box_update_choice(ui, box, h.kind);
    text_strip_draw_scroll(ui, h.help[h.kind], 0xfff0f0f0);
    if (menu_accept(ui, &answers[h.kind])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        if (h.kind == 0) {
            if (static_cast<std::uint32_t>(w.story.money) >= static_cast<std::uint32_t>(unlock_price)) {
                ui_sound(ui, bank_sys, se_purchase);
                w.story.money -= unlock_price;
                w.garages.unlocked |= 1u << h.sel;
                box_open(h, b_unlocked, s_select, fe);
            } else {
                box_open(h, b_no_money, s_select, fe);
            }
        } else {
            help_list(ui, h, 0);
            fe.screen.step = s_select;
        }
        msg_box_open(box);
    } else if (menu_cancel(ui) && h.kind != 1) {
        h.kind = 1;
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
    }
}

// the boxes: hangar_message_not_ready (boarding, and keep SV's refusal), _back, _check,
// _no_weapon, hangar_unlock_done_message and hangar_unlock_refused_message
void box_step(FrontEnd& fe, Ui& ui, HangarScreen& h) {
    MsgBox& box = h.boxes[h.box];
    const bool unlock = h.box == b_unlocked || h.box == b_no_money;
    if (unlock) {
        menu_input_mark(ui);
    }
    hangar_screen_draw(fe, ui, h, false);
    if (h.box == b_boarded || h.box == b_already) {
        hangar_card_draw(ui, h);
    }
    if (unlock) {
        text_strip_draw_scroll(ui, h.help[h.kind], 0xfff0f0f0);
    }
    msg_box_update_ok(ui, box);
    const PointerRect ok = msg_box_ok_rect(ui, box);
    if (!menu_accept(ui, &ok)) {
        return;
    }
    ui_sound(ui, bank_sys, se_menu_accept);
    msg_box_open(box);
    if (h.box == b_boarded) {
        // hangar_message_not_ready on a Ride: boarded
        ui_sound(ui, bank_sys, se_hangar_message_close);
        h.boarded = h.sel;
        fe.world->garages.selected = h.sel;
    } else if (h.box == b_keep_not_ready) {
        ui_sound(ui, bank_sys, se_hangar_message_close);
    }
    if (h.box_after == s_select) {
        help_list(ui, h, 0);
    }
    fe.screen.step = h.box_after;
    h.box = -1;
}

} // namespace

void hangar_spawn(FrontEnd& fe, Ui&) {
    fe.screen.campaign.hangar = HangarScreen{};
    fe.screen.campaign.hangar.mode = fe.previous;
}

void hangar_step(FrontEnd& fe, Ui& ui) {
    HangarScreen& h = fe.screen.campaign.hangar;
    World& w = *fe.world;
    // PC: the cursor's machine turned by hand while the wheel is up (the garage menu's rows and
    // boxes sit over the cards in the other steps)
    ModelStand& stand = fe.shop.stand;
    if (fe.screen.step == s_select) {
        stand_turn_input(ui, stand);
    } else {
        stand.user_turn = 0.0f;
        stand.user_held = stand.dragging = false;
    }
    switch (fe.screen.step) {
    case s_setup: { // hangar_screen_update
        texture_set_load(ui, 0x690);
        texture_set_load(ui, 0x676);
        level_meter_spawn(ui, fe.screen.meter);
        h.banner = -1;
        h.turn = 8 * (60 / ui.frame_cadence);
        h.turn_from = 0.0f;
        garages_tidy(fe.shop.tables, fe.world->garages);
        h.boarded = w.garages.selected;
        stand_start(ui, fe.shop.stand, 6);
        stand_area_set(fe.shop.stand, {40, 70, 330, 200}); // the cursor's card
        pop_timer_init(h.card, 0x5fc, 4, 8);
        status_refresh(fe, h);
        for (int i = 0; i < garage_count; ++i) {
            h.values[i] = garage_common_class(fe.shop.tables, w.garages, w.garages.garages[i]);
        }
        for (int b = 0; b < 8; ++b) {
            h.boxes[b] = msg_box_from(ui, "hangar", "hangar_boxes", b);
        }
        const int back = fe.campaign.hangar_mode;
        const int editing = fe.shop.editing >= 0 && fe.shop.editing < 11 ? fe.shop.editing : 0;
        switch (h.mode) {
        case app_assemble: // back from assemble: on its garage's card, on Assemble
            h.mode = back;
            h.sel = editing;
            h.kind = 3;
            help_list(ui, h, h.flags[h.sel] & garage_whole ? 1 : 2);
            card_start(ui, h);
            break;
        case app_spec_scoobee: // back from the spec page, on Specs
            h.mode = back;
            h.sel = editing;
            if (h.mode == app_keep_sv) {
                h.kind = 1;
                help_list(ui, h, 4);
            } else {
                h.kind = 2;
                help_list(ui, h, 1);
            }
            card_start(ui, h);
            break;
        case app_keep_sv: // keep SV: picking a machine for VS
            fe.shop.editing = -1;
            h.kind = 0;
            help_list(ui, h, 0);
            card_start(ui, h);
            break;
        case app_test_drive: // back from the test drive, on Test-drive
            h.mode = back;
            h.sel = editing;
            h.kind = 1;
            help_list(ui, h, 1);
            card_start(ui, h);
            break;
        default: // the hub or the area select: on garage 0
            h.sel = 0;
            h.kind = 0;
            help_list(ui, h, 0);
            break;
        }
        if (h.sel > 10) {
            h.sel = 10;
        }
        const int starts[10] = {-745, -673, -601, -529, -457, -385, -313, -241, -169, -97};
        slide_init(h.slide, 10, starts);
        slide_start(ui, h.slide);
        state_music_play(fe, ui, 0xe);
        h.timer = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
        fade_in_start(ui, static_cast<float>(h.timer));
        fe.screen.step = s_fade_in;
        break;
    }
    case s_fade_in: // hangar_fade_in
        menu_input_mark(ui);
        hangar_screen_draw(fe, ui, h, false);
        if (h.kind != 0) {
            hangar_card_draw(ui, h);
        }
        if (--h.timer < 0) {
            now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
            h.banner = task_timer_get(ui);
            fe.screen.step = h.kind == 0 ? s_select : s_menu;
        }
        break;
    case s_select:
        select_step(fe, ui, h);
        break;
    case s_menu:
        menu_step(fe, ui, h);
        break;
    case s_unlock:
        unlock_step(fe, ui, h);
        break;
    case s_box:
        box_step(fe, ui, h);
        break;
    case s_leave: // hangar_screen_leave
        menu_input_mark(ui);
        if (h.next == 2 || h.next == 3 || h.next == 5 || h.next == 6) {
            hangar_card_draw(ui, h);
        }
        hangar_screen_draw(fe, ui, h, false);
        if (--h.timer < 0) {
            fe.screen.strip.active = false;
            ui_music_stop(ui);
            task_header_set(fe, h.next);
            fe.screen.step = s_leave + 1;
        }
        break;
    default:
        hangar_screen_draw(fe, ui, h, false);
        break;
    }
    if (fe.screen.step != s_setup) {
        // model_viewer_actor_update: the turning machine; PC: the cursor's turned by hand
        // (stand_turn_input, at the step's start), its turn waiting meanwhile
        for (int i = 0; i < garage_count; ++i) {
            h.heading[i] += stand.user_held ? 0.0f : h.spin[i] * 3.14159265f / 180.0f;
        }
        if (h.sel >= 0 && h.sel < garage_count) {
            h.heading[h.sel] += stand.user_turn;
        }
        ++fe.shop.stand.ticks;
    }
}

void hangar_destroy(FrontEnd& fe, Ui& ui) {
    // hangar_screen_destroy: the boarded garage kept (the battle reads it from there)
    HangarScreen& h = fe.screen.campaign.hangar;
    fe.world->garages.selected = h.boarded;
    stand_hide(fe.shop.stand);
    ui_music_stop(ui);
    ui.clear_colour = 0xff000000;
}
