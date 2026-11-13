// What the garages hold: status, the inventory, prices, a used build delivered and the hangar's
// tidy (decomp src/od/u_0b9460.c, src/o1/u_01c789.c, src/hand/curve.c).
#include "screens/garage/garage_rules.hpp"

#include <cstdlib>
#include <cstring>

bool garage_unlocked(const Garages& g, int garage) {
    return garage >= 0 && garage < garage_count && (g.unlocked & (1u << garage)) != 0;
}

// decomp: garage_status 0x1CCB3
int garage_status(const PartTables& t, const Garages& g, int i) {
    if (i < 0 || i >= garage_count) {
        return 0;
    }
    if (!garage_unlocked(g, i)) {
        return garage_locked;
    }
    int flags = g.selected == i ? garage_boarded : 0;
    const Garage& l = g.garages[i];
    const std::int16_t parts[7] = {l.body, l.leg, l.arm[0], l.arm[1], l.back[0], l.back[1], l.chip};
    int m = 0;
    for (int k = 0; k < 7; ++k) {
        if (parts[k] != -1) {
            m |= 1 << k;
        }
    }
    for (int j = 0; j < 10; ++j) {
        if (l.option[j] != -1) {
            m |= 1 << (j + 7);
        }
    }
    if (l.color != 0 && l.color < color_count) {
        m |= 0x20000; // painted
    }
    if (l.sticker != no_sticker && l.sticker < sticker_count) {
        m |= 0x40000; // a sticker
    }
    if (!m) {
        flags |= garage_empty;
    }
    // the parts mask: bit k of body, legs, arm A, arm B, back A, back B, chip, then the options
    flags |= (m & 0x43) != 0x43 ? garage_part_missing : garage_whole; // body, legs, chip
    if (!(m & 0x3c)) {                                                // no arm or back weapon
        flags |= garage_no_weapon;
    }
    if (loadout_weight(t, g, l) - loadout_leg_capacity(t, g, l) >= 0.05f) {
        flags |= garage_overweight;
    }
    if (!(flags & garage_warnings)) {
        flags |= garage_ready;
    }
    return flags;
}

// decomp: inventory_first_free 0xBAA88
int inventory_first_free(const Garages& g) {
    for (int i = 0; i < inventory_size; ++i) {
        if (g.inventory[i].part == -1) {
            return i;
        }
    }
    return -1;
}

void glyph_name_copy(GlyphName& to, const GlyphName& from) {
    std::memcpy(to, from, sizeof(GlyphName));
}

// --- prices ---

// decomp: shop_item_price 0x93247
int shop_item_price(const PartTables& t, int kind, int id, int tune) {
    const std::int8_t tune8 = static_cast<std::int8_t>(tune);
    switch (kind) {
    case 0:
        return body_part(t, id) ? static_cast<int>(curve_value(body_part(t, id)->price, tune8)) : 0;
    case 1:
        return leg_part(t, id) ? static_cast<int>(curve_value(leg_part(t, id)->price, tune8)) : 0;
    case 2:
    case 3:
        return weapon_part(t, kind, id)
                   ? static_cast<int>(curve_value(weapon_part(t, kind, id)->price, tune8))
                   : 0;
    case 4:
        return chip_part(t, id) ? static_cast<int>(curve_value(chip_part(t, id)->price, tune8)) : 0;
    case 5:
        return option_part(t, id) ? option_part(t, id)->price : 0;
    case 6:
    case 7:
        return paint_part(t, kind, id) ? paint_part(t, kind, id)->price : 0;
    default:
        return 0;
    }
}

// decomp: part_tune_price 0x2F2FE
int part_tune_price(const PartTables& t, int kind, int id, int from, int to) {
    const int p_from = shop_item_price(t, kind, id, from);
    const int p_to = shop_item_price(t, kind, id, to);
    const int base = shop_item_price(t, kind, id, 0);
    return from * to >= 0 ? std::abs(p_to - p_from) : p_to - base * 2 + p_from;
}

// decomp: shop_scoobee_total 0x3927F
int shop_scoobee_total(const PartTables& t, const ShopBuild& b) {
    int sum = shop_item_price(t, 0, b.body, b.body_tune);
    sum += shop_item_price(t, 1, b.leg, b.leg_tune);
    for (int m = 0; m < 4; ++m) {
        sum += shop_item_price(t, m < 2 ? 2 : 3, b.weapon[m], b.weapon_tune[m]);
    }
    sum += shop_item_price(t, 4, b.chip, b.chip_tune);
    sum += shop_item_price(t, 6, b.color, 0);
    sum += shop_item_price(t, 7, b.sticker, 0);
    for (const int option : b.option) {
        sum += shop_item_price(t, 5, option, 0);
    }
    return sum;
}

// decomp: wild_machines_used_deliver 0x226E2
int used_deliver(const PartTables&, Garages& g, const ShopBuild& b, int garage) {
    Garage& l = g.garages[garage];
    int chip_slot = -1;
    // item_fit: each part into the first free slot, fitted to the garage
    const auto fit = [&](int part, int tune, int kind) -> std::int16_t {
        const int at = part != -1 ? inventory_first_free(g) : -1;
        if (at < 0) {
            return -1;
        }
        InventoryItem& item = g.inventory[at];
        item.garage = static_cast<std::int8_t>(garage);
        item.tune = static_cast<std::int8_t>(tune);
        item.kind = static_cast<std::int16_t>(kind);
        item.part = static_cast<std::int16_t>(part);
        return static_cast<std::int16_t>(at);
    };
    // the body and legs take a slot even when the build has none (retail's)
    const auto fit_always = [&](int part, int tune, int kind) -> std::int16_t {
        const int at = inventory_first_free(g);
        if (at < 0) {
            return -1;
        }
        InventoryItem& item = g.inventory[at];
        item.garage = static_cast<std::int8_t>(garage);
        item.tune = static_cast<std::int8_t>(tune);
        item.kind = static_cast<std::int16_t>(kind);
        item.part = static_cast<std::int16_t>(part);
        return static_cast<std::int16_t>(at);
    };
    l.body = fit_always(b.body, b.body_tune, 0);
    l.leg = fit_always(b.leg, b.leg_tune, 1);
    l.arm[0] = fit(b.weapon[0], b.weapon_tune[0], 2);
    l.arm[1] = fit(b.weapon[1], b.weapon_tune[1], 2);
    l.back[0] = fit(b.weapon[2], b.weapon_tune[2], 3);
    l.back[1] = fit(b.weapon[3], b.weapon_tune[3], 3);
    l.chip = fit(b.chip, b.chip_tune, 4);
    chip_slot = l.chip;
    for (int o = 0; o < 10; ++o) {
        l.option[o] = fit(b.option[o], 0, 5);
    }
    if (b.color != -1 && b.color < color_count) {
        g.colors_owned[b.color] |= 1;
        l.color = static_cast<std::uint8_t>(b.color);
    } else {
        l.color = 0;
    }
    if (b.sticker != -1 && b.sticker < sticker_count) {
        g.stickers_owned[b.sticker] |= 1;
        l.sticker = static_cast<std::uint8_t>(b.sticker);
    } else {
        l.sticker = 0xff;
    }
    // the build's name, cut at the eight glyphs a name holds (retail copies up to 0x200 bytes
    // over the colour, the sticker and the next garage: docs/bugs.md)
    for (int i = 0; i < glyph_name_length; ++i) {
        l.name[i] = -1;
    }
    for (int i = 0; i < 8 && i < static_cast<int>(b.name.glyphs.size()) && b.name.glyphs[i] != -1;
         ++i) {
        l.name[i] = b.name.glyphs[static_cast<std::size_t>(i)];
    }
    return chip_slot;
}

// --- the hangar ---

// the class every fitted part agrees on (7 any)
// decomp: Loadout::common_class 0xBA381
int garage_common_class(const PartTables& t, const Garages& g, const Garage& l) {
    int klass = 7;
    const std::int16_t parts[6] = {l.body, l.leg, l.arm[0], l.arm[1], l.back[0], l.back[1]};
    const int kinds[6] = {0, 1, 2, 2, 3, 3};
    for (int k = 0; k < 6; ++k) {
        if (parts[k] < 0 || parts[k] >= inventory_size) {
            continue;
        }
        const int c = part_class(t, kinds[k], g.inventory[parts[k]].part);
        if (c == 8) {
            continue; // no record
        }
        if (k == 0 || klass == 7 || c == klass) {
            klass = c;
        }
    }
    return klass;
}

// body, legs and Chip all fitted to this garage
// decomp: hangar_garage_parts_owned 0x1CDF3
bool garage_parts_owned(const Garages& g, int i) {
    if (i < 0 || i >= garage_count) {
        return false;
    }
    const Garage& l = g.garages[i];
    if (l.body == -1 || l.leg == -1 || l.chip == -1) {
        return false;
    }
    return g.inventory[l.body].garage == i && g.inventory[l.leg].garage == i &&
           g.inventory[l.chip].garage == i;
}

// garages_drop_missing_parts and inventory_drop_invalid (retail's walk never
// moves past the first item: docs/bugs.md), then selected_garage_drop_if_incomplete
// decomp: garages_drop_missing_parts 0xBA744, inventory_drop_invalid 0xBA9AA
void garages_tidy(const PartTables& t, Garages& g) {
    for (InventoryItem& item : g.inventory) {
        if (item.part != -1 && ((item.garage < -1 || item.garage >= garage_count) || item.kind < 0 ||
                                item.kind >= 6)) {
            item.part = -1;
        }
    }
    const auto drop = [&](std::int16_t& slot) {
        if (slot != -1 && (slot < 0 || slot >= inventory_size || g.inventory[slot].part == -1)) {
            slot = -1;
        }
    };
    for (int i = 0; i < garage_count; ++i) {
        if (!garage_unlocked(g, i)) {
            continue;
        }
        Garage& l = g.garages[i];
        drop(l.body);
        drop(l.leg);
        drop(l.arm[0]);
        drop(l.arm[1]);
        drop(l.back[0]);
        drop(l.back[1]);
        drop(l.chip);
        for (std::int16_t& o : l.option) {
            drop(o);
        }
    }
    if (g.selected != -1 && !(garage_status(t, g, g.selected) & 4)) {
        g.selected = -1;
    }
}
