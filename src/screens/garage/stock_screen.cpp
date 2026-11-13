// The modules in possession (stock_module_* state 0x26, decomp src/o1/u_03c67e.c): the editor's
// first four columns of categories and exit as tabs, each a list of what is owned with where it
// is fitted, the row's card and blurb.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

namespace {

constexpr int common_asset = 0x690, stock_asset = 0x687, module_asset = 0x692;
// the tab slides (retail .rdata: D_0019DBD4 targets, D_0019DBFC / D_0019DC10 ids, D_0019DC24 the
// lit tabs); their starts are the editor's grid's (the fifth, exit, starts at 0 in retail)
constexpr int targets[5] = {72, 117, 162, 207, 252};
constexpr int top_start[5] = {-488, -403, -318, -233, -148};
constexpr int bottom_start[5] = {-558, -468, -378, -288, -198};
constexpr int top_ids[5] = {2569, 2565, 2567, 2584, -1};
constexpr int bottom_ids[5] = {2582, 2572, 2574, 2592, 2576};
constexpr int lit_ids[2][5] = {{2570, 2566, 2568, 2585, -1}, {2583, 2573, 2575, 2593, 2577}};
constexpr int tab_map[10] = {0, 1, 2, 4, 3, 6, 5, 7, 8, 8}; // D_001FE08C + 6
constexpr int titles[8] = {0xa38, 0xa3b, 0xa36, 0xa37, 0xa39, 0xa3c, 0xa3a, 0xa3d};
constexpr int class_icons[6] = {0xa13, 0xa1b, 0xa04, 0xa23, 0xa22, 0xa0b};

void blurb(Ui& ui, StockScreen& s) {
    const GaragePage& page = s.pages[s.tab];
    const int row = s.scroll + s.cursor;
    if (s.tab > 7 || row >= static_cast<int>(page.entries.size())) {
        return;
    }
    // D_001FE7CC: each kind's first line in D_001FE138 (as addresses)
    const std::vector<double> starts =
        ui_values(ui.data, "stock_module", "stock_module_kind_starts", 8);
    const int first = (static_cast<int>(starts[static_cast<std::size_t>(s.tab)]) - 0x1fe138) / 4;
    help_strip_init(ui, s.help,
                    ui_strings(ui.data, "stock_module", "stock_module_lines",
                               first + page.entries[static_cast<std::size_t>(row)].id, 1)[0]);
}

void header_draw(FrontEnd& fe, Ui& ui, StockScreen& s, bool tabs) {
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    sprite_rows_draw(ui, ui_slots(ui.data, "stock_module", "stock_tab_header_sprites"));
    garage_banner_draw(ui, *fe.world, s.frames < 0 ? task_timer_get(ui) : -1, 0x17a, 2);
    slide_draw(ui, s.tabs, 5, 0x18, 0x101, 0xd4, targets, top_ids);
    slide_draw(ui, s.bottom_tabs, 5, 0x18, 0x101, 0x101, targets, bottom_ids);
    const int x = s.tab_row == 0 ? s.tabs.pos[s.tab_col] : s.bottom_tabs.pos[s.tab_col];
    sprite_draw_at(ui, lit_ids[s.tab_row][s.tab_col], static_cast<float>(x),
                   static_cast<float>(s.tab_row) * 45.0f + 212.0f, 0x102, 0xffd300 | 0xff000000u,
                   tabs ? 0x2000 : 0);
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

// the cursor's item's card under its two label plates, which pop
// open first (the second plate's pop gates the card); the kind's picture and, for Chips, colours
// and stickers, the corner mark
// decomp: stock_option_card_draw 0x3D4A7
void card_draw(FrontEnd& fe, Ui& ui, StockScreen& s, const InventoryItem& item) {
    const PartTables& t = fe.shop.tables;
    constexpr int pictures[8] = {0xa2e, 0xa31, 0xa35, 0xa35, 0xa2f, 0xa32, 0xa30, 0xa34};
    const bool option = item.kind == 5;
    const int a = option ? 0xa2d : 0xa2a, b = option ? 0xa2c : 0xa29;
    const int y = option ? 0xac : 0x63;
    s.plates[1].sprite = a;
    s.plates[0].sprite = b;
    pop_timer_draw(ui, s.plates[1], sprite_width(ui, a) / 2 + 0x179, sprite_height(ui, a) / 2 + y,
                   5);
    if (pop_timer_draw(ui, s.plates[0], sprite_width(ui, b) / 2 + 0x179,
                       sprite_height(ui, b) / 2 + y, 0x102)) {
        return;
    }
    if (option) {
        const int id = item_option_laser_reduction(t, item) > 0.0f ? 0xa33 : pictures[s.tab & 7];
        sprite_draw_at(ui, id, 378, 201, 0x103);
        part_card_draw(ui, t, item, 0x235, 0xca, 0x15, 0x1c4, 0x7d, 0x104, false);
    } else {
        sprite_draw_at(ui, pictures[s.tab & 7], 378, 244, 0x103);
        part_card_draw(ui, t, item, 0x230, 0xf6, 0x15, 0x1c4, 0x7d, 0x104, false);
    }
    if (item.kind == 4 || item.kind == 6 || item.kind == 7) {
        sprite_draw_at(ui, 0xa2b, 445, 119, 0x106);
    }
}

// while the list's frame pops the rows wait, but the card's plates pop
// with it
// decomp: stock_list_draw 0x3D7F2
void list_draw(FrontEnd& fe, Ui& ui, StockScreen& s) {
    const World& w = *fe.world;
    const bool ready = !pop_timer_draw(ui, s.frame, sprite_width(ui, 0xa27) / 2 + 0x28,
                                       sprite_height(ui, 0xa27) / 2 + 0x76, 0x105);
    const GaragePage& page = s.pages[s.tab];
    const int count = static_cast<int>(page.entries.size());
    if (ready) {
        icon_on_plate_draw(ui, 0x119, 0x17c, 0x107, 0xc3c, 0x13);
        sprite_draw_at(ui, titles[s.tab], 117, 122, 0x106);
        if (count > 6) {
            if (s.scroll) {
                sprite_draw_at(ui, 0x9f5, 184, 136, 0x107, 0xffffffff, 0x4000);
            }
            if (count > s.scroll + 6) {
                sprite_draw_at(ui, 0x9f4, 184, 371, 0x107, 0xffffffff, 0x4000);
            }
        }
        if (count > 0) {
            sprite_draw_at(ui, 0xa28, 43, static_cast<float>(s.cursor) * 38.0f + 144.0f, 0x106,
                           0xffffffff, 0x2000);
        }
    }
    for (int i = 0; ready && i < count && i < 6; ++i) {
        const GarageEntry& e = page.entries[static_cast<std::size_t>(s.scroll + i)];
        const float y = 156.0f + 38.0f * i;
        GlyphText name = e.name ? e.name->glyphs : GlyphText{-1};
        if (s.tab == 4) {
            name = glyph_name_text(w.garages.inventory[e.item].name, glyph_name_length);
        }
        text_draw(ui, name, 0x51, static_cast<int>(y), 7, 18, 18, 0xfff0f0f0, 0x2d);
        if (s.tab == 6 || s.tab == 7) {
            bool used = false;
            for (const Garage& g : w.garages.garages) {
                used = used || (s.tab == 6 ? g.color == e.id : g.sticker == e.id);
            }
            if (used) {
                sprite_draw_at(ui, 0x9f7, 321, y, 0x108, 0xffffd300);
            }
            continue;
        }
        if (e.garage >= 0 && e.garage < 10) {
            sprite_draw_at(ui, 0x9f8 + e.garage, 321, y, 0x108, 0xffffd300);
            if (s.tab == 2 || s.tab == 3) {
                sprite_draw_at(ui, e.slot == 0 ? 0xa1c : 0xa14, 297, y, 0x108);
            }
        }
        if (e.klass >= 0 && e.klass < 6) {
            sprite_draw_at(ui, class_icons[e.klass], 50, 152.0f + 38.0f * i, 0x108);
        }
    }
    if (s.cursor + s.scroll < count) {
        const GarageEntry& e = page.entries[static_cast<std::size_t>(s.scroll + s.cursor)];
        InventoryItem item;
        if (s.tab >= 6) {
            item.kind = static_cast<std::int16_t>(s.tab);
            item.part = static_cast<std::int16_t>(e.id);
        } else {
            item = w.garages.inventory[e.item];
        }
        card_draw(fe, ui, s, item);
    }
}

// stock_stand_fit_part: the part under the cursor onto the stand (model_viewer_fit_part: the
// body, legs, arms and backs tabs; the others show none)
void stand_fit(FrontEnd& fe, Ui& ui, StockScreen& s) {
    const GaragePage& page = s.pages[s.tab];
    const int row = s.scroll + s.cursor;
    stand_fit_part(ui, fe.shop.stand, s.tab,
                   row < static_cast<int>(page.entries.size())
                       ? page.entries[static_cast<std::size_t>(row)].id
                       : -1);
}

void draw(FrontEnd& fe, Ui& ui, StockScreen& s, bool tabs, bool list) {
    header_draw(fe, ui, s, tabs);
    if (list) {
        list_draw(fe, ui, s);
        // the tab's stand hook (stock_stand_draw: the fitted part growing in)
        stand_draw(ui, fe.shop.stand);
    }
    text_strip_draw_scroll(ui, s.help, 0xfff0f0f0);
}

} // namespace

void stock_spawn(FrontEnd& fe, Ui&) {
    fe.screen.stock.from = fe.previous;
}

void stock_step(FrontEnd& fe, Ui& ui) {
    StockScreen& s = fe.screen.stock;
    switch (fe.screen.step) {
    case 0:
        texture_set_load(ui, common_asset);
        texture_set_load(ui, stock_asset);
        texture_set_load(ui, module_asset);
        level_meter_spawn(ui, fe.screen.meter);
        garage_pages_build(fe.shop.tables, fe.world->garages, s.pages);
        garage_pages_mark_fitted(fe.world->garages, s.pages);
        slide_init(s.tabs, 5, top_start);
        slide_init(s.bottom_tabs, 5, bottom_start);
        slide_start(ui, s.tabs);
        slide_start(ui, s.bottom_tabs);
        state_music_play(fe, ui, 0x26);
        stand_start(ui, fe.shop.stand, 1); // model_viewer mode 1
        s.frames = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
        fade_in_start(ui, static_cast<float>(s.frames));
        fe.screen.step = 1;
        break;
    case 1:
        draw(fe, ui, s, false, false);
        if (--s.frames < 0) {
            now_playing_spawn(ui, fe.screen.strip, 0, 0x1a3, 0x17);
            fe.screen.step = 2;
        }
        break;
    case 2: { // the tabs: a 5 x 2 grid whose last column has only its bottom cell
        menu_cursor(ui, s.tab_row, s.tab_col == 4 ? 1 : 0, s.tab_col == 4 ? 1 : 2, 0x16, 0, 3);
        if (menu_cursor(ui, s.tab_col, 0, 5, 0x17, 0, 4) && s.tab_col == 4) {
            s.tab_row = 1;
        }
        // PC: the pointer lights the tab it enters (the last column has only its bottom cell)
        PointerRect cells[10];
        for (int col = 0; col < 5; ++col) {
            cells[col * 2] =
                col == 4 ? PointerRect{}
                         : sprite_rect(ui, top_ids[col], static_cast<float>(s.tabs.pos[col]), 212);
            cells[col * 2 + 1] =
                sprite_rect(ui, bottom_ids[col], static_cast<float>(s.bottom_tabs.pos[col]), 257);
        }
        int cell = s.tab_col * 2 + s.tab_row;
        if (menu_cursor(ui, cell, 0, 10, cursor_no_dpad, bank_sys, se_menu_cursor_horizontal,
                        cells)) {
            s.tab_col = cell / 2;
            s.tab_row = cell % 2;
        }
        s.tab = tab_map[s.tab_col * 2 + s.tab_row];
        draw(fe, ui, s, true, false);
        if (menu_accept(ui, &cells[s.tab_col * 2 + s.tab_row])) {
            ui_sound(ui, bank_sys, se_menu_accept);
            if (s.tab == 8) {
                s.frames = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
                fade_out_start(ui, static_cast<float>(s.frames));
                fe.screen.step = 4;
                break;
            }
            s.cursor = s.scroll = 0;
            pop_timer_init(s.frame, 0xa27, 5, 10);
            pop_timer_init(s.plates[0], 0xa2c, 6, 12);
            pop_timer_init(s.plates[1], 0xa2d, 6, 12);
            for (PopTimer* timer : {&s.frame, &s.plates[0], &s.plates[1]}) {
                pop_timer_start(ui, *timer, 1.0f, 1.0f, false, 0, 0);
            }
            blurb(ui, s);
            stand_fit(fe, ui, s);
            fe.screen.step = 3;
        } else if (menu_cancel(ui) && s.tab != 8) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
            s.tab_col = 4;
            s.tab_row = 1;
        }
        break;
    }
    case 3: { // the list
        GaragePage& page = s.pages[s.tab];
        int moved = 0;
        const int count = static_cast<int>(page.entries.size());
        menu_list_move(ui.pad, s.cursor, s.scroll, 0, count, 6, 0x12, moved);
        // PC: entering a row lights it, the arrows scroll
        PointerRect rows[6];
        for (int i = 0; i < 6; ++i) {
            rows[i] = sprite_rect(ui, 0xa28, 43, 144.0f + 38.0f * static_cast<float>(i));
        }
        const PointerRect up = sprite_rect(ui, 0x9f5, 184, 136);
        const PointerRect down = sprite_rect(ui, 0x9f4, 184, 371);
        if (moved == 0) {
            moved = menu_list_pointer(ui, s.cursor, s.scroll, count, 6, 0x12, rows,
                                      count > 6 ? &up : nullptr, count > 6 ? &down : nullptr);
        }
        if (moved) {
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            menu_input_mark(ui);
            blurb(ui, s);
            stand_fit(fe, ui, s);
        }
        stand_update(ui, fe.shop.stand);
        draw(fe, ui, s, false, true);
        if (menu_cancel(ui)) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            help_strip_init(ui, s.help, std::string());
            stand_hide(fe.shop.stand); // viewer.pending = -1
            fe.screen.step = 2;
        } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_x)) {
            // the stock rule: a new column sorts with flag 0 (names Z -> A, prices low -> high)
            ui_sound(ui, bank_sys, se_menu_accept);
            garage_page_sort(page, 0, page.column != 0 ? false : !page.descending);
            stand_fit(fe, ui, s); // viewer.stale
        } else if (pad_button_test(ui.pad, PadEdge::Pressed, PadMatch::Any, pad_y)) {
            ui_sound(ui, bank_sys, se_menu_accept);
            garage_page_sort(page, 1, page.column != 1 ? false : !page.descending);
            stand_fit(fe, ui, s);
        }
        break;
    }
    case 4: // the fade out, back to the hub
        draw(fe, ui, s, false, false);
        if (--s.frames < 0) {
            ui_music_stop(ui);
            task_header_set(fe, 0);
            fe.screen.step = 5;
        }
        break;
    default:
        draw(fe, ui, s, false, false);
        break;
    }
}

void stock_destroy(FrontEnd& fe, Ui& ui) {
    stand_hide(fe.shop.stand);
    ui_music_stop(ui);
    ui.clear_colour = 0xff000000;
}
