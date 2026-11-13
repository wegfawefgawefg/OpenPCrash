// The bug reports' panel (report_panel.hpp): what opens it, its input and what it sends. The
// drawing is report_panel_draw.cpp.
#include "screens/pc/report_panel.hpp"

#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "report/report_fight.hpp"
#include "report/reports.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/pc_settings_screen.hpp"
#include "screens/pc/report_panel_layout.hpp"

#include <SDL3/SDL.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {

int env_update(const char* name) {
    const char* text = std::getenv(name);
    return text ? std::atoi(text) : -1;
}

void close(FrontEnd& fe) {
    ReportPanel& p = fe.report;
    std::error_code error;
    std::filesystem::remove(report_shot_path(), error); // a picture not sent
    if (p.typing && fe.pc && fe.pc->window) {
        SDL_StopTextInput(fe.pc->window);
    }
    p.typing = false;
    p.mode = 0;
}

void say(ReportPanel& p, const std::string& what) {
    p.said = what;
    p.said_ticks = 120;
}

// the note, as the letters allow: upper case, the strip's signs, a line's worth or three
void type(FrontEnd& fe, const char* text) {
    ReportPanel& p = fe.report;
    for (const char* at = text; *at; ++at) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(*at)));
        if (report_letter_allowed(c) && p.note.size() < report_note_most) {
            p.note += c;
        }
    }
}

void send(FrontEnd& fe, Ui& ui) {
    ReportPanel& p = fe.report;
    const bool picture = p.picture_offered && p.picture && p.shot == 2;
    const std::string why = report_send_manual(p.note, picture ? report_shot_path() : std::string(),
                                               report_fight_bytes(fe.world));
    ui_sound(ui, bank_sys, why.empty() ? se_menu_accept : se_menu_cancel);
    say(p, why.empty() ? "SENT. THANK YOU." : why);
    if (why.empty()) {
        p.note.clear();
    }
}

void question_input(FrontEnd& fe, Ui& ui) {
    ReportPanel& p = fe.report;
    const PointerRect rects[2] = {report_question_button(0), report_question_button(1)};
    menu_cursor(ui, p.button, 0, 2, cursor_menu_horizontal, bank_sys, se_menu_cursor_horizontal,
                rects);
    if (menu_accept(ui, &rects[p.button]) || menu_cancel_pressed(ui.pad)) {
        const bool yes = p.button == 0 && !menu_cancel_pressed(ui.pad);
        ui_sound(ui, bank_sys, se_menu_accept);
        report_consent_set(yes);
        close(fe);
    }
}

void note_input(FrontEnd& fe, Ui& ui) {
    ReportPanel& p = fe.report;
    if (p.said_ticks > 0) {
        // what happened, a moment, then the panel goes once it went
        if (--p.said_ticks == 0 && p.note.empty()) {
            close(fe);
        }
        return;
    }
    const int rows = 3;
    const int before = p.row;
    menu_cursor_move(ui.pad, p.row, 0, rows, cursor_value_vertical | cursor_left_stick);
    if (!p.picture_offered && p.row == 1) {
        p.row = before < 1 ? 2 : 0;
    }
    if (p.row != before) {
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
    }
    // the pointer: the buttons, the picture's row, the note's box
    for (int b = 0; b < 2; ++b) {
        if (pointer_inside(ui.pointer, report_note_button(b)) && ui.pointer.moved) {
            p.row = 2, p.button = b;
        }
    }
    const bool b_pressed = pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_b);
    if (b_pressed || ui.pointer.right) {
        ui_sound(ui, bank_sys, se_menu_cancel);
        close(fe);
        return;
    }
    if (menu_start_pressed(ui.pad)) {
        p.row = 2, p.button = 0; // START goes to SEND
    }
    switch (p.row) {
    case 0: {
        // the strip: left and right pick, A types it, X takes the last one back
        const int moved = menu_cursor_move(ui.pad, p.letter, 0, report_strip_size(), cursor_ring);
        if (moved != 0) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        for (int i = 0; i < report_strip_size(); ++i) {
            if (ui.pointer.left && pointer_inside(ui.pointer, report_strip_rect(i))) {
                p.letter = i;
                const char c[2] = {report_strip_letter(i), 0};
                type(fe, c);
            }
        }
        if (pad_button_test(ui.pad, PadEdge::Repeat, PadMatch::Any, pad_a)) {
            const char c[2] = {report_strip_letter(p.letter), 0};
            type(fe, c);
        }
        if (pad_button_test(ui.pad, PadEdge::Repeat, PadMatch::Any, pad_x) && !p.note.empty()) {
            p.note.pop_back();
            ui_sound(ui, bank_sys, se_name_entry_backspace);
        }
        break;
    }
    case 1: {
        int side = p.picture ? 0 : 1; // left YES, right NO
        if (menu_cursor_move(ui.pad, side, 0, 2, cursor_menu_horizontal) != 0 ||
            menu_accept_pressed(ui.pad) ||
            (ui.pointer.left && pointer_inside(ui.pointer, report_picture_rect()))) {
            p.picture = !p.picture;
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        break;
    }
    default:
        if (menu_cursor_move(ui.pad, p.button, 0, 2, cursor_menu_horizontal) != 0) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        const PointerRect chosen = report_note_button(p.button);
        if (menu_accept(ui, &chosen)) {
            if (p.button == 0) {
                send(fe, ui);
            } else {
                ui_sound(ui, bank_sys, se_menu_cancel);
                close(fe);
            }
        }
        break;
    }
    if (ui.pointer.left && pointer_inside(ui.pointer, report_picture_rect()) && p.row != 1 &&
        p.picture_offered) {
        p.row = 1;
        p.picture = !p.picture;
    }
}

} // namespace

void report_panel_open(FrontEnd& fe, bool picture) {
    ReportPanel& p = fe.report;
    if (!reports_available()) {
        return;
    }
    p.mode = 2;
    p.row = 0;
    p.button = 0;
    p.said.clear();
    p.said_ticks = 0;
    p.picture_offered = picture;
    p.picture = true;
    p.shot = picture ? 1 : 0;
    if (fe.pc && fe.pc->window && SDL_StartTextInput(fe.pc->window)) {
        p.typing = true;
    }
    if (report_consent() != ReportConsent::yes) {
        say(p, "BUG REPORTS ARE OFF. TURN THEM ON IN PC CONFIG, GAME.");
    }
}

void report_panel_frame(FrontEnd& fe, Ui& ui) {
    ReportPanel& p = fe.report;
    report_note_game(fe.state, fe.campaign.battle_running,
                     fe.world ? pc_cheats_text(fe.world->story.cheats) : "retail");
    // the headless checks' own
    static const int dev_report = env_update("PHANTOMCRASH_DEV_REPORT");
    static const int dev_open = env_update("PHANTOMCRASH_DEV_REPORT_OPEN");
    static const int dev_crash = env_update("PHANTOMCRASH_DEV_CRASH");
    if (fe.updates == dev_report) {
        const std::string why = report_send_manual("DEV CHECK", "", report_fight_bytes(fe.world));
        std::printf("report: dev report at update %d: %s\n", fe.updates,
                    why.empty() ? "queued" : why.c_str());
    }
    if (fe.updates == dev_open) {
        report_panel_open(fe, fe.campaign.paused);
    }
    if (fe.updates == dev_crash) {
        report_crash_on_purpose();
    }
    // the question, once, over the title
    if (p.mode == 0 && !p.asked && fe.state == app_title && fade_is_idle(ui) &&
        reports_question_due()) {
        p.asked = true;
        p.mode = 1;
        p.button = 0;
    }
    if (p.mode == 0) {
        return;
    }
    // the picture: this update's frame is drawn with nothing over the paused fight, and kept
    if (p.shot == 1 || p.shot == 3) {
        p.shot = p.shot == 1 ? 3 : 0; // 3 still here: it was never taken
        if (p.shot == 3) {
            ui.pad = PadRecord{};
            ui.pointer.left = ui.pointer.right = false;
            return;
        }
    }
    if (p.mode == 1) {
        question_input(fe, ui);
    } else {
        note_input(fe, ui);
    }
    // the screen under it reads nothing
    ui.pad = PadRecord{};
    ui.pointer.left = ui.pointer.right = false;
    ui.pointer.wheel = 0;
}

bool report_panel_event(FrontEnd& fe, Ui&, const SDL_Event& event) {
    ReportPanel& p = fe.report;
    if (p.mode == 0) {
        return false;
    }
    if (p.mode == 2 && event.type == SDL_EVENT_TEXT_INPUT && p.said_ticks == 0) {
        type(fe, event.text.text);
        return true;
    }
    if (event.type != SDL_EVENT_KEY_DOWN) {
        return false;
    }
    // the question takes the keys as the pad (Enter, Escape, the arrows) through the bindings
    if (p.mode == 1) {
        return false;
    }
    switch (event.key.key) {
    case SDLK_BACKSPACE:
        if (!p.note.empty()) {
            p.note.pop_back();
        }
        return true;
    case SDLK_ESCAPE:
        close(fe);
        return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        p.row = 2;
        p.button = 0;
        return false; // A, through the bindings: SEND
    case SDLK_UP:
    case SDLK_DOWN:
    case SDLK_LEFT:
    case SDLK_RIGHT:
        return false; // the d-pad, through the bindings
    default:
        // a letter typed is the note's (its text event follows), never a pad button
        return true;
    }
}

std::string report_panel_shot(FrontEnd& fe) {
    ReportPanel& p = fe.report;
    if (p.mode != 2 || p.shot != 3) {
        return {};
    }
    p.shot = 2;
    return report_shot_path();
}

bool report_panel_hides_pause(const FrontEnd& fe) {
    return fe.report.mode == 2 && fe.report.shot == 3;
}
