// KEEP SV's steps (keep_sv.hpp; the drawing is keep_sv_draw.cpp's). Retail reads the drive's saved games a window at a
// time (vs_save_infos_fill) and shows a read failure as "unable to load"; the port reads every
// kept machine on entry and after each change, and a file that doesn't read shows as a damaged
// row. The drive's block and save-game limits (boxes 8, 9 and 12, states 13 and 14) can't happen.
#include "screens/campaign/keep_sv.hpp"

#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/campaign/area_select.hpp"
#include "screens/flow/extra_screens.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

using namespace keep_sv_states;

namespace {

constexpr int keep_sv_asset = 0x678; // keep_sv_tex.bin
constexpr int common_asset = 0x690;

// the save game's name (vs_save_write): the machine's name, its full-width letters as ASCII, and
// " for VS"; characters a file name can't hold become '_'
std::string save_name_of(Ui& ui, const GlyphName& name) {
    std::string out;
    for (int k = 0; k < glyph_name_length && name[k] != -1; ++k) {
        std::uint32_t code = '?';
        for (const auto& [point, glyph] : ui.data.font.glyph_of) {
            if (glyph == name[k]) {
                code = point;
                break;
            }
        }
        // fullwidth_to_ascii_wide
        if (code >= 0xff01 && code <= 0xff5e) {
            code -= 0xfee0;
        } else if (code == 0x3000) {
            code = ' ';
        }
        // the rest as it is (UTF-8 here), but what a file name can't hold
        if (code < 0x20 || code == '/' || code == '\\' || code == ':' || code == '*' ||
            code == '?' || code == '"' || code == '<' || code == '>' || code == '|' ||
            code == '.') {
            out += '_';
        } else if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xc0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    return (out.empty() ? std::string("Scoobee") : out) + " for VS";
}

// vs_save_find_garage
int find_garage(Ui& ui, const KeepSvScreen& k, const Garages& g, int garage) {
    const std::string name = save_name_of(ui, g.garages[garage].name);
    for (std::size_t i = 0; i < k.saves.size(); ++i) {
        if (k.saves[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// vs_save_write: the hangar's machine with its parts resolved, now, its names, the pilot's and the
// button config
bool write_garage(FrontEnd& fe, Ui& ui, KeepSvScreen& k, int garage) {
    const World& w = *fe.world;
    const Garage& l = w.garages.garages[garage];
    VsSave save;
    save.name = save_name_of(ui, l.name);
    save.present = true;
    save.written = static_cast<std::int64_t>(std::time(nullptr));
    save.build = garage_build(w.garages, garage);
    std::copy(std::begin(l.name), std::end(l.name), std::begin(save.mech));
    if (l.chip >= 0 && l.chip < inventory_size) {
        const GlyphName& chip = w.garages.inventory[l.chip].name;
        std::copy(std::begin(chip), std::end(chip), std::begin(save.chip));
    }
    std::copy(std::begin(w.profile.pilot_name), std::end(w.profile.pilot_name),
              std::begin(save.pilot));
    save.buttons = w.profile.options.buttons; // hdr.stats, GameState +0x6018
    std::string error;
    const bool ok = vs_save_store(fe.save_dir, save, error);
    if (!ok) {
        std::fprintf(stderr, "keep sv: %s\n", error.c_str());
    }
    k.saves = vs_saves_list(fe.save_dir);
    return ok;
}

void box_open(KeepSvScreen& k, int box) {
    msg_box_open(k.boxes[box]);
}

void fade_out(Ui& ui, KeepSvScreen& k, int next) {
    k.next = next;
    fade_out_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
}


// the side menu: 0 the list, 1 SAVE, 2 EXIT (vertical, sound 3)
void side_menu(Ui& ui, KeepSvScreen& k) {
    if (menu_cursor_move(ui.pad, k.at.menu, 0, 3, cursor_menu_vertical) != 0) {
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
    }
}

bool back_pressed(Ui& ui, KeepSvScreen& k) {
    if (!menu_cancel(ui)) {
        return false;
    }
    k.at.chosen = -1;
    k.at.menu = 2;
    k.state = s_exit_item;
    ui_sound(ui, bank_sys, se_menu_cursor_vertical);
    return true;
}

// a box's answers: horizontal, sound 4, the mouse over one takes the cursor; cancel moves to
// `back` with sound 4
void answer_cursor(Ui& ui, KeepSvScreen& k, int count, int back, const PointerRect* rects) {
    if (menu_cursor(ui, k.sel, 0, count, cursor_menu_horizontal, 0, 4, rects) != 0) {
        return;
    }
    if (menu_cancel(ui)) {
        k.sel = back;
        ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
    }
}

bool accept_sound(Ui& ui, const PointerRect* rect = nullptr) {
    if (!menu_accept(ui, rect)) {
        return false;
    }
    ui_sound(ui, bank_sys, se_menu_accept);
    return true;
}

// keep_sv_screen_step4: the input of each state
void input_step(FrontEnd& fe, Ui& ui, KeepSvScreen& k) {
    if (k.state == s_list || k.state == s_save_item || k.state == s_exit_item) {
        keep_sv_pointer_step(ui, k);
    }
    PointerRect answers[3];
    keep_sv_answer_rects(ui, k, answers);
    switch (k.state) {
    case s_list: {
        const int count = static_cast<int>(k.saves.size());
        if (k.scroll == 0) {
            int moved = 0;
            menu_list_move(ui.pad, k.at.cursor, k.at.wanted, 0, std::max(count, keep_sv_rows),
                           keep_sv_rows, cursor_value_horizontal, moved);
            if (moved != 0) {
                ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
                menu_input_mark(ui);
            }
            if (k.at.wanted != k.at.top) {
                k.scroll_clock = 4.0f;
                k.scroll = k.at.wanted > k.at.top ? 3 : 1;
            }
        }
        if (k.scroll == 0) {
            k.at.chosen = k.at.top + k.at.cursor;
            side_menu(ui, k);
            if (k.at.menu == 0) {
                const PointerRect place = keep_sv_place_rect(ui, k.at.cursor + 1);
                if (menu_accept(ui, &place)) {
                    const VsSave* save = keep_sv_slot(k, k.at.cursor + 1);
                    if (k.at.chosen >= 0 && save) {
                        ui_sound(ui, bank_sys, se_menu_accept);
                        k.sel = 0;
                        k.state = save->present ? s_kept : s_damaged;
                        box_open(k, save->present ? 1 : 10);
                    } else {
                        ui_sound(ui, bank_sys, se_menu_cancel);
                    }
                }
            } else {
                k.at.chosen = -1;
                k.state = k.at.menu == 1 ? s_save_item : s_exit_item;
            }
            back_pressed(ui, k);
        } else if ((k.scroll_clock -= static_cast<float>(60 / ui.frame_cadence)) <= 0.0f) {
            if (k.scroll == 1 || k.scroll == 3) {
                k.at.top = k.at.wanted;
                k.scroll_clock = 4.0f;
                k.scroll = k.scroll == 1 ? 2 : 4;
            } else {
                k.scroll = 0;
            }
        }
        break;
    }
    case s_save_item:
        side_menu(ui, k);
        if (k.at.menu == 0) {
            k.state = s_list;
        } else if (k.at.menu == 1) {
            const PointerRect save = keep_sv_side_rect(ui, 1);
            if (accept_sound(ui, &save)) {
                // a whole machine to keep, or the "none to save" box
                bool any = false;
                for (int g = 0; g < 10 && !any; ++g) {
                    any = (garage_status(fe.shop.tables, fe.world->garages, g) & 4) != 0;
                }
                k.sel = 0;
                k.state = any ? s_save_ask : s_none_to_save;
                box_open(k, any ? 0 : 14);
            }
        } else {
            k.at.chosen = -1;
            k.state = s_exit_item;
        }
        back_pressed(ui, k);
        break;
    case s_kept:
        answer_cursor(ui, k, 3, 2, answers);
        if (accept_sound(ui, &answers[k.sel])) {
            if (k.sel == 0) {
                // look: the spec page shows it (GameState +0x10F5C, +0x10F98, +0x10FAA)
                const VsSave& save = *keep_sv_slot(k, k.at.cursor + 1);
                const RosterBuild& b = save.build;
                ShopBuild& spec = fe.shop.spec;
                spec = ShopBuild{};
                spec.body = b.body, spec.body_tune = b.body_tune;
                spec.leg = b.leg, spec.leg_tune = b.leg_tune;
                spec.chip = b.chip, spec.chip_tune = b.chip_tune;
                for (int m = 0; m < 4; ++m) {
                    spec.weapon[m] = b.weapon[m].id;
                    spec.weapon_tune[m] = b.weapon[m].tune;
                }
                spec.color = b.color, spec.sticker = b.sticker;
                for (int o = 0; o < 10; ++o) {
                    spec.option[o] = b.option[o];
                }
                glyph_name_copy(fe.shop.spec_name, save.mech);
                glyph_name_copy(fe.shop.spec_chip_name, save.chip);
                fade_out(ui, k, 3);
            } else if (k.sel == 1) {
                k.sel = 1;
                k.state = s_delete_ask;
                box_open(k, 3);
            } else {
                k.state = s_list;
            }
        }
        break;
    case s_damaged:
        answer_cursor(ui, k, 2, 1, answers);
        if (accept_sound(ui, &answers[k.sel])) {
            if (k.sel == 0) {
                k.sel = 1;
                k.state = s_delete_ask;
                box_open(k, 3);
            } else {
                k.state = s_list;
            }
        }
        break;
    case s_save_ask:
        answer_cursor(ui, k, 2, 1, answers);
        if (accept_sound(ui, &answers[k.sel])) {
            if (k.sel == 0) {
                fade_out(ui, k, 1); // to the hangar, in its keep SV mode
            } else {
                k.state = s_list;
            }
        }
        break;
    case s_saved: {
        const PointerRect ok = msg_box_ok_rect(ui, k.boxes[2]);
        if (accept_sound(ui, &ok)) {
            k.state = s_save_item;
        }
        break;
    }
    case s_overwrite:
        answer_cursor(ui, k, 2, 1, answers);
        if (accept_sound(ui, &answers[k.sel])) {
            if (k.sel == 0 && !write_garage(fe, ui, k, fe.shop.editing)) {
                k.state = s_save_failed;
                box_open(k, 6);
            } else if (k.sel == 0) {
                k.state = s_saved;
                box_open(k, 2);
            } else {
                k.state = s_save_item;
            }
        }
        break;
    case s_delete_ask:
        answer_cursor(ui, k, 2, 1, answers);
        if (accept_sound(ui, &answers[k.sel])) {
            k.state = s_list;
            if (k.sel == 0) {
                const VsSave* save = keep_sv_slot(k, k.at.cursor + 1);
                if (save && vs_save_remove(fe.save_dir, save->name)) {
                    k.saves = vs_saves_list(fe.save_dir);
                    // the window slides back when the last page empties
                    if (k.at.top > 0 && static_cast<int>(k.saves.size()) - keep_sv_rows <= k.at.top) {
                        k.at.wanted -= 1;
                        k.scroll_clock = 4.0f;
                        k.scroll = 1;
                    }
                } else {
                    k.state = s_delete_failed;
                    box_open(k, 7);
                }
            }
        }
        break;
    case s_exit_item:
        side_menu(ui, k);
        if (k.at.menu == 0) {
            k.state = s_list;
        } else if (k.at.menu == 1) {
            k.state = s_save_item;
        }
        if (k.state == s_exit_item) {
            const PointerRect exit = keep_sv_side_rect(ui, 2);
            if (accept_sound(ui, &exit)) {
                fade_out(ui, k, 0);
            }
        }
        break;
    case s_none_to_save:
        if (accept_sound(ui)) {
            k.state = s_list;
        }
        break;
    case s_read_failed:
    case s_save_failed:
    case s_delete_failed: {
        const int box = k.state == s_read_failed ? 5 : k.state == s_save_failed ? 6 : 7;
        const PointerRect ok = msg_box_ok_rect(ui, k.boxes[box]);
        if (accept_sound(ui, &ok)) {
            k.state = s_list;
        }
        break;
    }
    default:
        break;
    }
}

} // namespace

void keep_sv_spawn(FrontEnd& fe, Ui&) {
    fe.screen.campaign.keep_sv = KeepSvScreen{};
    fe.screen.campaign.keep_sv.mode = fe.previous;
}

void keep_sv_step(FrontEnd& fe, Ui& ui) {
    KeepSvScreen& k = fe.screen.campaign.keep_sv;
    switch (fe.screen.step) {
    case 0: { // keep_sv_screen_update and step3: the sheets, the list, the opening state
        texture_set_load(ui, keep_sv_asset);
        texture_set_load(ui, common_asset);
        for (int b = 0; b < 15; ++b) {
            k.boxes[b] = msg_box_from(ui, "keep_sv", "keep_sv_boxes", b);
        }
        k.saves = vs_saves_list(fe.save_dir);
        level_meter_spawn(ui, fe.screen.meter);
        const int garage = fe.shop.editing;
        if (k.mode != 0xe) {
            k.state = s_save_item;
        } else if (garage >= 0 && garage < 10) {
            k.state = s_saved;
            box_open(k, 2);
        } else {
            k.state = s_list;
        }
        k.banner_timer = static_cast<int>(ui.task_timer);
        help_strip_init(ui, k.text, table_text(ui, "keep_sv", "keep_sv_help"));
        ui_music(ui, 0xc, true);
        now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
        if (k.mode == 0x27) {
            k.at = KeepSvKept{};
            k.at.menu = 1;
        } else {
            k.at = fe.campaign.keep_sv;
            k.clock = keep_sv_clock_end;
        }
        if (k.state == s_saved) {
            // the hangar's pick: kept, or asked about the one of the same name
            if (find_garage(ui, k, fe.world->garages, garage) >= 0) {
                k.state = s_overwrite;
                k.sel = 1;
                box_open(k, 4);
            } else if (!write_garage(fe, ui, k, garage)) {
                k.state = s_save_failed;
                box_open(k, 6);
            }
        }
        fade_in_start(ui, 30.0f);
        fe.screen.step = 1;
        break;
    }
    case 1: // keep_sv_list_reveal: until the list has faded in
        if (k.clock >= 26.0f) {
            fe.screen.step = 2;
        }
        keep_sv_draw(fe, ui, k);
        break;
    case 2: // keep_sv_screen_step4
        if (fade_is_idle(ui)) {
            input_step(fe, ui, k);
        }
        keep_sv_draw(fe, ui, k);
        if (!fade_is_idle(ui) && !fade_is_black(ui) && k.next >= 0 && fe.screen.step == 2 &&
            ui.fade.state != 0) {
            // the half-second fade out runs (keep_sv_leave)
        }
        if (fade_is_black(ui)) {
            task_header_set(fe, k.next);
            fe.screen.step = 3;
        }
        break;
    default:
        keep_sv_draw(fe, ui, k);
        break;
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

void keep_sv_destroy(FrontEnd& fe, Ui& ui) {
    // keep_sv_screen_destroy: the window and cursor kept for the way back
    fe.campaign.keep_sv = fe.screen.campaign.keep_sv.at;
    ui_music_stop(ui);
    fe.screen.strip.active = false;
}
