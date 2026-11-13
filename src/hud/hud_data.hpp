#pragma once
// The battle HUD's tables: the layouts of data/ui/layouts/hud.sexp and battle.sexp (through the
// screen_hud and screen_battle packs; decomp src/data/ui_c.c) and the names the tags and barks
// show (tables/identities.sexp, pilots.sexp, chips.sexp, reaction_profiles.sexp in core).
#include "screens/ui/ui_data.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// HudRow (0x1C) and HudElemRow (0x28, include/tables.h): a sprite at its 640x480 top left, its
// scale, layer and colour; a readout row adds its place on the cockpit panels (metres for a
// frame, pixels x 0.02 from the frame for the rest) and the panel's turn
struct HudRow {
    int sprite = -1;
    float x = 0, y = 0;
    float scale_x = 1, scale_y = 1;
    int blend = 0x101;
    std::uint32_t color = 0xFFFFFFFF;
    float world_x = 0, world_y = 0, world_yaw = 0;
};

using HudLayout = std::array<HudRow, 18>;
using HudWeaponLayout = std::array<HudRow, 22>;

// rows of the readout layouts (hud_draw_readout)
enum HudWeaponRow : int {
    row_arm_frame = 0,
    row_back_frame = 1,
    row_digit0 = 2, // .. 11
    row_percent = 12,
    row_icon_melee = 13,
    row_icon_missile = 14,
    row_icon_grenade = 15,
    row_icon_gun = 16,
    row_icon_laser = 17,
    row_icon_homing = 18,
    row_icon_2 = 19,
    row_ammo_bar = 20,
    row_broken = 21,
};

// rows of hud_layout_1p / 2p / 4p
enum HudLayoutRow : int {
    row_camo_bar = 0,
    row_camo_frame = 1,
    row_armour_bar = 2,
    row_armour_frame = 3,
    row_lock_warning = 4,
    row_hazard = 5,
    row_down_50 = 6, // and 7
    row_down_75 = 8, // and 9
    row_time = 10,
    row_day = 11,
    row_money = 12,
    row_kills = 13,
    row_popup_taken = 14,
    row_popup_kill = 15,
    row_popup_dealt = 16,
};

struct HudData {
    HudLayout layout_1p, layout_2p, layout_4p;
    std::array<HudWeaponLayout, 5> weapon; // hud_weapon_layouts[5][22]
    std::array<HudRow, 3> vs_marks_1view, vs_marks_2view;
    // hud_ammo_digits_*: each digit's (x, y) from the frame's top left
    std::array<int, 8> digits_1view_left{}, digits_1view_right{}, digits_left{},
        digits_4view_right{}, digits_2view_right{};
    // the cockpit gauges' cells on their 512 textures (hud_draw_meters)
    std::array<std::array<int, 2>, 15> walk_cells{};
    std::array<std::array<int, 2>, 7> bar_cells{}, speed_cells{};
    std::array<std::array<int, 2>, 12> jump_cells{};
    std::array<std::array<float, 3>, 4> cloak_arrows{}; // x, y, turn
    std::array<float, 4> view_dx{}, view_dworld{};      // the readout's slide on a view change
    std::vector<SpriteSlot> battle_sprites;              // the VS end menu, the empty quarter
    std::array<std::array<float, 2>, 4> vs_empty_view{};

    // names: identity rows (name_en empty for none), pilot rows, chip bark profiles, and each
    // reaction profile's 35 bark scripts by kind (0 none)
    std::vector<std::string> identity_name_en, identity_name;
    std::vector<int> identity_sprite;
    std::vector<int> pilot_identity, pilot_scoobee, pilot_reaction;
    std::vector<int> chip_bark_profile, chip_identity; // ChipRecord +0x18, +0x1A ("family")
    std::vector<std::string> chip_name;                // ChipRecord +0 (the chip's own name)
    std::vector<std::array<std::uint32_t, 35>> reaction_barks;
};

bool load_hud_data(HudData& data, UiData& ui, std::string& error);
