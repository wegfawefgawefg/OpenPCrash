// VS setup's chain and input (decomp docs/port/screens.md "VS setup (0x2C)": step3, step5, step9,
// step11, step6, vs_entry_intro, vs_entry_update, step7, step10, step8, vs_area_select_leave),
// and state 6 with vs_mode, whose round is the battle session's (src/battle_vs.cpp).
#include "screens/vs/vs_setup.hpp"

#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "input/seats.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/ui/screen_header.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

// the screen's sheets (vs_mode2_tex, area_select_tex, common) and its music (5GARAGE.WAV, played
// directly)
constexpr int vs_assets[3] = {0x68c, 0x667, 0x690};
constexpr int vs_track = 0xc;

float half_second(const Ui& ui) {
    return static_cast<float>(ui.frame_cadence) * 0.5f;
}

int entrants(const VsSession& vs) {
    return static_cast<int>(std::count(vs.entered.begin(), vs.entered.end(), true));
}

// the banner box of an intro (rows 0 and the banner) growing in 15, held, shrinking 75..90
void intro_box(Ui& ui, int banner, int clock) {
    float s = 1;
    if (clock < 15) {
        s = static_cast<float>(clock) / 15.0f;
    } else if (clock >= 75) {
        s = std::max(1.0f - static_cast<float>(clock - 75) / 15.0f, 0.0f);
    }
    vs_row_draw(ui, 0, s, s);
    vs_row_draw(ui, banner, s, s);
}

// a row of vs_sprites_2 as the pointer's hit box
PointerRect row_rect(Ui& ui, int row) {
    const SpriteSlot* slot = vs_row(ui, row);
    return slot ? sprite_rect(ui, slot->sprite, slot->x, slot->y, (slot->flags & 8) != 0)
                : PointerRect{};
}

// step11: who plays. A device nobody holds joins with accept (the first free panel); a player's
// cancel leaves (so the device can come back on another); start needs two or more
void join_step(FrontEnd& fe, Ui& ui) {
    VsSession& vs = fe.vs;
    Seats& seats = *fe.seats;
    vs_page_draw(fe, ui, 0);
    vs.clock += 1;
    switch (vs.sub) {
    case 1: {
        // a pulled pad drops its player
        for (int i = 0; i < vs_panel_count; ++i) {
            if (seats.seats[static_cast<std::size_t>(i)].used && !seat_connected(seats, i)) {
                seat_leave(seats, i);
                vs.entered[static_cast<std::size_t>(i)] = false;
            }
        }
        bool start = false, back = false;
        // the mouse's buttons are the keyboard's accept and cancel while nobody holds it
        if (vs_mouse_seat(fe) < 0 && (ui.pointer.left || ui.pointer.right)) {
            seats.presses.push_back({SeatDevice{true, -1}, ui.pointer.left, !ui.pointer.left});
        }
        for (const SeatPress& press : seats.presses) {
            if (seat_of(seats, press.device) >= 0) {
                continue; // a player's own pad below
            }
            if (press.accept) {
                const int seat = seat_join(seats, press.device);
                std::printf("vs: %s joins as seat %d (update %d)\n",
                            seat_device_label(seats, press.device).c_str(), seat + 1, fe.updates);
                if (seat >= 0) {
                    vs.entered[static_cast<std::size_t>(seat)] = true;
                    vs.panels[static_cast<std::size_t>(seat)].wait_release = true;
                    vs.panels[static_cast<std::size_t>(seat)].joined_update = fe.updates;
                    ui_sound(ui, bank_sys, se_menu_accept);
                } else {
                    ui_sound(ui, bank_sys, se_menu_cancel);
                }
            } else if (press.cancel) {
                back = true;
            }
        }
        for (int i = 0; i < vs_panel_count; ++i) {
            const PadRecord* pad = vs_seat_pad(fe, ui, i);
            if (!pad) {
                continue;
            }
            if (vs_accept(fe, ui, i, *pad)) {
                start = true;
            } else if (vs_cancel(fe, ui, i, *pad)) {
                seat_leave(seats, i);
                vs.entered[static_cast<std::size_t>(i)] = false;
                ui_sound(ui, bank_sys, se_menu_cancel);
            }
        }
        // each panel's list or hint by its device, the "I'm in" marks with two or more in
        const bool marks = entrants(vs) >= 2;
        for (int i = 0; i < vs_panel_count; ++i) {
            VsPanel& p = vs.panels[static_cast<std::size_t>(i)];
            if (p.anim[vs_frame] == 2) {
                vs_panel_idle(fe, 0, i);
            }
            vs_panel_anim(p, vs_title, marks && vs.entered[static_cast<std::size_t>(i)] ? 1 : 3);
        }
        if (start && entrants(vs) >= 2) {
            ui_sound(ui, bank_sys, se_menu_accept);
            vs.sub = 2;
            vs.clock = 0;
            vs.choice = 0;
        } else if (start) {
            ui_sound(ui, bank_sys, se_menu_cancel);
        } else if (back) {
            fade_out_start(ui, 34.0f);
            ui_sound(ui, bank_sys, se_menu_cancel);
            vs.sub = 4;
            vs.clock = 0;
        }
        break;
    }
    case 2: { // the start confirm
        vs_confirm_draw(fe, ui, std::min(static_cast<float>(vs.clock) / 10.0f, 1.0f));
        const int answer = vs.clock >= 10 ? vs_confirm_input(fe, ui) : -1;
        if (answer >= 0) {
            vs.sub = answer == 1 ? 5 : 3;
            vs.clock = 0;
        }
        break;
    }
    case 3: // no: everyone presses accept again
        vs_confirm_draw(fe, ui, std::max(1.0f - static_cast<float>(vs.clock) / 10.0f, 0.0f));
        if (vs.clock >= 10) {
            seats_split(seats);
            vs.entered.fill(false);
            vs.sub = 1;
        }
        break;
    case 4: // back out to the title
        vs_panels_close_all(vs, vs.clock);
        if (vs.clock >= 62 && fade_is_black(ui)) {
            task_header_set(fe, 0);
            vs.sub = 6;
        }
        break;
    case 5: // yes: on to the machines
        vs_confirm_draw(fe, ui, std::max(1.0f - static_cast<float>(vs.clock) / 10.0f, 0.0f));
        vs_panels_close_contents(vs, vs.clock);
        if (vs.clock >= 46) {
            vs_go(fe, vs_step_machine_reset);
        }
        break;
    default: // a 30-frame wait, no input
        if (vs.sub == 0 && vs.clock >= 30) {
            vs.sub = 1;
        }
        break;
    }
    vs_panels_draw(fe, ui, 0);
}

} // namespace

// the start question (rows 0, 2) and its yes / no (rows 4 / 6, lit 5 / 7)
void vs_confirm_draw(FrontEnd& fe, Ui& ui, float s) {
    vs_row_draw(ui, 0, s, s);
    vs_row_draw(ui, 2, s, s);
    if (s >= 1) {
        vs_row_draw(ui, 4);
        vs_row_draw(ui, 6);
        vs_row_draw(ui, fe.vs.choice == 0 ? 5 : 7);
    }
}

void vs_page_draw(FrontEnd& fe, Ui& ui, int page) {
    sprite_rows_draw(ui, ui_slots(ui.data, "vs_mode", "vs_sprites"));
    vs_row_draw(ui, page == 0 ? 118 : 119);
    text_strip_draw_scroll(ui, fe.vs.help[page], 0xFFF0F0F0);
}

// the start confirm's input, from any entered pad: 1 yes, 0 no, -1 still asking; the mouse
// hovers yes (row 4) or no (row 6) and clicks one
int vs_confirm_input(FrontEnd& fe, Ui& ui) {
    const PointerRect answers[2] = {row_rect(ui, 4), row_rect(ui, 6)};
    for (int i = 0; i < vs_panel_count; ++i) {
        const PadRecord* pad = vs_seat_pad(fe, ui, i);
        if (!pad) {
            continue;
        }
        const int hover = vs_hover(fe, ui, i, answers, 2);
        if (menu_cursor_move(*pad, fe.vs.choice, 0, 2, cursor_menu_horizontal) != 0 ||
            (hover >= 0 && hover != fe.vs.choice)) {
            fe.vs.choice = hover >= 0 ? hover : fe.vs.choice;
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        if (vs_accept(fe, ui, i, *pad, i == vs_mouse_seat(fe) ? &answers[fe.vs.choice] : nullptr)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            return fe.vs.choice == 0 ? 1 : 0;
        }
        if (vs_cancel(fe, ui, i, *pad)) {
            fe.vs.choice = 1;
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
    }
    return -1;
}

// an entered seat's pad, or none while it is still holding the press it joined with
const PadRecord* vs_seat_pad(FrontEnd& fe, Ui& ui, int i) {
    VsPanel& p = fe.vs.panels[static_cast<std::size_t>(i)];
    const PadRecord& pad = ui.pads[static_cast<std::size_t>(i)];
    if (!fe.vs.entered[static_cast<std::size_t>(i)]) {
        return nullptr;
    }
    if (p.wait_release) {
        if (fe.updates > p.joined_update + 1 && pad.b.cur == 0 && pad.stick[0].x == 0 && pad.stick[0].y == 0) {
            p.wait_release = false;
        }
        return nullptr;
    }
    return &pad;
}

int vs_mouse_seat(const FrontEnd& fe) {
    return fe.seats ? mouse_seat(*fe.seats) : -1;
}

bool vs_accept(const FrontEnd& fe, const Ui& ui, int seat, const PadRecord& pad,
               const PointerRect* rect) {
    return menu_accept_pressed(pad) || (seat == vs_mouse_seat(fe) && ui.pointer.left &&
                                        (!rect || pointer_inside(ui.pointer, *rect)));
}

bool vs_cancel(const FrontEnd& fe, const Ui& ui, int seat, const PadRecord& pad) {
    return menu_cancel_pressed(pad) || (seat == vs_mouse_seat(fe) && ui.pointer.right);
}

int vs_hover(const FrontEnd& fe, const Ui& ui, int seat, const PointerRect* rects, int count) {
    return seat == vs_mouse_seat(fe) ? pointer_hover(ui, rects, count) : -1;
}

void vs_go(FrontEnd& fe, int step) {
    fe.vs.step = step;
    fe.vs.sub = step == vs_step_join || step == vs_step_machine ? 1 : 0;
    fe.vs.clock = 0;
}

bool vs_screens_enter(FrontEnd& fe, Ui& ui, int state) {
    if (state == app_vs_area_select) {
        fe.vs.step = vs_step_load;
        fe.vs.sub = 0;
        fe.vs.clock = 0;
        return true;
    }
    (void)ui;
    return false;
}

bool vs_screens_step(FrontEnd& fe, Ui& ui) {
    if (fe.state != app_vs_area_select) {
        return false;
    }
    VsSession& vs = fe.vs;
    switch (vs.step) {
    case vs_step_load: {
        // vs_area_select_screen_update: the sheets, the meter, the music and its strip, the
        // three help lines; then step3: from a battle back to its page, else a fresh join page
        for (int asset : vs_assets) {
            texture_set_load(ui, asset);
        }
        level_meter_spawn(ui, fe.screen.meter);
        ui_music(ui, vs_track, true);
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
        const std::vector<std::string> lines = ui_strings(ui.data, "vs_mode", "vs_help_lines", 0, 3);
        for (int k = 0; k < 3; ++k) {
            help_strip_init(ui, vs.help[k], lines[static_cast<std::size_t>(k)]);
        }
        fade_in_start(ui, half_second(ui));
        for (VsPanel& p : vs.panels) {
            p = VsPanel{};
        }
        vs_go(fe, vs.return_page == 1   ? vs_step_machine_reset
               : vs.return_page == 2 ? vs_step_area_reset
                                     : vs_step_join_reset);
        vs.return_page = -1;
        break;
    }
    case vs_step_join_reset:
        // a fresh join page: the records zeroed, nobody in
        fe.world->vs.wins.fill(0);
        fe.world->vs.losses.fill(0);
        vs.entered.fill(false);
        seats_split(*fe.seats);
        for (VsPanel& p : vs.panels) {
            p = VsPanel{};
        }
        vs_go(fe, vs_step_join_intro);
        [[fallthrough]];
    case vs_step_join_intro:
        vs_page_draw(fe, ui, 0);
        intro_box(ui, 1, vs.clock);
        vs_panels_stagger(fe, 0, vs.clock);
        vs_panels_draw(fe, ui, 0);
        if (++vs.clock >= 90) {
            vs_go(fe, vs_step_join);
            vs.sub = 0;
        }
        break;
    case vs_step_join:
        join_step(fe, ui);
        break;
    case vs_step_machine_reset:
        vs_machines_reset(fe);
        if (fade_is_black(ui)) {
            fade_in_start(ui, half_second(ui));
        }
        vs_go(fe, vs_step_machine_intro);
        [[fallthrough]];
    case vs_step_machine_intro:
        vs_page_draw(fe, ui, 1);
        intro_box(ui, 3, vs.clock);
        vs_panels_stagger(fe, 1, vs.clock);
        vs_panels_draw(fe, ui, 1);
        if (++vs.clock >= 90) {
            vs_go(fe, vs_step_machine);
        }
        break;
    case vs_step_machine:
        vs_machine_step(fe, ui);
        break;
    case vs_step_area_reset:
        vs.area = vs.area_shown = entrants(vs) > 2 ? 3 : 0;
        vs.area_phase = 0;
        vs.area_choice = vs.was_choice = 0;
        vs.plate_start = -1;
        vs_labels_start(fe, ui);
        fade_in_start(ui, half_second(ui));
        vs_go(fe, vs_step_area);
        [[fallthrough]];
    case vs_step_area:
        vs_area_step(fe, ui);
        break;
    case vs_step_area_leave: // Exit: through the fade back to the machines
        vs_area_draw(fe, ui);
        if (fade_is_black(ui)) {
            vs_go(fe, vs_step_machine_reset);
        }
        break;
    case vs_step_start: // step8: the area page until black, then state 6
        vs_area_draw(fe, ui);
        if (fade_is_black(ui)) {
            fe.vs_mode = true;
            task_header_set(fe, 1);
            vs.step = -1;
        }
        break;
    default:
        vs_area_draw(fe, ui);
        break;
    }
    vs_panels_tick(vs);
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
    return true;
}

bool vs_screens_destroy(FrontEnd& fe, Ui& ui) {
    if (fe.state == app_vs_area_select) {
        ui_music_stop(ui);
        return true;
    }
    return false;
}

bool vs_screens_next(FrontEnd& fe, int r, int& next) {
    if (fe.state != app_vs_area_select) {
        return false;
    }
    // state_vs_area_select_update: 0 the title, 1 the battle
    if (r == 1) {
        next = app_battle;
    } else {
        next = app_title_return;
        fe.vs_mode = false;
        seats_alone(*fe.seats);
    }
    return true;
}

