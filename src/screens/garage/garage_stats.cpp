// What items, builds and loadouts weigh and hold (decomp
// src/od/u_0b9460.c, src/hand/curve.c, src/hand/mech_part_assets.c, src/o1/u_01c789.c).
#include "screens/garage/garage_rules.hpp"

#include <cstdlib>

namespace {

const InventoryItem* item_at(const Garages& g, int index) {
    return index >= 0 && index < inventory_size ? &g.inventory[index] : nullptr;
}

} // namespace

// --- an item's stats ---

float item_weight(const PartTables& t, const InventoryItem& it) {
    if (it.part == -1) {
        return 0.0f;
    }
    switch (it.kind) {
    case 0:
        return body_part(t, it.part) ? curve_value(body_part(t, it.part)->weight, it.tune) : 0.0f;
    case 1:
        return leg_part(t, it.part) ? curve_value(leg_part(t, it.part)->weight, it.tune) : 0.0f;
    case 2:
    case 3:
        return weapon_part(t, it.kind, it.part)
                   ? curve_value(weapon_part(t, it.kind, it.part)->weight, it.tune)
                   : 0.0f;
    case 4:
        return chip_part(t, it.part) ? curve_value(chip_part(t, it.part)->weight, it.tune) : 0.0f;
    case 5:
        return option_part(t, it.part) ? option_part(t, it.part)->weight : 0.0f;
    default:
        return 0.0f;
    }
}

float item_leg_capacity(const PartTables& t, const InventoryItem& it) {
    return it.part != -1 && it.kind == 1 && leg_part(t, it.part)
               ? curve_value(leg_part(t, it.part)->capacity, it.tune)
               : 0.0f;
}

float item_durability(const PartTables& t, const InventoryItem& it) {
    if (it.part == -1) {
        return 0.0f;
    }
    if (it.kind == 0 && body_part(t, it.part)) {
        return curve_value(body_part(t, it.part)->durability, it.tune);
    }
    if (it.kind == 1 && leg_part(t, it.part)) {
        return curve_value(leg_part(t, it.part)->durability, it.tune);
    }
    if (it.kind == 5 && option_part(t, it.part)) {
        return static_cast<float>(static_cast<unsigned>(option_part(t, it.part)->durability));
    }
    return 0.0f;
}

int item_body_sockets(const PartTables& t, const InventoryItem& it) {
    return it.part != -1 && it.kind == 0 && body_part(t, it.part)
               ? static_cast<int>(curve_value(body_part(t, it.part)->sockets, it.tune))
               : 0;
}

float item_leg_speed(const PartTables& t, const InventoryItem& it) {
    return it.part != -1 && it.kind == 1 && leg_part(t, it.part)
               ? curve_value(leg_part(t, it.part)->speed, it.tune)
               : 0.0f;
}

float item_leg_jump(const PartTables& t, const InventoryItem& it) {
    return it.part != -1 && it.kind == 1 && leg_part(t, it.part)
               ? curve_value(leg_part(t, it.part)->jump, it.tune)
               : 0.0f;
}

float item_weapon_resource(const PartTables& t, const InventoryItem& it) {
    float best = 0.0f;
    int floor = 0;
    if (it.part != -1 && (it.kind == 2 || it.kind == 3) && weapon_part(t, it.kind, it.part)) {
        const WeaponPart* w = weapon_part(t, it.kind, it.part);
        best = curve_value(w->resource, it.tune);
        floor = w->resource_min;
    }
    return static_cast<float>(floor) > best ? static_cast<float>(floor) : best;
}

int item_weapon_shots(const PartTables& t, const InventoryItem& it) {
    if (it.part == -1 || (it.kind != 2 && it.kind != 3) || !weapon_part(t, it.kind, it.part)) {
        return 0;
    }
    const WeaponPart* w = weapon_part(t, it.kind, it.part);
    const int shots = static_cast<int>(curve_value(w->shots, it.tune));
    return shots < w->shots_min ? w->shots_min : shots;
}

int item_weapon_recharge(const PartTables& t, const InventoryItem& it) {
    return it.part != -1 && (it.kind == 2 || it.kind == 3) && weapon_part(t, it.kind, it.part)
               ? static_cast<int>(curve_value(weapon_part(t, it.kind, it.part)->recharge, it.tune))
               : 0;
}

int item_weapon_damage(const PartTables& t, const InventoryItem& it) {
    return it.part != -1 && (it.kind == 2 || it.kind == 3) && weapon_part(t, it.kind, it.part)
               ? static_cast<int>(curve_value(weapon_part(t, it.kind, it.part)->damage, it.tune))
               : 0;
}

int item_chip_identity_sprite(const PartTables& t, const InventoryItem& it) {
    return it.part != -1 && it.kind == 4 && chip_part(t, it.part)
               ? identity_sprite(t, chip_part(t, it.part)->identity)
               : -1;
}

int item_option_sockets(const PartTables& t, const InventoryItem& it) {
    if (it.part == -1 || it.kind != 5 || !option_part(t, it.part)) {
        return 0;
    }
    const int sockets = option_part(t, it.part)->sockets;
    return sockets <= 0 ? 1 : sockets;
}

int item_option_camo_frames(const PartTables& t, const InventoryItem& it) {
    return it.part != -1 && it.kind == 5 && option_part(t, it.part)
               ? option_part(t, it.part)->camo_frames
               : 0;
}

int item_option_camo_recharge(const PartTables& t, const InventoryItem& it) {
    return it.part != -1 && it.kind == 5 && option_part(t, it.part)
               ? option_part(t, it.part)->camo_recharge
               : 0;
}

float item_option_laser_reduction(const PartTables& t, const InventoryItem& it) {
    const float r = it.part != -1 && it.kind == 5 && option_part(t, it.part)
                        ? static_cast<float>(option_part(t, it.part)->laser_reduction)
                        : 0.0f;
    return r > 100.0f ? 100.0f : r;
}

int item_color_sprite(const PartTables& t, const InventoryItem& it) {
    return it.part != -1 && it.kind == 6 && paint_part(t, 6, it.part)
               ? (paint_part(t, 6, it.part)->selector & 0xff) + 0xcea
               : -1;
}

int item_sticker_sprite(const PartTables& t, const InventoryItem& it) {
    return it.part != -1 && it.kind == 7 && paint_part(t, 7, it.part)
               ? (paint_part(t, 7, it.part)->selector & 0xff) + 0xd21
               : -1;
}

int part_class(const PartTables& t, int kind, int id) {
    switch (kind) {
    case 0:
        return body_part(t, id) ? body_part(t, id)->body_class & 0xff : 8;
    case 1:
        return leg_part(t, id) ? leg_part(t, id)->maker & 0xff : 8;
    case 2:
    case 3:
        return weapon_part(t, kind, id) ? weapon_part(t, kind, id)->maker & 0xff : 8;
    case 4:
        return chip_part(t, id) ? chip_part(t, id)->style & 0xff : 8;
    case 5:
        return option_part(t, id) ? option_part(t, id)->style & 0xff : 8;
    default:
        return 7;
    }
}

// decomp: part_durability_floor_apply 0x3C562, body_durability_floor 0x92D12
float body_durability_floor(int body_class) {
    switch (body_class) {
    case 0:
        return 6000.0f;
    case 1:
        return 4500.0f;
    case 2:
        return 7500.0f;
    case 3:
    case 4:
        return 9000.0f;
    case 5:
        return 10500.0f;
    default:
        return 100.0f;
    }
}

// decomp: leg_durability_floor 0x92D8B
float leg_durability_floor(int) {
    return 100.0f; // leg_durability_floor: every geometry
}

// --- a build ---

float build_weight(const PartTables& t, const ShopBuild& b) {
    float weight = 0.0f;
    if (b.body != -1 && body_part(t, b.body)) {
        weight = curve_value(body_part(t, b.body)->weight, b.body_tune) + weight;
    }
    if (b.leg != -1 && leg_part(t, b.leg)) {
        weight = curve_value(leg_part(t, b.leg)->weight, b.leg_tune) + weight;
    }
    if (b.chip != -1 && chip_part(t, b.chip)) {
        weight = curve_value(chip_part(t, b.chip)->weight, b.chip_tune) + weight;
    }
    for (int m = 0; m < 4; ++m) {
        if (b.weapon[m] != -1 && weapon_part(t, m < 2 ? 2 : 3, b.weapon[m])) {
            weight = curve_value(weapon_part(t, m < 2 ? 2 : 3, b.weapon[m])->weight,
                                 b.weapon_tune[m]) +
                     weight;
        }
    }
    for (const int option : b.option) {
        if (option != -1 && option_part(t, option)) {
            weight += option_part(t, option)->weight;
        }
    }
    return weight;
}

// decomp: build_durability 0x76094
int build_durability(const PartTables& t, const ShopBuild& b) {
    float body = 0.0f, leg = 0.0f;
    const BodyPart* bp = body_part(t, b.body);
    const LegPart* lp = leg_part(t, b.leg);
    if (bp) {
        body = curve_value(bp->durability, b.body_tune);
        if (body_durability_floor(bp->body_class & 0xff) > body) {
            body = body_durability_floor(bp->body_class & 0xff);
        }
    }
    if (lp) {
        leg = curve_value(lp->durability, b.leg_tune);
        if (leg_durability_floor(lp->geometry) > leg) {
            leg = leg_durability_floor(lp->geometry);
        }
    }
    float total = body + leg;
    for (const int option : b.option) {
        if (option != -1 && option_part(t, option)) {
            total = static_cast<float>(static_cast<unsigned>(option_part(t, option)->durability)) +
                    total;
        }
    }
    return static_cast<int>(total);
}

ShopBuild loadout_build(const PartTables&, const Garages& g, const Garage& l) {
    ShopBuild b;
    const auto take = [&](int index, int& id, int& tune) {
        const InventoryItem* it = item_at(g, index);
        id = it ? it->part : -1;
        tune = it ? it->tune : 0;
    };
    take(l.body, b.body, b.body_tune);
    take(l.leg, b.leg, b.leg_tune);
    take(l.arm[0], b.weapon[0], b.weapon_tune[0]);
    take(l.arm[1], b.weapon[1], b.weapon_tune[1]);
    take(l.back[0], b.weapon[2], b.weapon_tune[2]);
    take(l.back[1], b.weapon[3], b.weapon_tune[3]);
    take(l.chip, b.chip, b.chip_tune);
    for (int o = 0; o < 10; ++o) {
        const InventoryItem* it = item_at(g, l.option[o]);
        b.option[o] = it ? it->part : -1;
    }
    // garage_stand_load_loadout: colour 0 and sticker 0xCD when out of range
    b.color = l.color < color_count ? l.color : 0;
    b.sticker = l.sticker < sticker_count ? l.sticker : no_sticker;
    return b;
}

// --- a loadout ---

float loadout_weight(const PartTables& t, const Garages& g, const Garage& l) {
    return build_weight(t, loadout_build(t, g, l));
}

float loadout_leg_capacity(const PartTables& t, const Garages& g, const Garage& l) {
    const InventoryItem* leg = item_at(g, l.leg);
    return leg && leg_part(t, leg->part) ? curve_value(leg_part(t, leg->part)->capacity, leg->tune)
                                         : 0.0f;
}

float loadout_durability(const PartTables& t, const Garages& g, const Garage& l) {
    ShopBuild b = loadout_build(t, g, l);
    for (int m = 0; m < 4; ++m) {
        b.weapon[m] = -1;
    }
    b.chip = -1;
    return static_cast<float>(build_durability(t, b));
}

int loadout_body_sockets(const PartTables& t, const Garages& g, const Garage& l) {
    const InventoryItem* body = item_at(g, l.body);
    return body && body_part(t, body->part)
               ? static_cast<int>(curve_value(body_part(t, body->part)->sockets, body->tune))
               : 0;
}

int loadout_camo_frames(const PartTables& t, const Garages& g, const Garage& l) {
    int total = 0;
    for (const std::int16_t index : l.option) {
        const InventoryItem* it = item_at(g, index);
        if (it && option_part(t, it->part)) {
            total += option_part(t, it->part)->camo_frames;
        }
    }
    return total;
}

int loadout_camo_recharge(const PartTables& t, const Garages& g, const Garage& l) {
    int total = 0;
    for (const std::int16_t index : l.option) {
        const InventoryItem* it = item_at(g, index);
        if (it && option_part(t, it->part)) {
            total += option_part(t, it->part)->camo_recharge;
        }
    }
    return total;
}

float loadout_laser_reduction(const PartTables& t, const Garages& g, const Garage& l) {
    float total = 0.0f;
    for (const std::int16_t index : l.option) {
        const InventoryItem* it = item_at(g, index);
        if (it && option_part(t, it->part)) {
            total = static_cast<float>(option_part(t, it->part)->laser_reduction) + total;
        }
    }
    return total > 100.0f ? 100.0f : total;
}

int loadout_option_socket_sum(const PartTables& t, const Garages& g, const Garage& l) {
    int total = 0;
    for (const std::int16_t index : l.option) {
        const InventoryItem* it = item_at(g, index);
        if (it && option_part(t, it->part)) {
            const int n = option_part(t, it->part)->sockets;
            total += n <= 0 ? 1 : n;
        }
    }
    return total;
}
