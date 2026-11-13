// The pause's settings (pause_settings.hpp): the PC's items under a fight's pause, PC CONFIG's
// pages opened over the paused fight, and their OPTIONS page.
#include "screens/pc/pause_settings.hpp"

#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "report/reports.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/music_panel.hpp"

#include <algorithm>
#include <string>

namespace {

// the items: option buttons down the pause's middle, under the menu's plate from where REPORT A
// BUG sat alone, else from just under PAUSE two a row (the entrance's letterbox comes up to 384)
constexpr float items_x = 224, items_y_menu = 300, items_y = 258, items_pitch = 42;
constexpr float grid_x = 122, grid_pitch = 204;
constexpr const char* item_words[] = {"OPTIONS", "PC CONFIG", "MUSIC", "REPORT A BUG"};

// OPTIONS' rows
enum OptionRow {
    option_vibration,
    option_bgm,
    option_se,
    option_shift_x,
    option_shift_y,
    option_buttons,
    option_rows
};

struct OptionText {
    const char* label;
    const char* help;
};

constexpr OptionText option_text[option_rows] = {
    {"VIBRATION", "The pad shakes when hit. PC CONFIG's INPUT sets how hard."},
    {"BGM VOLUME", "The original's music volume, kept with the game's save."},
    {"SE VOLUME", "The original's sound effects volume, kept with the game's save."},
    {"SCREEN LEFT RIGHT", "The original's screen adjust: the picture moved across the screen."},
    {"SCREEN UP DOWN", "The original's screen adjust: the picture moved up or down."},
    {"BUTTON CONFIG", "Not during a fight: the buttons are part of the fight's record. Change them "
                      "in OPTION outside a fight."},
};

// the volumes go by 15, so the defaults (255, 225: sound_volumes_default) are steps
constexpr int volume_step = 15;

// sprites the panel draws go by its own anchors and over the pause (ui.hpp, Ui::panel)
void panel_begin(Ui& ui) {
    ui.panel = true;
    ui.panel_anchors = ui_anchor_screen(ui.anchors, app_pc_config);
}

void open_panel(FrontEnd& fe, Ui& ui, int page) {
    if (!fe.pc) {
        return;
    }
    fe.screen.pc = PcSettingsScreen{};
    fe.screen.pc.paused = true;
    fe.screen.pc.page = page;
    fe.pause_settings = true;
    pc_art_load(fe.pc_art, ui);
    ui.panel_anchors = ui_anchor_screen(ui.anchors, app_pc_config);
    menu_input_mark(ui);
}

} // namespace

int pause_items_count() {
    return reports_available() ? 4 : 3;
}

PointerRect pause_item_rect(int item, bool menu_of_its_own) {
    if (!menu_of_its_own) {
        return pc_button_rect(grid_x + grid_pitch * static_cast<float>(item % 2),
                              items_y + items_pitch * static_cast<float>(item / 2));
    }
    return pc_button_rect(items_x, items_y_menu + items_pitch * static_cast<float>(item));
}

namespace {

// a tournament's pause's cursor on its two-a-row items: up and down a row, left and right in it,
// the pointer as menu_cursor's
void grid_cursor(Ui& ui, int& cursor, int count, const PointerRect* rects) {
    int row = cursor / 2, col = cursor % 2;
    const int rows = (count + 1) / 2;
    const int down = menu_cursor_move(ui.pad, row, 0, rows, cursor_value_vertical);
    const int across = menu_cursor_move(ui.pad, col, 0, 2, cursor_value_horizontal);
    const int next = std::min(row * 2 + col, count - 1);
    if ((down != 0 || across != 0) && next != cursor) {
        cursor = next;
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
        return;
    }
    menu_cursor(ui, cursor, 0, count, cursor_no_dpad, bank_sys, se_menu_cursor_vertical, rects);
}

} // namespace

void pause_items(FrontEnd& fe, Ui& ui, bool menu_of_its_own) {
    if (fe.report.mode != 0 || fe.pause_settings) {
        return;
    }
    Campaign& c = fe.campaign;
    const int count = pause_items_count();
    PointerRect rects[pause_items_max];
    for (int i = 0; i < count; ++i) {
        rects[i] = pause_item_rect(i, menu_of_its_own);
    }
    // a tournament's pause: its own cursor over the items (the sound of free play's menu)
    if (!menu_of_its_own) {
        grid_cursor(ui, c.pause_cursor, count, rects);
        if (pause_item_accepted(ui, rects[c.pause_cursor])) {
            pause_item_open(fe, ui, c.pause_cursor);
            return;
        }
    }
    if (menu_back_pressed(ui.pad) && reports_available()) {
        pause_item_open(fe, ui, pause_item_report);
        return;
    }
    // drawn by the panel's anchors (the HUD's would pull a button's ends out to the sides)
    pc_art_load(fe.pc_art, ui);
    panel_begin(ui);
    const int lit = menu_of_its_own ? c.pause_cursor - 2 : c.pause_cursor;
    for (int i = 0; i < count; ++i) {
        const PointerRect& r = rects[i];
        pc_button_draw(ui, fe.pc_art, item_words[i], r.left, r.top, i == lit, 0x1003,
                       i == lit ? 0x2000 : 0);
    }
    ui.panel = false;
}

bool pause_item_accepted(const Ui& ui, const PointerRect& rect) {
    return pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_a) ||
           (ui.pointer.left && pointer_inside(ui.pointer, rect));
}

void pause_item_open(FrontEnd& fe, Ui& ui, int item) {
    ui_sound(ui, bank_sys, se_menu_accept);
    if (item == pause_item_report) {
        report_panel_open(fe, true);
    } else if (item == pause_item_music) {
        open_panel(fe, ui, page_music);
        if (fe.pause_settings) {
            music_panel_open(fe, ui, false);
        }
    } else {
        open_panel(fe, ui, item == pause_item_options ? page_options : page_screen);
    }
    ui.pad = PadRecord{};
    ui.pointer.left = false;
}

void pause_settings_close(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    s.listening = false;
    s.calibration.open = false;
    fe.pause_settings = false;
    menu_input_mark(ui);
}

void pause_settings_frame(FrontEnd& fe, Ui& ui) {
    if (!fe.pause_settings) {
        return;
    }
    if (!fe.campaign.paused || !fe.campaign.battle_running) {
        pause_settings_close(fe, ui);
        return;
    }
    // VS: every entered seat's pad, as the pause's menu reads them; nothing while REPORT A BUG is
    // up over it (its frame blanked the pads already)
    if (fe.vs_mode && fe.report.mode == 0) {
        ui.pad = pause_pads(fe, ui);
    }
    const AnchorScreen* screen = ui.anchor_screen;
    panel_begin(ui);
    ui.anchor_screen = ui.pointer.anchors = ui.panel_anchors;
    pc_settings_input(fe, ui);
    if (fe.pause_settings) {
        pc_settings_draw(fe, ui, true);
    }
    ui.panel = false;
    ui.anchor_screen = ui.pointer.anchors = screen;
    // the pause under it reads nothing
    ui.pad = PadRecord{};
    ui.pointer.left = ui.pointer.right = false;
    ui.pointer.wheel = 0;
}

int pause_options_count() {
    return option_rows;
}

PcRowView pause_options_view(FrontEnd& fe, int row) {
    PcRowView view;
    if (row < 0 || row >= option_rows) {
        return view;
    }
    const MachineOptions& o = game_options(fe);
    view.label = option_text[row].label;
    view.help = option_text[row].help;
    switch (row) {
    case option_vibration:
        view.value = o.vibration == 0 ? "ON" : "OFF"; // the options' box: 0 ON, 1 OFF
        break;
    case option_bgm:
        view.value = std::to_string(o.music_volume);
        view.bar = o.music_volume * 100 / 255;
        break;
    case option_se:
        view.value = std::to_string(o.se_volume);
        view.bar = o.se_volume * 100 / 255;
        break;
    case option_shift_x:
        view.value = std::to_string(o.screen_shift_x);
        break;
    case option_shift_y:
        view.value = std::to_string(o.screen_shift_y);
        break;
    default:
        view.value = "NOT IN A FIGHT";
        view.arrows = false;
        view.dim = true;
        break;
    }
    return view;
}

bool pause_options_step(FrontEnd& fe, Ui& ui, int row, int step) {
    MachineOptions& o = game_options(fe);
    const MachineOptions before = o;
    switch (row) {
    case option_vibration:
        o.vibration = o.vibration == 0 ? 1 : 0;
        break;
    case option_bgm:
        o.music_volume = std::clamp(o.music_volume + step * volume_step, 0, 255);
        if (ui.audio) {
            set_music_level(*ui.audio, o.music_volume);
        }
        break;
    case option_se:
        o.se_volume = std::clamp(o.se_volume + step * volume_step, 0, 255);
        if (ui.audio) {
            set_sound_level(*ui.audio, o.se_volume);
        }
        break;
    case option_shift_x:
        // the adjust screen's ranges (adjust_screen_*: 6 signed bits), live as there
        o.screen_shift_x = std::clamp(o.screen_shift_x + step, -32, 30);
        ui.screen_shift[0] = o.screen_shift_x;
        break;
    case option_shift_y:
        o.screen_shift_y = std::clamp(o.screen_shift_y + step, -16, 14);
        ui.screen_shift[1] = o.screen_shift_y;
        break;
    default:
        return false;
    }
    return o.vibration != before.vibration || o.music_volume != before.music_volume ||
           o.se_volume != before.se_volume || o.screen_shift_x != before.screen_shift_x ||
           o.screen_shift_y != before.screen_shift_y;
}
