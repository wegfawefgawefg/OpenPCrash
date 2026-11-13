// VS setup's machine page (decomp docs/port/screens.md "Machine page": step6, vs_entry_update,
// vs_panel_machine_draw, vs_panel_name_draw): each player's list of the
// three stock machines and then KEEP SV's kept ones (src/save/vs_saves.hpp), three cells in view
// with the edge of a fourth, an 8-frame scroll, the locked machine's picture and name, and the
// card for a kept machine that doesn't read.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/ui/screen_header.hpp"
#include "screens/vs/vs_setup.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cstdio>

namespace {

constexpr std::uint32_t orange = 0xFFFEB000;
constexpr std::uint32_t text_colour = 0xFFF0F0F0;
// a kept machine's cell and picture rows by its body class 0..5 (cells lit at +1), and an
// unreadable one's cell
constexpr int save_cells[6] = {86, 88, 82, 90, 92, 84};
constexpr int save_pictures[6] = {106, 107, 104, 108, 109, 105};
constexpr int failed_cell = 96;

int rows(const VsSession& vs) {
    return vs_stock_count + static_cast<int>(vs.saves.size());
}

const VsSave* save_at(const VsSession& vs, int row) {
    const int k = row - vs_stock_count;
    return k >= 0 && k < static_cast<int>(vs.saves.size()) ? &vs.saves[static_cast<std::size_t>(k)]
                                                           : nullptr;
}

// a kept machine's body class (Scoobee::body_class), -1 when it has none of the six
// decomp: Scoobee::body_class 0x5F6D0
int body_class(FrontEnd& fe, const VsSave& save) {
    const int c = part_class(fe.shop.tables, 0, save.build.body);
    return c >= 0 && c < 6 ? c : -1;
}

// the cell row a list row draws with, -1 none
int cell_of(FrontEnd& fe, int row) {
    if (row >= 0 && row < vs_stock_count) {
        return 98 + 2 * row;
    }
    const VsSave* save = save_at(fe.vs, row);
    if (!save) {
        return -1;
    }
    const int c = save->present ? body_class(fe, *save) : -1;
    // retail stops drawing the whole list at a body class past 5 (read); the port shows the
    // unreadable cell
    return c >= 0 ? save_cells[c] : failed_cell;
}

// a row's machine name: a kept machine's own, or the stock loadout's
GlyphText name_of(FrontEnd& fe, Ui& ui, int row) {
    if (const VsSave* save = save_at(fe.vs, row)) {
        return save->present ? glyph_name_text(save->mech, glyph_name_length) : GlyphText{};
    }
    const int build = vs_stock_loadout(ui, row);
    if (build >= 0 && build < static_cast<int>(fe.shop.tables.builds.size())) {
        return fe.shop.tables.builds[static_cast<std::size_t>(build)].name.glyphs;
    }
    return {};
}

// a row a player may lock: a stock machine or a kept one that read
bool lockable(const VsSession& vs, int row) {
    const VsSave* save = save_at(vs, row);
    return (row >= 0 && row < vs_stock_count) || (save && save->present);
}

// the scroll's cell shift (vs_panel_machine_draw): 47.5 px at most, back to 0 after the swap
float scroll_shift(const VsPanel& p) {
    if (p.scroll == 0) {
        return 0;
    }
    float t = 1.0f - p.scroll_clock * 0.25f;
    t = p.scroll == 3 ? -t : p.scroll == 4 ? 1.0f - t : p.scroll == 2 ? -(1.0f - t) : t;
    return t * 47.5f;
}

// vs_save_infos_fill's failure: a kept machine coming into the five slots that doesn't read
void check_read(FrontEnd& fe, VsPanel& p, int row) {
    const VsSave* save = save_at(fe.vs, row);
    if (save && !save->present) {
        p.reading = true;
        p.error_clock = 0;
    }
}

// the error card (row 64 + i rolling in over 10 frames, then D_001FEC34's lines); accept
// dismisses it
void error_step(FrontEnd& fe, Ui& ui, int i, VsPanel& p, const PadRecord& pad) {
    if (p.error_clock < 10.0f) {
        vs_row_draw(ui, 64 + i, 1, p.error_clock * 0.1f);
        p.error_clock += 1;
        return;
    }
    vs_row_draw(ui, 64 + i);
    const std::vector<std::string> lines = ui_strings(ui.data, "vs_mode", "vs_area_lines", 0, 6);
    const int x = static_cast<int>(vs_number(ui, 0x1FFC24 + 8u * static_cast<unsigned>(i)));
    const int y = static_cast<int>(vs_number(ui, 0x1FFC28 + 8u * static_cast<unsigned>(i)));
    for (std::size_t line = 0; line < lines.size(); ++line) {
        const int dy = static_cast<int>(vs_number(ui, 0x1FFC44 + 4u * static_cast<unsigned>(line)));
        text_draw(ui, utf8_to_glyphs(ui.data, lines[line]), x, y + dy, 8, 18, 18, text_colour, 30);
    }
    if (vs_accept(fe, ui, i, pad)) {
        ui_sound(ui, bank_sys, se_menu_accept);
        p.reading = false;
        p.error_clock = 0;
    }
}

// one unlocked player's list: the cursor in a window of three, the scroll, the lock
void list_step(FrontEnd& fe, Ui& ui, int i, VsPanel& p, const PadRecord& pad, bool& quit) {
    if (p.scroll == 0) {
        PointerRect cells[3];
        vs_machine_rects(ui, i, cells);
        const int hover = vs_hover(fe, ui, i, cells, std::min(3, rows(fe.vs)));
        const int wheel = i == vs_mouse_seat(fe) ? ui.pointer.wheel : 0;
        int moved = 0;
        menu_list_move(pad, p.cursor, p.top_wanted, 0, rows(fe.vs), 3, cursor_value_horizontal,
                       moved);
        if (moved == 0 && hover >= 0 && hover != p.cursor) {
            p.cursor = hover;
            moved = 1;
        } else if (moved == 0 && wheel != 0) {
            // the wheel steps the list as the d-pad would, scrolling at the window's edge
            const int row = std::clamp(p.top + p.cursor - wheel, 0, rows(fe.vs) - 1);
            if (row != p.top + p.cursor) {
                p.top_wanted = std::clamp(row - std::clamp(row - p.top, 0, 2), 0,
                                          std::max(rows(fe.vs) - 3, 0));
                p.cursor = row - p.top_wanted;
                moved = 1;
            }
        }
        if (moved != 0) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        if (p.top_wanted != p.top) {
            p.scroll_clock = 4;
            p.scroll = p.top_wanted > p.top ? 3 : 1;
        }
    }
    if (p.scroll == 0) {
        PointerRect cells[3];
        vs_machine_rects(ui, i, cells);
        if (vs_accept(fe, ui, i, pad, &cells[std::clamp(p.cursor, 0, 2)])) {
            // sound 5 even when refused (an unreadable kept machine)
            ui_sound(ui, bank_sys, se_menu_accept);
            if (lockable(fe.vs, p.top + p.cursor)) {
                p.locked = true;
                vs_panel_anim(p, vs_title, 1);
            }
        } else if (vs_cancel(fe, ui, i, pad)) {
            quit = true;
        }
        return;
    }
    // the scroll: four frames out, the window moves, four frames back in
    p.scroll_clock -= 1;
    if (p.scroll_clock > 0) {
        return;
    }
    if (p.scroll == 1 || p.scroll == 3) {
        const bool later = p.scroll == 3;
        p.top = p.top_wanted;
        p.scroll_clock = 4;
        p.scroll = later ? 4 : 2;
        check_read(fe, p, later ? p.top + 3 : p.top - 1);
    } else {
        p.scroll = 0;
    }
}

} // namespace

void vs_machines_reset(FrontEnd& fe) {
    VsSession& vs = fe.vs;
    vs.saves = vs_saves_list(fe.save_dir);
    for (VsPanel& p : vs.panels) {
        p.locked = false;
        p.cursor = p.top = p.top_wanted = p.scroll = 0;
        p.scroll_clock = p.error_clock = 0;
        p.reading = false;
    }
    // the first fill reads rows -1..3: the first kept machine
    for (int i = 0; i < vs_panel_count; ++i) {
        if (vs.entered[static_cast<std::size_t>(i)]) {
            check_read(fe, vs.panels[static_cast<std::size_t>(i)], vs_stock_count);
        }
    }
}

void vs_machine_rects(Ui& ui, int i, PointerRect* out) {
    float ox = 0, oy = 0, left = 0, right = 640;
    vs_pair_at(ui, 0x1FFB78, i, ox, oy);
    vs_pair_at(ui, 0x1FFB98, i, left, right);
    for (int n = 1; n <= 3; ++n) {
        const SpriteSlot* slot = vs_row(ui, 98 + 2 * (n - 1));
        PointerRect& r = out[n - 1];
        r = slot ? sprite_rect(ui, slot->sprite, ox + 60.0f * static_cast<float>(n), oy)
                 : PointerRect{};
        // the panel's clip
        r.left = std::max(r.left, left);
        r.right = std::min(r.right, right);
    }
}

void vs_machine_draw(FrontEnd& fe, Ui& ui, int i, float fill) {
    const VsPanel& p = fe.vs.panels[static_cast<std::size_t>(i)];
    float ox = 0, oy = 0, left = 0, right = 640;
    vs_pair_at(ui, 0x1FFB78, i, ox, oy);
    vs_pair_at(ui, 0x1FFB98, i, left, right);
    // the arrows, orange while the list scrolls their way
    vs_row_draw(ui, 48 + i, 1, fill, 0, p.scroll == 1 || p.scroll == 2 ? orange : 0xFFFFFFFF,
                true);
    vs_row_draw(ui, 52 + i, 1, fill, 0, p.scroll == 3 || p.scroll == 4 ? orange : 0xFFFFFFFF,
                true);
    const float shift = scroll_shift(p);
    for (int n = 0; n < 5; ++n) {
        const int id = cell_of(fe, p.top + n - 1);
        const SpriteSlot* slot = id >= 0 ? vs_row(ui, id) : nullptr;
        if (!slot) {
            continue;
        }
        const float x = ox + 60.0f * static_cast<float>(n) + shift;
        Sprite sprite = sprite_of(ui, slot->sprite);
        sprite.pos[0] = x;
        sprite.pos[1] = oy;
        sprite.blend = slot->blend;
        sprite.scale[1] = fill;
        sprite_submit_clipped(ui, sprite, left, 0, right, 480);
        // the cursor's cell lit on top, pulsing until locked, while still
        if (p.cursor + 1 == n && p.scroll == 0) {
            if (const SpriteSlot* lit = vs_row(ui, id + 1)) {
                Sprite on = sprite_of(ui, lit->sprite);
                on.pos[0] = x;
                on.pos[1] = oy;
                on.blend = lit->blend;
                on.scale[1] = fill;
                on.flags |= p.locked ? 0u : 0x2000u;
                sprite_submit(ui, on);
            }
        }
    }
    // the name rolls with the panel: glyphs fill x 18 high (wide_text_draw_small's height)
    if (const int glyph_h = static_cast<int>(fill * 18.0f); glyph_h > 0) {
        float x = 0, y = 0;
        vs_pair_at(ui, 0x1FFBC4, i, x, y);
        text_draw(ui, name_of(fe, ui, p.top + p.cursor), static_cast<int>(x),
                  static_cast<int>(y), 3, 18, glyph_h, text_colour, 9);
    }
}

void vs_locked_draw(FrontEnd& fe, Ui& ui, int i, float fill) {
    const VsPanel& p = fe.vs.panels[static_cast<std::size_t>(i)];
    const int row = p.top + p.cursor;
    int picture = row >= 0 && row < vs_stock_count ? 110 + row : -1;
    if (const VsSave* save = save_at(fe.vs, row)) {
        const int c = save->present ? body_class(fe, *save) : -1;
        picture = c >= 0 ? save_pictures[c] : -1;
    }
    float x = 0, y = 0;
    vs_pair_at(ui, 0x1FFBE4, i, x, y);
    if (const SpriteSlot* slot = picture >= 0 ? vs_row(ui, picture) : nullptr) {
        Sprite sprite = sprite_of(ui, slot->sprite);
        sprite.pos[0] = x;
        sprite.pos[1] = y;
        sprite.scale[1] = fill;
        sprite.blend = slot->blend;
        sprite_submit(ui, sprite);
    }
    if (const int glyph_h = static_cast<int>(fill * 18.0f); glyph_h > 0) {
        vs_pair_at(ui, 0x1FFC04, i, x, y);
        text_draw(ui, name_of(fe, ui, row), static_cast<int>(x), static_cast<int>(y), 6, 18,
                  glyph_h, text_colour, 9);
    }
}

void vs_machine_picks(FrontEnd& fe, Ui& ui) {
    VsSession& vs = fe.vs;
    for (int i = 0; i < vs_panel_count; ++i) {
        const VsPanel& p = vs.panels[static_cast<std::size_t>(i)];
        const int row = p.top + p.cursor;
        vs.kept[static_cast<std::size_t>(i)] = save_at(vs, row) ? row - vs_stock_count : -1;
        vs.build[static_cast<std::size_t>(i)] = vs_stock_loadout(ui, row);
    }
}

void vs_machine_step(FrontEnd& fe, Ui& ui) {
    VsSession& vs = fe.vs;
    vs_page_draw(fe, ui, 1);
    vs.clock += 1;
    switch (vs.sub) {
    case 1: {
        bool quit = false;
        for (int i = 0; i < vs_panel_count; ++i) {
            VsPanel& p = vs.panels[static_cast<std::size_t>(i)];
            const PadRecord* pad = vs_seat_pad(fe, ui, i);
            if (!pad) {
                continue;
            }
            if (p.reading) {
                error_step(fe, ui, i, p, *pad);
            } else if (!p.locked) {
                list_step(fe, ui, i, p, *pad, quit);
            } else if (vs_cancel(fe, ui, i, *pad)) {
                ui_sound(ui, bank_sys, se_menu_cancel);
                p.locked = false;
                vs_panel_anim(p, vs_title, 3);
            } else if (vs_accept(fe, ui, i, *pad)) {
                ui_sound(ui, bank_sys, se_menu_cancel);
            }
        }
        bool all_locked = true;
        for (int i = 0; i < vs_panel_count; ++i) {
            all_locked = all_locked && (!vs.entered[static_cast<std::size_t>(i)] ||
                                        vs.panels[static_cast<std::size_t>(i)].locked);
        }
        if (quit && !all_locked) {
            vs.quit_box = msg_box_from(ui, "pause", "vs_quit_box", 0);
            msg_box_open(vs.quit_box);
            vs.choice = 1;
            vs.sub = 6;
        } else if (all_locked) {
            vs.sub = 2;
            vs.clock = 0;
            vs.choice = 0;
        }
        break;
    }
    case 2: {
        vs_confirm_draw(fe, ui, std::min(static_cast<float>(vs.clock) / 10.0f, 1.0f));
        const int answer = vs.clock >= 10 ? vs_confirm_input(fe, ui) : -1;
        if (answer == 1) {
            fade_out_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
            vs.sub = 5;
            vs.clock = 0;
        } else if (answer == 0) {
            vs.sub = 3;
            vs.clock = 0;
        }
        break;
    }
    case 3: // no: every panel unlocked
        vs_confirm_draw(fe, ui, std::max(1.0f - static_cast<float>(vs.clock) / 10.0f, 0.0f));
        if (vs.clock >= 10) {
            for (VsPanel& p : vs.panels) {
                p.locked = false;
                vs_panel_anim(p, vs_title, 3);
            }
            vs.sub = 1;
        }
        break;
    case 4: // the quit box's yes: back to the join page, entries and records cleared
        vs_panels_close_contents(vs, vs.clock);
        if (vs.clock >= 46) {
            vs_go(fe, vs_step_join_reset);
        }
        break;
    case 5: // the builds go in, on to the area page
        vs_panels_close_all(vs, vs.clock);
        if (vs.clock >= 62 && fade_is_black(ui)) {
            vs_machine_picks(fe, ui);
            vs_go(fe, vs_step_area_reset);
        }
        break;
    case 6: { // the quit box
        msg_box_update_choice(ui, vs.quit_box, vs.choice);
        PointerRect answers[2];
        msg_box_choice_rects(ui, vs.quit_box, answers);
        for (int i = 0; i < vs_panel_count && vs.sub == 6; ++i) {
            const PadRecord* pad = vs_seat_pad(fe, ui, i);
            if (!pad) {
                continue;
            }
            const int hover = vs_hover(fe, ui, i, answers, 2);
            if (menu_cursor_move(*pad, vs.choice, 0, 2, cursor_menu_horizontal) != 0 ||
                (hover >= 0 && hover != vs.choice)) {
                vs.choice = hover >= 0 ? hover : vs.choice;
                ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            }
            if (vs_accept(fe, ui, i, *pad, i == vs_mouse_seat(fe) ? &answers[vs.choice] : nullptr)) {
                ui_sound(ui, bank_sys, se_menu_accept);
                vs.sub = vs.choice == 0 ? 4 : 1;
                vs.clock = 0;
            } else if (vs_cancel(fe, ui, i, *pad)) {
                vs.choice = 1;
                ui_sound(ui, bank_sys, se_menu_cancel);
            }
        }
        break;
    }
    default:
        break;
    }
    vs_panels_draw(fe, ui, 1);
}
