// The part cards: part_card_draw (src/o1/u_03c67e.c), the editor's card
// garage_part_card_draw and garage_stat_compare_draw (src/hand/garage.c).
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

namespace {

// part_card_stat_flags (0x17BDDC in the XBE): the rows a card shows per kind
constexpr std::uint32_t card_flags[8] = {0x4b, 0x3f, 0x783, 0x783, 0x803, 0xf00a, 0x10000, 0x20000};
// frame_cadence_table[6] (D_002559E0): a recharge's frames to seconds
constexpr float recharge_rate = 30.0f;
constexpr float video_rate = 60.0f;
constexpr float list_x = 40.0f, list_y = 118.0f;

// the after value, coloured and marked by its direction
// decomp: stat_compare_number_draw 0x179ED
void compare_number(Ui& ui, int blend, float before, float after, float x, float y, bool mode,
                    bool mode2) {
    int sprite = -1;
    std::uint32_t colour = 0xfff0f0f0;
    if (before > after) {
        sprite = 0xe12;
        colour = 0xffff8080;
    } else if (before < after) {
        sprite = 0xe13;
        colour = 0xff8080ff;
    }
    const auto whole = static_cast<long long>(after);
    if (mode && mode2) {
        number_draw_full(ui, 0x68, whole, after, static_cast<int>(x), static_cast<int>(y), blend,
                         colour, 6, 3, 1, 0);
    } else if (mode) {
        number_draw_full(ui, 0x48, whole, after, static_cast<int>(x), static_cast<int>(y), blend,
                         colour, 3, 1, 1, 0);
    } else {
        number_draw_full(ui, 0x48, whole, after, static_cast<int>(x), static_cast<int>(y), blend,
                         colour, 8, 0, 1, 0);
    }
    if (sprite >= 0) {
        sprite_draw_at(ui, sprite, x + 24.0f, y, blend, colour);
    }
}

// stat_delta_up_draw / stat_delta_down_draw: a negative value draws only 0xE78
// decomp: stat_delta_up_draw 0x19018, stat_delta_down_draw 0x190AE
void compare_delta(Ui& ui, int blend, float after, float before, float x, float y, bool mode,
                   bool up) {
    if (after >= 0.0f) {
        compare_number(ui, blend, before, after, x, y, mode, up);
    } else {
        sprite_draw_at(ui, 0xe78, x + 112.0f - 30.0f + 36.0f, y - 8.0f, blend);
    }
}

// after (the working loadout with the entry toggled) against
// before (the working loadout), rows by the item's kind
// decomp: garage_stat_compare_draw 0x19489
void stat_compare_draw(Ui& ui, const PartTables& t, const World& w, const Garage& after,
                       const Garage& before, const InventoryItem& item, const GarageEntry& entry,
                       int x, int y, int dy, int tx, int ty, int blend) {
    const Garages& g = w.garages;
    // D_001DAF68 / D_001DAF7C by kind; the option row's 0x40 is laser reduction's
    std::uint32_t flags = 0;
    const std::uint32_t table[8] = {0x0f, 7, 3, 3, 0x603, 0x27, 7, 7};
    if (item.kind >= 0 && item.kind < 8) {
        flags = table[item.kind];
    }
    if (item.kind == 5) {
        flags = item_option_laser_reduction(t, item) > 0.0f ? flags | 0x40 : flags & ~0x40u;
    }
    const float fx = static_cast<float>(x);
    if (flags & 1) {
        compare_delta(ui, blend, loadout_weight(t, g, after) / 100.0f,
                      loadout_weight(t, g, before) / 100.0f, fx, static_cast<float>(y), true, true);
        y += dy;
    }
    if (flags & 2) {
        compare_delta(ui, blend, loadout_leg_capacity(t, g, after) / 100.0f,
                      loadout_leg_capacity(t, g, before) / 100.0f, fx, static_cast<float>(y), true,
                      true);
        y += dy;
    }
    if (flags & 4) {
        compare_delta(ui, blend, loadout_durability(t, g, after), loadout_durability(t, g, before),
                      fx, static_cast<float>(y), false, true);
        y += dy;
    }
    if (flags & 8) {
        compare_delta(ui, blend, static_cast<float>(loadout_body_sockets(t, g, after)),
                      static_cast<float>(loadout_body_sockets(t, g, before)), fx,
                      static_cast<float>(y), false, true);
        y += dy;
    }
    if (flags & 0x40) {
        compare_delta(ui, blend, loadout_laser_reduction(t, g, after),
                      loadout_laser_reduction(t, g, before), fx, static_cast<float>(y), false, true);
        y += dy;
    } else if (flags & 0x20) {
        const auto seconds = [](int frames) {
            const float s = static_cast<float>(frames) / video_rate;
            return s < 0.0f ? 0.0f : s;
        };
        compare_delta(ui, blend, seconds(loadout_camo_recharge(t, g, after)),
                      seconds(loadout_camo_recharge(t, g, before)), fx, static_cast<float>(y), true,
                      false);
        compare_delta(ui, blend, static_cast<float>(loadout_camo_frames(t, g, after)) / video_rate,
                      static_cast<float>(loadout_camo_frames(t, g, before)) / video_rate,
                      static_cast<float>(x - 0x55), static_cast<float>(y), true, false);
        y += dy;
    }
    // the Chip rows read the Chip already fitted, else the candidate (retail's)
    const InventoryItem* chip = before.chip >= 0 && before.chip < 0xff ? &g.inventory[before.chip]
                                                                        : &item;
    if (flags & 0x200) {
        const int v = chip->tune < 0 ? -chip->tune : chip->tune;
        number_draw_full(ui, 0x48, v, static_cast<float>(v), x, y, blend, 0xfff0f0f0, 2, 0, 1, 0);
        y += dy;
    }
    if (flags & 0x400) {
        GlyphText name = chip->name[0] != -1 ? glyph_name_text(chip->name, glyph_name_length)
                                             : (entry.name ? entry.name->glyphs : GlyphText{-1});
        text_draw(ui, name, tx, ty, blend - 0x101, 18, 18, 0xfff0f0f0, 9);
    }
}

} // namespace

// decomp: part_card_draw 0x3CB43
void part_card_draw(Ui& ui, const PartTables& t, const InventoryItem& card, int x, int y, int line,
                    int a, int b, int blend, bool compact) {
    if (card.kind < 0 || card.kind >= 8) {
        return;
    }
    const std::uint32_t flags = card_flags[card.kind];
    const std::uint32_t white = 0xfff0f0f0;
    const auto num = [&](std::uint32_t f, float v, int digits, int decimals, int at_x, int at_y) {
        number_draw_full(ui, f, static_cast<long long>(v), v, at_x, at_y, blend, white, digits,
                         decimals, 1, 0);
    };
    int row = y;
    if (flags & 1) {
        if (flags & 0x800) {
            sprite_draw_at(ui, item_chip_identity_sprite(t, card), static_cast<float>(a),
                           static_cast<float>(b), blend + 1);
        }
        const int tune = card.tune;
        const int size = tune < 0 ? -tune : tune;
        if (card.kind != 4) {
            const int sign = tune < 0 ? 0xc0d : tune == 0 ? 0xc0e : 0xc0c;
            const std::uint32_t colour = tune < 0 ? 0xff00d3ff : tune == 0 ? 0xffffd300 : 0xffff0000;
            sprite_draw_at(ui, sign, static_cast<float>(x) - 85.0f, static_cast<float>(y), blend + 1,
                           colour);
        }
        number_draw_full(ui, 0x48, size, static_cast<float>(size), x, y, blend, white, 2, 0, 1, 0);
        row = y + line;
    }
    if (flags & 2) {
        num(0x68, item_weight(t, card) * 0.01f, 3, 3, x, row);
        row += line;
    }
    if ((flags & 4) && !compact) {
        num(0x68, item_leg_capacity(t, card) * 0.01f, 3, 3, x, row);
        row += line;
    }
    if (flags & 8) {
        float v = item_durability(t, card);
        // part_durability_floor_apply
        if (card.kind == 0 && body_part(t, card.part)) {
            v = std::max(v, body_durability_floor(body_part(t, card.part)->body_class & 0xff));
        } else if (card.kind == 1 && leg_part(t, card.part)) {
            v = std::max(v, leg_durability_floor(leg_part(t, card.part)->geometry));
        }
        num(0x68, v, 6, 0, x, row);
        row += line;
    }
    if (flags & 0x40) {
        num(0x68, static_cast<float>(item_body_sockets(t, card)), 6, 0, x, row);
        row += line;
    }
    if (flags & 0x10) {
        num(0x68, item_leg_speed(t, card), 6, 0, x, row);
        row += line;
    }
    if (flags & 0x20) {
        num(0x68, item_leg_jump(t, card), 6, 0, x, row);
        row += line;
    }
    if (flags & 0x80) {
        // weapon_resource_floor_apply (the item's resource already holds its floor)
        num(0x68, static_cast<float>(static_cast<int>(item_weapon_resource(t, card))), 6, 0, x, row);
        row += line;
    }
    if (flags & 0x100) {
        num(0x68, static_cast<float>(item_weapon_shots(t, card)), 6, 0, x, row);
        row += line;
    }
    if (flags & 0x200) {
        num(0x68, static_cast<float>(item_weapon_recharge(t, card)) / recharge_rate, 3, 1, x, row);
        row += line;
    }
    if (flags & 0x400) {
        int v = item_weapon_damage(t, card);
        if (const WeaponPart* w = weapon_part(t, card.kind, card.part)) {
            v = v < w->damage_min ? w->damage_min : v; // weapon_damage_floor_apply
        }
        num(0x68, static_cast<float>(v), 6, 0, x, row);
        row += line;
    }
    if (flags & 0x1000) {
        num(0x68, static_cast<float>(item_option_sockets(t, card)), 6, 0, x, row);
        row += line;
    }
    if (item_option_laser_reduction(t, card) > 0.0f) {
        if (flags & 0x8000) {
            num(0x68, item_option_laser_reduction(t, card), 6, 0, x, row);
        }
    } else {
        if (flags & 0x2000) {
            num(0x68, static_cast<float>(item_option_camo_frames(t, card)) / video_rate, 3, 1,
                compact ? x - 0x55 : x, row);
            if (!compact) {
                row += line;
            }
        }
        if (flags & 0x4000) {
            const float v = static_cast<float>(item_option_camo_recharge(t, card)) / video_rate;
            number_draw_full(ui, 0x69, static_cast<long long>(v), v, x, row, blend, white, 3, 1, 1,
                             0);
        }
    }
    if (flags & 0x10000) {
        sprite_draw_at(ui, item_color_sprite(t, card), static_cast<float>(a), static_cast<float>(b),
                       blend + 1);
    }
    if (flags & 0x20000) {
        sprite_draw_at(ui, item_sticker_sprite(t, card), static_cast<float>(a),
                       static_cast<float>(b), blend + 1);
    }
}

void assemble_card_draw(FrontEnd& fe, Ui& ui, AssembleScreen& s) {
    const PartTables& t = fe.shop.tables;
    const World& w = *fe.world;
    GarageEditor& e = s.editor;
    const int elapsed = task_timer_get(ui) - s.card_start;
    float fade = static_cast<float>(static_cast<unsigned>(elapsed)) * 0.125f;
    fade = fade < 0.0f ? 0.0f : fade > 1.0f ? 1.0f : fade;
    assemble_list_frame_draw(ui, s, 1);
    const int category = e.category;
    if (fade >= 1.0f) {
        icon_on_plate_draw(ui, 0x119, 0xbd, 0x106, 0xc3c, 0x13);
        sprite_draw_at(ui, 0x3d0, list_x + 3.0f, list_y + 26.0f, 0x107, 0xffffffff, 0x2000, fade);
        sprite_draw_at(ui, 0x3cf, list_x + 11.0f, list_y + 46.0f, 0x106, 0xffffffff, 0, fade);
        int title = 0;
        if (category == 2) {
            title = e.side == 0 ? 0x42f : 0x42a;
        } else if (category == 3) {
            title = e.side == 0 ? 0x430 : 0x42b;
        } else {
            title = static_cast<int>(ui_values(ui.data, "garage", "garage_category_places",
                                               16)[static_cast<std::size_t>(category * 2)]);
        }
        sprite_draw_at(ui, title, 117, 122, 0x107, 0xffffffff, 0, fade);
    }
    GaragePage& page = e.pages[category];
    const int row = e.scroll + e.cursor;
    if (row < 0 || row >= static_cast<int>(page.entries.size())) {
        return;
    }
    GarageEntry& entry = page.entries[static_cast<std::size_t>(row)];
    if (category == 2 || category == 3) {
        entry.slot = e.side;
    }
    InventoryItem item;
    if (category >= 6) {
        item.kind = static_cast<std::int16_t>(category);
        item.part = static_cast<std::int16_t>(entry.id);
    } else if (entry.item >= 0 && entry.item < inventory_size) {
        item = w.garages.inventory[entry.item];
    }
    const int fit = garage_fit_state(e, entry);
    const std::uint32_t colour = fit == 3 ? 0xffffffff : 0xffffd300;
    const std::uint32_t text = fit == 3 ? 0xfff0f0f0 : 0xffffd300;
    GlyphText name = entry.name ? entry.name->glyphs : GlyphText{-1};
    if (category == 4 && item.name[0] != -1) {
        name = glyph_name_text(item.name, glyph_name_length);
    }
    if (fade >= 1.0f) {
        text_draw(ui, name, static_cast<int>(list_x) + 0x29, static_cast<int>(list_y) + 0x26, 5, 18,
                  18, text, 0x2d);
    }
    const int labels[8] = {0x41f, 0x422, 0x426, 0x426, 0x420, 0x423, 0x421, 0x425};
    sprite_draw_at(ui, 0x41e, 14, 188, 0x105, 0xffffffff, 0, fade * 0.7f);
    const bool laser = item_option_laser_reduction(t, item) > 0.0f;
    sprite_draw_at(ui, laser ? 0x424 : labels[category], 51, 198, 0x106, 0xffffffff, 0, fade);
    if (entry.garage == e.garage) {
        sprite_draw_at(ui, 0x3ce, list_x + 29.0f, list_y + 14.0f, 0x109, 0xff808080, 0, fade);
        sprite_draw_at(ui, 0x3ce, list_x + 29.0f, list_y + 14.0f, 0x10a, 0xffffd300, 0x2000, fade);
    }
    garage_row_marks_draw(ui, t, w.garages, e, e.garage, entry, fit, list_y + 38.0f, colour);
    if (fade >= 1.0f) {
        const int at = category == 4 ? 0x105 : category == 5 ? 0x13f : 0x136;
        const int x = category == 5 ? 0x13f : 0x136;
        part_card_draw(ui, t, item, at, 0xe3, 0x15, 0x122, 0xf4, 0x106, true);
        Garage after = e.working;
        loadout_toggle_part(after, category, entry.item, entry.slot);
        const std::vector<double> ys = ui_values(ui.data, "garage", "part_card_rows_y", 12);
        stat_compare_draw(ui, t, w, after, e.working, item, entry, x,
                          static_cast<int>(ys[static_cast<std::size_t>(category)]), 0x15, 0xbb, 0x187,
                          0x106);
    }
}
