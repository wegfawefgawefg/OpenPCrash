#pragma once
// The part records as the garage and shop screens read them (decomp include/tables.h, docs/port/
// parts.md): names, prices, the stats the cards show, the shops' stock rows and the used
// Scoobees' builds, from tables/*.sexp. The menus count money and stats in the original's float
// arithmetic (curve.c), so these keep its types; the world's fixed point copies are src/battle's.
#include "talk/talk_script.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct UiData;

// a CurveF, CurveU or CurveS (base, at_mid, at_max added to base, mid, negative)
enum class CurveKind : std::uint8_t { f, u, s };
struct Curve {
    CurveKind kind = CurveKind::f;
    double base = 0, at_mid = 0, at_max = 0;
    int mid = 0, negative = -1;
};
// float_curve_value, rumble_curve_value, Curve::value_at: the float the
// original returns, clamped to 0..100000000
float curve_value(const Curve& curve, int tune);

// a record's name: as the font's glyphs (drawing) and Shift-JIS bytes (garage_page_sort's strcmp)
struct PartName {
    GlyphText glyphs;
    std::string sjis;
};

struct BodyPart {
    PartName name;
    int body_class = 0; // +0x04
    Curve durability, weight, sockets, price;
};
struct LegPart {
    PartName name;
    int maker = 0;    // +0x04, the class byte the garage compares
    int geometry = 0; // the leg class
    Curve durability, speed, jump, weight, capacity, price;
};
struct WeaponPart {
    PartName name;
    int maker = 0;
    int geometry = -1;
    Curve weight, shots, damage, resource, recharge, price;
    int shots_min = 0, damage_min = 0, resource_min = 0;
};
struct ChipPart {
    PartName name;
    int style = 7;     // +0x04
    int identity = 0;  // +0x1A, rumble_identities row
    int reticle_kind_a = 0, reticle_kind_b = 0;
    Curve weight, reticle_size_a, reticle_size_b, crit, shield, price;
};
struct OptionPart {
    PartName name;
    int style = 7;
    int durability = 0;
    float weight = 0;
    int sockets = 0;         // u8
    int laser_reduction = 0; // u8
    int camo_frames = 0, camo_recharge = 0;
    int price = 0;
};
struct PaintPart {
    PartName name;
    int selector = 0;
    int price = 0;
};

// an ItemStock or ScoobeeStock row: (part or loadout, tune) pairs
struct StockItem {
    int id = -1, tune = 0;
};

// BuildRecord: every part a row, -1 none
struct ShopBuild {
    PartName name;
    int body = -1, body_tune = 0, leg = -1, leg_tune = 0, chip = -1, chip_tune = 0;
    int weapon[4] = {-1, -1, -1, -1}, weapon_tune[4] = {};
    int color = 0, sticker = 205;
    int option[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};

struct PartTables {
    std::vector<BodyPart> bodies;
    std::vector<LegPart> legs;
    std::vector<WeaponPart> arms, backs;
    std::vector<ChipPart> chips;
    std::vector<OptionPart> options;
    std::vector<PaintPart> colors, stickers;
    std::vector<int> identity_sprites; // rumble_identities[i].sprite
    std::vector<std::vector<StockItem>> parts_stock, chip_stock, scoobee_stock;
    std::vector<ShopBuild> builds; // rumble_scoobee_loadouts
    bool loaded = false;
};

// from the core pack (tables family); false with the error when a table is missing
bool load_part_tables(PartTables& tables, UiData& data, std::string& error);

// sv_*_record: -1 none (null); a row out of range reads row 0, as the lookups do
const BodyPart* body_part(const PartTables& t, int id);
const LegPart* leg_part(const PartTables& t, int id);
const WeaponPart* weapon_part(const PartTables& t, int kind, int id); // kind 2 arm, 3 back
const ChipPart* chip_part(const PartTables& t, int id);
const OptionPart* option_part(const PartTables& t, int id);
const PaintPart* paint_part(const PartTables& t, int kind, int id); // kind 6 colour, 7 sticker
// part_name: a kind's record name, empty for none
const PartName* part_name(const PartTables& t, int kind, int id);
// identity_record_at(i)->sprite, -1 none
int identity_sprite(const PartTables& t, int identity);
