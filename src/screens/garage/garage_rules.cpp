// The parts editor's pages, admission, fitting and taking off (decomp src/hand/garage.c,
// src/o1/u_012ff5.c).
#include "screens/garage/garage_rules.hpp"

#include <cstring>

namespace {

const InventoryItem* item_at(const Garages& g, int index) {
    return index >= 0 && index < inventory_size ? &g.inventory[index] : nullptr;
}

} // namespace

// --- the pages ---

float price_value(const PartTables& t, int kind, int id, int tune) {
    const Curve* curve = nullptr;
    switch (kind) {
    case 0:
        curve = body_part(t, id) ? &body_part(t, id)->price : nullptr;
        break;
    case 1:
        curve = leg_part(t, id) ? &leg_part(t, id)->price : nullptr;
        break;
    case 2:
    case 3:
        curve = weapon_part(t, kind, id) ? &weapon_part(t, kind, id)->price : nullptr;
        break;
    case 4:
        curve = chip_part(t, id) ? &chip_part(t, id)->price : nullptr;
        break;
    default:
        return static_cast<float>(static_cast<unsigned>(shop_item_price(t, kind, id, 0)));
    }
    return curve ? curve_value(*curve, tune) : 0.0f;
}

// decomp: garage_pages_build 0x182B1
void garage_pages_build(const PartTables& t, const Garages& g, GaragePage pages[8]) {
    for (int k = 0; k < 8; ++k) {
        pages[k] = GaragePage{};
    }
    for (int n = 0; n < inventory_size; ++n) {
        const InventoryItem& it = g.inventory[n];
        if (it.part == -1 || it.kind < 0 || it.kind > 5) {
            continue;
        }
        GaragePage& page = pages[it.kind];
        if (page.entries.size() >= 0xff) {
            continue;
        }
        GarageEntry e;
        e.item = n;
        e.garage = it.garage < 0 ? -1 : it.garage;
        e.klass = part_class(t, it.kind, it.part);
        e.tune = it.tune;
        e.id = it.part;
        e.name = part_name(t, it.kind, it.part);
        // the price curve at the tune, unrounded; options their flat price
        e.value = price_value(t, it.kind, it.part, it.tune);
        page.entries.push_back(e);
    }
    for (int k = 0; k < 6; ++k) {
        garage_page_sort(pages[k], 0, true);
    }
    // owned colours but "none", owned stickers but "none"
    for (int kind = 6; kind <= 7; ++kind) {
        GaragePage& page = pages[kind];
        const int count = kind == 6 ? color_count : sticker_count;
        for (int c = 0; c < count; ++c) {
            const bool owned = kind == 6 ? (g.colors_owned[c] & 1) && c != 0
                                         : (g.stickers_owned[c] & 1) && c != no_sticker;
            if (!owned || page.entries.size() >= 0xff) {
                continue;
            }
            GarageEntry e;
            e.item = c;
            e.garage = -1;
            e.klass = 7;
            e.tune = 0;
            e.id = c;
            e.name = part_name(t, kind, c);
            e.value = paint_part(t, kind, c)
                          ? static_cast<float>(static_cast<unsigned>(paint_part(t, kind, c)->price))
                          : 0.0f;
            page.entries.push_back(e);
        }
        garage_page_sort(page, 0, true);
    }
}

// decomp: garage_pages_mark_fitted 0x17152
void garage_pages_mark_fitted(const Garages& g, GaragePage pages[8]) {
    for (int gi = 0; gi < garage_count; ++gi) {
        const Garage& l = g.garages[gi];
        for (GarageEntry& e : pages[2].entries) {
            if (e.item == l.arm[1]) {
                e.slot = 1;
            } else if (e.item == l.arm[0]) {
                e.slot = 0;
            }
        }
        for (GarageEntry& e : pages[3].entries) {
            if (e.item == l.back[1]) {
                e.slot = 1;
            } else if (e.item == l.back[0]) {
                e.slot = 0;
            }
        }
        for (int o = 0; o < 10; ++o) {
            if (l.option[o] == -1) {
                continue;
            }
            for (GarageEntry& e : pages[5].entries) {
                if (e.item == l.option[o]) {
                    e.slot = o;
                }
            }
        }
    }
}

// decomp: garage_page_sort 0x1733D
void garage_page_sort(GaragePage& page, int column, bool descending) {
    std::vector<GarageEntry>& rows = page.entries;
    const int count = static_cast<int>(rows.size());
    // a selection sort, as retail's: equal keys keep no particular order, so this one is copied
    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            bool swap = false;
            if (column == 0) {
                const std::string& a = rows[static_cast<std::size_t>(i)].name
                                           ? rows[static_cast<std::size_t>(i)].name->sjis
                                           : std::string();
                const std::string& b = rows[static_cast<std::size_t>(j)].name
                                           ? rows[static_cast<std::size_t>(j)].name->sjis
                                           : std::string();
                const int r = std::strcmp(a.c_str(), b.c_str());
                swap = (descending && r > 0) || (!descending && r < 0);
            } else {
                const float a = rows[static_cast<std::size_t>(i)].value;
                const float b = rows[static_cast<std::size_t>(j)].value;
                swap = (descending && a < b) || (!descending && a > b);
            }
            if (swap) {
                std::swap(rows[static_cast<std::size_t>(i)], rows[static_cast<std::size_t>(j)]);
            }
        }
    }
    page.column = column;
    page.descending = descending;
}

// decomp: garage_pages_commit_inventory 0x17271
void garage_pages_commit_inventory(const GarageEditor& e, Garages& g) {
    for (int k = 0; k < 6; ++k) {
        for (const GarageEntry& entry : e.pages[k].entries) {
            if (entry.item >= 0 && entry.item < inventory_size) {
                g.inventory[entry.item].garage = static_cast<std::int8_t>(entry.garage);
            }
        }
    }
}

// decomp: garage_option_body_stat 0x18684
int garage_free_sockets(const PartTables& t, const Garages& g, const GarageEditor& e) {
    int value = 0;
    if (e.working.body != -1) {
        const InventoryItem* body = item_at(g, e.working.body);
        if (body && body_part(t, body->part)) {
            value = static_cast<int>(curve_value(body_part(t, body->part)->sockets, body->tune));
            if (value > 10) {
                value = 10;
            }
        }
    }
    const int free = value - loadout_option_socket_sum(t, g, e.working);
    return free < 0 ? 0 : free;
}

// decomp: garage_admit_candidate 0x186E9
bool garage_admit(const PartTables& t, const Garages& g, const GarageEditor& e,
                  const GarageEntry& entry) {
    // (1) another maker's part than the build's
    if (e.build_class != 7 && entry.klass != 7 && entry.klass != e.build_class) {
        return false;
    }
    if (e.garage != entry.garage) {
        // (3) an option needing more sockets than are free
        if (e.category == 5) {
            int needed = 1;
            if (const OptionPart* o = option_part(t, entry.id)) {
                needed = o->sockets <= 0 ? 1 : o->sockets;
            }
            if (needed > garage_free_sockets(t, g, e)) {
                return false;
            }
        }
        // (2) on another garage
        if (entry.garage != -1) {
            return false;
        }
    }
    // (4) a weapon on a garage on the other side (only this one gets here)
    if ((e.category == 2 || e.category == 3) && entry.garage >= 0 && entry.garage < 10 &&
        entry.slot != e.side) {
        return false;
    }
    return true;
}

// decomp: garage_entry_fit_state 0x1798B
int garage_fit_state(const GarageEditor& e, const GarageEntry& entry) {
    if (e.category == 6) {
        return entry.item == e.working.color ? 0 : 3;
    }
    if (e.category == 7) {
        return static_cast<std::int8_t>(entry.item) == static_cast<std::int8_t>(e.working.sticker)
                   ? 0
                   : 3;
    }
    if (e.garage == entry.garage) {
        return e.category == 2 || e.category == 3 ? (entry.slot == 1) + 1 : 0;
    }
    return 3;
}

// decomp: garage_loadout_records_check 0x17ED8
void garage_records_check(const PartTables& t, const Garages& g, GarageEditor& e) {
    int klass = 7;
    const auto look = [&](int index, int kind) {
        const InventoryItem* it = item_at(g, index);
        if (!it) {
            return;
        }
        const int c = part_class(t, kind, it->part);
        if (c != 8 && c != 7) {
            klass = c;
        }
    };
    look(e.working.body, 0);
    look(e.working.leg, 1);
    look(e.working.arm[0], 2);
    look(e.working.arm[1], 2);
    look(e.working.back[0], 3);
    look(e.working.back[1], 3);
    look(e.working.chip, 4);
    for (const std::int16_t option : e.working.option) {
        look(option, 5);
    }
    if (klass >= 3 && klass <= 6) {
        e.working.color = 0;
    }
    e.build_class = klass;
}

// decomp: garage_clear_working_loadout_fields 0x1809D
void garage_rebuild_working(const PartTables& t, const Garages& g, GarageEditor& e) {
    Garage& w = e.working;
    w.body = w.leg = w.chip = -1;
    w.arm[0] = w.arm[1] = w.back[0] = w.back[1] = -1;
    for (std::int16_t& option : w.option) {
        option = -1;
    }
    for (int category = 0; category < 6; ++category) {
        for (const GarageEntry& entry : e.pages[category].entries) {
            if (entry.garage == -1 || entry.garage != e.garage) {
                continue;
            }
            const auto item = static_cast<std::int16_t>(entry.item);
            switch (category) {
            case 0:
                w.body = item;
                break;
            case 1:
                w.leg = item;
                break;
            case 2:
                w.arm[entry.slot != 1 ? 0 : 1] = item;
                break;
            case 3:
                w.back[entry.slot != 1 ? 0 : 1] = item;
                break;
            case 4:
                w.chip = item;
                break;
            case 5:
                if (entry.slot >= 0 && entry.slot < 10) {
                    w.option[entry.slot] = item;
                }
                break;
            }
        }
    }
    e.status = garage_status(t, g, e.garage);
    garage_records_check(t, g, e);
}

// decomp: garage_manual_select 0x1AFE1
bool garage_manual_select(const PartTables& t, const Garages& g, GarageEditor& e, bool& changed,
                          int& dropped) {
    changed = false;
    dropped = 0;
    GaragePage& page = e.pages[e.category];
    const int row = e.cursor + e.scroll;
    if (row < 0 || row >= static_cast<int>(page.entries.size())) {
        return false;
    }
    GarageEntry& entry = page.entries[static_cast<std::size_t>(row)];
    bool ok = true;
    const auto free_options = [&](bool count) {
        for (GarageEntry& o : e.pages[5].entries) {
            if (o.garage == e.garage) {
                o.garage = -1;
                o.slot = 0;
                if (count) {
                    ++dropped;
                }
            }
        }
    };
    if (e.category == 2 || e.category == 3) {
        if (entry.garage == e.garage && entry.slot == e.side) {
            entry.garage = -1;
            changed = true;
        } else if (garage_admit(t, g, e, entry)) {
            for (GarageEntry& o : page.entries) {
                if (o.garage == e.garage && o.slot == e.side) {
                    o.slot = -1;
                    o.garage = -1;
                }
            }
            entry.slot = e.side;
            entry.garage = e.garage;
            changed = true;
        }
    } else if (e.category == 6 && entry.item == e.working.color) {
        e.working.color = 0;
        entry.garage = -1;
        changed = true;
    } else if (e.category == 7 &&
               static_cast<std::int8_t>(entry.item) == static_cast<std::int8_t>(e.working.sticker)) {
        e.working.sticker = no_sticker;
        entry.garage = -1;
        changed = true;
    } else if (entry.garage == e.garage && e.category != 6 && e.category != 7) {
        changed = true;
        entry.garage = -1;
        if (e.category == 0) {
            free_options(false); // the body's options come off silently
        }
    } else if (garage_admit(t, g, e, entry)) {
        const int sockets_used = e.category == 0 ? loadout_option_socket_sum(t, g, e.working) : 0;
        if (e.category != 5) {
            for (GarageEntry& o : page.entries) {
                if (o.garage == e.garage) {
                    o.garage = -1;
                }
            }
        } else {
            for (int i = 0; i < 10; ++i) {
                if (e.working.option[i] == -1) {
                    entry.slot = i;
                    break;
                }
            }
        }
        entry.garage = e.garage;
        if (e.category == 0) {
            int sockets = 0;
            if (const BodyPart* body = body_part(t, entry.id)) {
                sockets = static_cast<int>(curve_value(body->sockets, entry.tune));
            }
            if (sockets_used > sockets) {
                free_options(true);
            }
        } else if (e.category == 6) {
            e.working.color = static_cast<std::uint8_t>(entry.item);
        } else if (e.category == 7) {
            e.working.sticker = static_cast<std::uint8_t>(entry.item);
        }
        changed = true;
    } else {
        ok = false;
    }
    garage_rebuild_working(t, g, e);
    return ok;
}

// decomp: loadout_toggle_part 0x174AE
void loadout_toggle_part(Garage& l, int kind, int part, int side) {
    const auto toggle = [&](std::int16_t& slot) {
        slot = slot == part ? static_cast<std::int16_t>(-1) : static_cast<std::int16_t>(part);
    };
    switch (kind) {
    case 0:
        toggle(l.body);
        break;
    case 1:
        toggle(l.leg);
        break;
    case 2:
        toggle(l.arm[side == 0 ? 0 : 1]);
        break;
    case 3:
        toggle(l.back[side == 0 ? 0 : 1]);
        break;
    case 4:
        toggle(l.chip);
        break;
    case 5: {
        for (std::int16_t& option : l.option) {
            if (option == part) {
                option = -1;
                return;
            }
        }
        int i = 0;
        while (i < 10 && l.option[i] != -1) {
            ++i;
        }
        // retail writes option[10] (the name's first glyph) when all ten are full; the port
        // writes nothing (docs/bugs.md)
        if (i < 10) {
            l.option[i] = static_cast<std::int16_t>(part);
        }
        break;
    }
    case 6:
        l.color = l.color == part ? 0 : static_cast<std::uint8_t>(part);
        break;
    case 7:
        l.sticker = static_cast<std::int8_t>(l.sticker) == static_cast<std::int8_t>(part)
                        ? static_cast<std::uint8_t>(no_sticker)
                        : static_cast<std::uint8_t>(part);
        break;
    }
}

// decomp: garage_working_loadout_reset 0x17104
void loadout_reset(Garage& l) {
    l = Garage{};
    l.name[0] = -1;
}
