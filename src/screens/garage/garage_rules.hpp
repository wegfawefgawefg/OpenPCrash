#pragma once
// The garage's rules over the world's Garages (decomp src/hand/garage.c, src/od/u_0b9460.c,
// src/hand/curve.c; docs/port/parts.md, screens.md "Assemble"): what an item or a loadout weighs
// and holds, a garage's status, prices, the parts editor's pages, admission, fitting and taking
// off. The menus do this in the original's float arithmetic.
#include "screens/garage/part_tables.hpp"
#include "world/game_state.hpp"

#include <vector>

// --- an item's stats (InventoryItem::weight 0xBAE7A ... option_laser_reduction 0xBB668) ---
float item_weight(const PartTables& t, const InventoryItem& item);
float item_leg_capacity(const PartTables& t, const InventoryItem& item);
float item_durability(const PartTables& t, const InventoryItem& item);
int item_body_sockets(const PartTables& t, const InventoryItem& item);
float item_leg_speed(const PartTables& t, const InventoryItem& item);
float item_leg_jump(const PartTables& t, const InventoryItem& item);
float item_weapon_resource(const PartTables& t, const InventoryItem& item);
int item_weapon_shots(const PartTables& t, const InventoryItem& item);
int item_weapon_recharge(const PartTables& t, const InventoryItem& item);
int item_weapon_damage(const PartTables& t, const InventoryItem& item);
int item_chip_identity_sprite(const PartTables& t, const InventoryItem& item);
int item_option_sockets(const PartTables& t, const InventoryItem& item);
int item_option_camo_frames(const PartTables& t, const InventoryItem& item);
int item_option_camo_recharge(const PartTables& t, const InventoryItem& item);
float item_option_laser_reduction(const PartTables& t, const InventoryItem& item);
int item_color_sprite(const PartTables& t, const InventoryItem& item);   // 0xCEA + selector
int item_sticker_sprite(const PartTables& t, const InventoryItem& item); // 0xD21 + selector
// the class byte (record +4; 7 fits any), 8 for a missing record
int part_class(const PartTables& t, int kind, int id);
// part_durability_floor_apply and the human floors (body_durability_floor)
float body_durability_floor(int body_class);
float leg_durability_floor(int geometry);

// --- a whole build (BuildRecord) ---
// chassis_loadout_weight
float build_weight(const PartTables& t, const ShopBuild& b);
// build_durability with the human floors (no actor)
int build_durability(const PartTables& t, const ShopBuild& b);
// garage_resolve_inventory_refs: a loadout's parts and tunes as a build (colour and sticker kept)
ShopBuild loadout_build(const PartTables& t, const Garages& g, const Garage& loadout);

// --- a loadout (Loadout::weight 0xB9940 ...) ---
float loadout_weight(const PartTables& t, const Garages& g, const Garage& l);
float loadout_leg_capacity(const PartTables& t, const Garages& g, const Garage& l);
float loadout_durability(const PartTables& t, const Garages& g, const Garage& l);
int loadout_body_sockets(const PartTables& t, const Garages& g, const Garage& l);
int loadout_camo_frames(const PartTables& t, const Garages& g, const Garage& l);
int loadout_camo_recharge(const PartTables& t, const Garages& g, const Garage& l);
float loadout_laser_reduction(const PartTables& t, const Garages& g, const Garage& l);
int loadout_option_socket_sum(const PartTables& t, const Garages& g, const Garage& l);

// garage_status's bits
inline constexpr int garage_locked = 0x1;
inline constexpr int garage_empty = 0x2;
inline constexpr int garage_whole = 0x4; // body, legs and chip fitted
inline constexpr int garage_boarded = 0x8;
inline constexpr int garage_part_missing = 0x10; // one of body, legs and chip missing
inline constexpr int garage_ready = 0x20;
inline constexpr int garage_no_weapon = 0x40;
inline constexpr int garage_overweight = 0x80;
inline constexpr int garage_warnings = garage_part_missing | garage_no_weapon | garage_overweight;
int garage_status(const PartTables& t, const Garages& g, int garage);
bool garage_unlocked(const Garages& g, int garage);
// inventory_first_free: the first slot with part -1, -1 when full
int inventory_first_free(const Garages& g);
// a glyph name copied (loadout_name_copy), -1 ended
void glyph_name_copy(GlyphName& to, const GlyphName& from);

// --- prices (units of 10 NY) ---
// shop_item_price
int shop_item_price(const PartTables& t, int kind, int id, int tune);
// part_tune_price: Plus Tech's price for a tune from -> to
int part_tune_price(const PartTables& t, int kind, int id, int from, int to);
// shop_scoobee_total: every part at its tune, paint and options
int shop_scoobee_total(const PartTables& t, const ShopBuild& b);
// the price curve's float at a tune (the lists' sort value), a flat price for the rest
float price_value(const PartTables& t, int kind, int id, int tune);

// --- the parts editor's pages (GaragePage, include/garage.h) ---
struct GarageEntry {
    int item = -1;  // the inventory slot (colours and stickers: the row)
    int id = -1;    // the part row
    int tune = 0;
    int garage = -1; // fitted to, -1 none
    int slot = -1;  // arm or back side 0/1, option slot
    int klass = 7;  // the class byte
    const PartName* name = nullptr;
    float value = 0; // the price it sorts by
};
struct GaragePage {
    std::vector<GarageEntry> entries;
    int column = 0;       // f_ff1: 0 names, 1 prices
    bool descending = true; // f_ff2
};

// the editor's state the rules read and write (the Garage payload, include/garage.h)
struct GarageEditor {
    int garage = 0;      // +0x08 the garage edited
    int status = 0;      // +0x0C garage_status
    int category = 0;    // +0x3614
    int side = 0;        // +0x2C the weapon side picked
    int cursor = 0, scroll = 0;
    int build_class = 7; // +0x3628
    GaragePage pages[8];
    Garage working;      // +0xB6D4
};

// garage_pages_build (each page sorted by name) and garage_pages_mark_fitted
void garage_pages_build(const PartTables& t, const Garages& g, GaragePage pages[8]);
void garage_pages_mark_fitted(const Garages& g, GaragePage pages[8]);
// garage_page_sort: column 0 names (strcmp on the Shift-JIS bytes), 1 prices; flag 1 is
// A -> Z and high -> low
void garage_page_sort(GaragePage& page, int column, bool descending);
// garage_pages_commit_inventory: the page marks back to the items' garages
void garage_pages_commit_inventory(const GarageEditor& e, Garages& g);
// garage_option_body_stat: free sockets (the body's, capped at 10, less those used)
int garage_free_sockets(const PartTables& t, const Garages& g, const GarageEditor& e);
// garage_admit_candidate
bool garage_admit(const PartTables& t, const Garages& g, const GarageEditor& e,
                  const GarageEntry& entry);
// garage_entry_fit_state: 0 on this garage, 1 right side, 2 left side, 3 not here
int garage_fit_state(const GarageEditor& e, const GarageEntry& entry);
// garage_loadout_records_check: the build class, colour cleared for classes 3..6
void garage_records_check(const PartTables& t, const Garages& g, GarageEditor& e);
// garage_clear_working_loadout_fields: working rebuilt from the marks, status, class
void garage_rebuild_working(const PartTables& t, const Garages& g, GarageEditor& e);
// garage_manual_select on the entry under the cursor. Returns accepted; `changed` says
// whether something was fitted or taken off (sound 0x1D), `dropped` the options freed
bool garage_manual_select(const PartTables& t, const Garages& g, GarageEditor& e, bool& changed,
                          int& dropped);
// loadout_toggle_part
void loadout_toggle_part(Garage& l, int kind, int part, int side);
// garage_working_loadout_reset
void loadout_reset(Garage& l);
// garage_stock.cpp, the hangar's: Loadout::common_class, the class every fitted part
// agrees on (7 any); hangar_garage_parts_owned, body, legs and Chip all fitted to the
// garage; garages_drop_missing_parts, inventory_drop_invalid (retail's walk
// never moves past the first item: docs/bugs.md) and selected_garage_drop_if_incomplete
int garage_common_class(const PartTables& t, const Garages& g, const Garage& l);
bool garage_parts_owned(const Garages& g, int garage);
void garages_tidy(const PartTables& t, Garages& g);
// wild_machines_used_deliver: a build's parts into free slots fitted to the garage, its
// paint owned, its name; returns the Chip's slot (-1 none)
int used_deliver(const PartTables& t, Garages& g, const ShopBuild& b, int garage);
