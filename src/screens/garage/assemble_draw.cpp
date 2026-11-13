// The parts editor's drawing (decomp src/hand/garage.c: garage_screen_draw,
// garage_list_draw and the row drawers 0x1A32A..0x1AB89, garage_row_frame_draw,
// garage_side_tabs_draw, garage_frame_draw, garage_list_frame_slide_in).
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

namespace {

// the list's origin (D_0017BDA4, D_0017BDA8) and the grid (retail .rdata, read from the XBE:
// D_0017BDFC / D_0017BE14 the slides' starts, D_0017BE2C the cells' x, D_0017BE5C / D_0017BE74
// the cells' sprites, D_0017BE8C the lit cells [row][column])
constexpr float list_x = 40.0f, list_y = 118.0f;
constexpr int top_start[6] = {-488, -403, -318, -233, -148, -63};
constexpr int bottom_start[6] = {-558, -468, -378, -288, -198, -108};
constexpr int cell_x[6] = {72, 117, 162, 207, 252, 297};
constexpr int top_ids[6] = {1007, 995, 1001, 1027, 1025, 1035};
constexpr int bottom_ids[6] = {1023, 1010, 1012, 1037, 1014, 1018};
constexpr int lit_ids[2][6] = {{1008, 996, 1002, 1028, 1026, 1036},
                               {1024, 1011, 1013, 1038, 1015, 1019}};
// assemble_grid_categories: by column * 2 + row
constexpr int grid_categories[12] = {0, 1, 2, 4, 3, 6, 5, 7, 8, 9, 10, 11};

const InventoryItem* inventory_item(const World& w, int index) {
    return index >= 0 && index < inventory_size ? &w.garages.inventory[index] : nullptr;
}

std::uint32_t row_colour(const PartTables& t, const World& w, const GarageEditor& e,
                         const GarageEntry& entry, int category) {
    const bool ok = garage_admit(t, w.garages, e, entry);
    // the Chip and option rows test gold first, the rest grey first
    if (category == 4 || category == 5) {
        return entry.garage == e.garage ? 0xffffd300 : !ok ? 0xff7f7f7f : 0xfff0f0f0;
    }
    return !ok ? 0xff7f7f7f : entry.garage == e.garage ? 0xffffd300 : 0xfff0f0f0;
}

// the row drawers (D_001DAE38 by category)
void rows_draw(FrontEnd& fe, Ui& ui, AssembleScreen& s, int count, float x, float y) {
    const PartTables& t = fe.shop.tables;
    const World& w = *fe.world;
    GarageEditor& e = s.editor;
    const GaragePage& page = e.pages[e.category];
    const int limits[8] = {0x10, 0x2b, 0x4c, 0x45, 0x60, 0x6b, 0x39, 0xce};
    for (int i = 0; i < count; ++i) {
        const int row = e.scroll + i;
        if (row < 0 || row >= static_cast<int>(page.entries.size())) {
            break;
        }
        const GarageEntry& entry = page.entries[static_cast<std::size_t>(row)];
        if (entry.id < 0 || entry.id >= limits[e.category]) {
            continue;
        }
        std::uint32_t colour = 0xfff0f0f0;
        int garage = entry.garage;
        int mode = 3;
        if (e.category == 6 || e.category == 7) {
            const bool current = e.category == 6 ? entry.item == e.working.color
                                                 : static_cast<std::int8_t>(entry.item) ==
                                                       static_cast<std::int8_t>(e.working.sticker);
            colour = current ? 0xffffd300 : 0xfff0f0f0;
            garage = e.garage;
            mode = current ? 0 : 3;
        } else {
            colour = row_colour(t, w, e, entry, e.category);
            if (entry.garage != -1) {
                mode = e.category == 2 || e.category == 3 ? (entry.slot == 1) + 1 : 0;
            }
        }
        garage_row_marks_draw(ui, t, w.garages, e, garage, entry, mode, y, colour);
        GlyphText name = entry.name ? entry.name->glyphs : GlyphText{-1};
        const InventoryItem* item = e.category == 4 ? inventory_item(w, entry.item) : nullptr;
        if (item && item->name[0] != -1) {
            name = glyph_name_text(item->name, glyph_name_length);
        }
        text_draw(ui, name, static_cast<int>(x), static_cast<int>(y), 7, 18, 18, colour, 0x2d);
        y += 38.0f;
    }
}

} // namespace

int assemble_category(int column, int row) {
    return grid_categories[(column * 2 + row) % 12];
}

PointerRect assemble_row_rect(int i) {
    const float y = list_y + 32.0f + 14.0f + 38.0f * static_cast<float>(i);
    return {list_x + 3.0f, y - 4.0f, list_x + 240.0f, y + 34.0f};
}

PointerRect assemble_cell_rect(Ui& ui, const AssembleScreen& s, int row, int column) {
    const int x = row == 0 ? s.top.pos[column] : s.bottom.pos[column];
    return sprite_rect(ui, lit_ids[row][column], static_cast<float>(x), 212.0f + 45.0f * row);
}

void garage_row_marks_draw(Ui& ui, const PartTables& t, const Garages& g, const GarageEditor& e,
                           int garage, const GarageEntry& entry, int mode, float y,
                           std::uint32_t color) {
    if (mode == 3) {
        sprite_draw_at(ui, 0x3d7, list_x + 281.0f, y, 0x108);
    } else {
        // the garage's number, lit when it is this one's and the part may be taken
        std::uint32_t number = 0xff808080;
        if (garage == e.garage && garage_admit(t, g, e, entry)) {
            number = color;
        }
        sprite_draw_at(ui, 0x3d7, list_x + 281.0f, y, 0x106);
        if (garage >= 0 && garage < 10) {
            sprite_draw_at(ui, 0x3d8 + garage, list_x + 281.0f, y, 0x107, number);
        }
        if (mode == 1) {
            sprite_draw_at(ui, 0x407, list_x + 257.0f, y, 0x108, color);
            if (garage == e.garage) {
                sprite_draw_at(ui, 0x406, list_x + 257.0f, y, 0x109, color);
            }
        } else if (mode == 2) {
            sprite_draw_at(ui, 0x3fe, list_x + 257.0f, y, 0x108, color);
            if (garage == e.garage) {
                sprite_draw_at(ui, 0x3fd, list_x + 257.0f, y, 0x109, color);
            }
        }
    }
    // the maker's icon, classes 0..5
    const int icons[6] = {0x3fc, 0x405, 0x3e2, 0x410, 0x40f, 0x3f1};
    if (entry.klass >= 0 && entry.klass < 6) {
        sprite_draw_at(ui, icons[entry.klass], list_x + 10.0f, y - 4.0f, 0x106);
    }
}

void assemble_frame_timers(Ui& ui, AssembleScreen& s, int which) {
    const int first = which == 1 ? 3 : 0;
    pop_timer_init(s.frame[first], 0x3d6, 5, 10);
    pop_timer_init(s.frame[first + 1], 0x3cc, 5, 10);
    pop_timer_init(s.frame[first + 2], 0x3cd, 5, 10);
    // garage_list_frame_slide_in: all three from one point, the middle stretched to fit
    const int x = sprite_width(ui, 0x3cc) / 2 + static_cast<int>(list_x);
    const int h_top = sprite_height(ui, 0x3d6), h_mid = sprite_height(ui, 0x3cc);
    int y = 0;
    float stretch = 1.0f;
    if (which == 1) {
        y = h_top + static_cast<int>(list_y) + 0x3e;
        stretch = 48.0f / static_cast<float>(h_mid);
    } else {
        y = static_cast<int>(static_cast<unsigned>(0x183 - static_cast<int>(list_y) - h_top) >> 1) +
            h_top + static_cast<int>(list_y) + 0x26;
        stretch = (387.0f - (static_cast<float>(h_top) + list_y)) / static_cast<float>(h_mid);
    }
    pop_timer_start(ui, s.frame[first], 1.0f, 1.0f, true, x, y);
    pop_timer_start(ui, s.frame[first + 1], 1.0f, stretch, true, x, y);
    pop_timer_start(ui, s.frame[first + 2], 1.0f, 1.0f, true, x, y);
}

bool assemble_list_frame_draw(Ui& ui, AssembleScreen& s, int mode) {
    const int first = mode == 1 ? 3 : 0;
    const int x = sprite_width(ui, 0x3d6) / 2 + static_cast<int>(list_x);
    const int h_top = sprite_height(ui, 0x3d6);
    pop_timer_draw(ui, s.frame[first], x, h_top / 2 + static_cast<int>(list_y), 0x105);
    float y = static_cast<float>(h_top) + list_y;
    const int v = mode == 1 ? static_cast<int>(y + 24.0f) : static_cast<int>((387.0f - y) * 0.5f + y);
    const bool running = pop_timer_draw(ui, s.frame[first + 1], x, v, 0x105);
    y = mode == 1 ? y + 48.0f : 386.0f;
    pop_timer_draw(ui, s.frame[first + 2], x, sprite_height(ui, 0x3cd) / 2 + static_cast<int>(y),
                   0x105);
    return running;
}

void assemble_help_set(Ui& ui, AssembleScreen& s, int set) {
    // D_001DAF88: {first string of D_001DADC0, count} per set
    const int first[5] = {0, 12, 14, 16, 17}, count[5] = {12, 2, 2, 1, 1};
    const UiTable* lines = ui_table(ui.data, "garage", "assemble_lines");
    if (set < 0 || set > 4) {
        return;
    }
    for (int i = 0; i < count[set]; ++i) {
        const std::size_t at = static_cast<std::size_t>(first[set] + i);
        help_strip_init(ui, s.help[i],
                        lines && at < lines->strings.size() ? lines->strings[at] : std::string());
    }
}

void assemble_frame_draw(FrontEnd& fe, Ui& ui, AssembleScreen& s, int flip) {
    const World& w = *fe.world;
    const PartTables& t = fe.shop.tables;
    GarageEditor& e = s.editor;
    day_draw(ui, w, 0x108);
    money_draw(ui, w, 0x108);
    const std::vector<double> plates = ui_values(ui.data, "garage", "garage_plates", 10);
    sprite_draw_at(ui, static_cast<int>(plates[static_cast<std::size_t>(e.garage % 10)]), 41, 121,
                   0x102);
    const int used = loadout_option_socket_sum(t, w.garages, e.working);
    const int sockets = loadout_body_sockets(t, w.garages, e.working);
    number_draw_full(ui, 0x48, used, static_cast<float>(used), 0x224, 0x84, 0x106, 0xfff0f0f0, 2,
                     0, 1, 0);
    number_draw_full(ui, 0x48, sockets, static_cast<float>(sockets), 0x23e, 0x84, 0x106,
                     0xfff0f0f0, 2, 0, 1, 0);
    if (w.garages.selected != -1) {
        garage_banner_draw(ui, w, s.banner, 0x17a, 0x101);
    }
    // the grid: two slides of six cells, the lit cell, the cells that are off
    slide_draw(ui, s.top, 6, 0x18, 0x101, 0xd4, cell_x, top_ids);
    slide_draw(ui, s.bottom, 6, 0x18, 0x101, 0x101, cell_x, bottom_ids);
    const int lit_x = s.row == 0 ? s.top.pos[s.column] : s.bottom.pos[s.column];
    sprite_draw_at(ui, lit_ids[s.row][s.column], static_cast<float>(lit_x),
                   static_cast<float>(s.row) * 45.0f + 212.0f, 0x102, 0xffffd300,
                   flip & 1 ? 0x2000 : 0);
    const std::vector<double> points = ui_values(ui.data, "garage", "assemble_points", 26);
    for (int category = 0; category < 12; ++category) {
        if (!s.available[category]) {
            sprite_draw_at(ui, 0x40a, static_cast<float>(points[1 + category * 2]),
                           static_cast<float>(points[2 + category * 2]), 0x103);
        }
    }
    if (flip & 2) {
        sprite_draw_at(ui, 0x408, 72, 212, 0x104);
    }
    sprite_rows_draw(ui, ui_slots(ui.data, "garage", "garage_sprites"));
    text_draw(ui, glyph_name_text(e.working.name, glyph_name_length), 0x1bc, 0x109, 1, 18, 18,
              0xfff0f0f0, 9);
    sprite_draw_at(ui, 0x3d4, 349, 235, 0x101);
    sprite_draw_at(ui, 0x3d5, 450, 307, 0x102);
    sprite_draw_at(ui, 0x3d5, 382, 307, 0x102);
    // the sticker, colour and Chip cards
    InventoryItem card;
    if (paint_part(t, 7, e.working.sticker) && e.working.sticker < sticker_count &&
        e.working.sticker != no_sticker) {
        card.kind = 7;
        card.part = e.working.sticker;
        sprite_draw_at(ui, item_sticker_sprite(t, card), 0x206, 0x133, 0x103);
    } else {
        sprite_draw_at(ui, 0x3d5, 518, 307, 0x102);
    }
    if (e.build_class < 3 || e.build_class > 6) {
        card.kind = 6;
        card.part = e.working.color;
        sprite_draw_at(ui, item_color_sprite(t, card), 0x1c2, 0x133, 0x103);
    }
    if (const InventoryItem* chip = inventory_item(w, e.working.chip)) {
        sprite_draw_at(ui, item_chip_identity_sprite(t, *chip), 0x17e, 0x133, 0x103);
    }
    stand_draw(ui, fe.shop.stand);
    // the warnings: a part missing, no weapon, overweight
    const int f = e.status;
    if (!(f & garage_empty)) {
        if (f & garage_part_missing) {
            sprite_draw_at(ui, 0x411, 432, 172, 0x101);
        } else if ((f & garage_no_weapon) && (f & garage_overweight)) {
            sprite_draw_at(ui, 0x412, 371, 178, 0x101);
            sprite_draw_at(ui, 0x413, 487, 178, 0x101);
        } else if (f & garage_no_weapon) {
            sprite_draw_at(ui, 0x412, 432, 178, 0x101);
        } else if (f & garage_overweight) {
            sprite_draw_at(ui, 0x413, 432, 178, 0x101);
        }
    }
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}

void assemble_list_draw(FrontEnd& fe, Ui& ui, AssembleScreen& s) {
    GarageEditor& e = s.editor;
    const int category = e.category;
    if (category < 0 || category >= 8) {
        return;
    }
    const GaragePage& page = e.pages[category];
    const int count = static_cast<int>(page.entries.size());
    if (assemble_list_frame_draw(ui, s, 0)) {
        return;
    }
    icon_on_plate_draw(ui, 0x119, 0x17d, 0x106, 0xc3c, 0x13);
    for (int i = 0; i < 6; ++i) {
        sprite_draw_at(ui, 0x3cf, list_x + 11.0f, list_y + 32.0f + 14.0f + 38.0f * i, 0x106);
    }
    sprite_draw_at(ui, 0x3d0, list_x + 3.0f, list_y + static_cast<float>(e.cursor) * 38.0f + 26.0f,
                   0x107, 0xffffffff, 0x20);
    if (count > 6) {
        if (e.scroll != 0) {
            sprite_draw_at(ui, 0x3cb, 184, 136, 0x108, 0xffffffff, 0x4000);
        }
        if (count > e.scroll + 6) {
            sprite_draw_at(ui, 0x3ca, 184, 371, 0x108, 0xffffffff, 0x4000);
        }
    }
    int title = 0;
    if (category == 2) {
        title = e.side == 0 ? 0x42f : 0x42a;
    } else if (category == 3) {
        title = e.side == 0 ? 0x430 : 0x42b;
    } else {
        title = static_cast<int>(ui_values(ui.data, "garage", "garage_category_places",
                                           16)[static_cast<std::size_t>(category * 2)]);
    }
    sprite_draw_at(ui, title, 117, 122, 0x107);
    rows_draw(fe, ui, s, count < 6 ? count : 6, list_x + 41.0f, list_y + 38.0f);
}

void assemble_side_tabs_draw(Ui& ui, AssembleScreen& s) {
    float t = static_cast<float>(static_cast<unsigned>(task_timer_get(ui) - s.side_start)) * 0.125f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    float spread = (t - 0.5f) * 2.0f;
    float rise = t * 2.0f;
    spread = (spread < 0.0f ? 0.0f : spread > 1.0f ? 1.0f : spread) * 22.5f;
    rise = (rise < 0.0f ? 0.0f : rise > 1.0f ? 1.0f : rise) * 45.0f;
    if (t < 1.0f) {
        menu_input_mark(ui);
    }
    const bool arms = s.editor.category == 2;
    const float centre = arms ? 117.5f : 162.5f;
    const int right = arms ? 999 : 0x3ed, left = right - 2;
    const float y = 215.0f - rise;
    sprite_draw_at(ui, right, spread + centre, y, 0x103);
    sprite_draw_at(ui, left, centre - spread, y, 0x103);
    if (s.editor.side == 1) {
        sprite_draw_at(ui, arms ? 0x3e6 : 0x3ec, centre - spread, y, 0x104, 0xffffd300, 0x2000);
    } else {
        sprite_draw_at(ui, arms ? 0x3e8 : 0x3ee, spread + centre, y, 0x104, 0xffffd300, 0x2000);
    }
}

void assemble_grid_reset(AssembleScreen& s) {
    slide_init(s.top, 6, top_start);
    slide_init(s.bottom, 6, bottom_start);
}
