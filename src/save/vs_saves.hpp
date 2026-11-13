#pragma once
// The VS Scoobees KEEP SV writes (decomp src/hand/u_06afc0.c vs_save_list_first,
// vs_save_write, save_slot_info_read, save_list_delete,
// vs_save_find_garage). Retail makes each one a saved game of its own on the drive,
// named after the machine in ASCII plus " for VS", holding one signed 0xC4-byte header: the
// button config (GameState +0x6018, the header's stats), the machine's resolved build, the time,
// its name, its Chip's name and the pilot's. The port writes one .sexp file each in <save dir>/vs,
// named the same way, in the order retail's list would grow (oldest first).
#include "world/game_state.hpp"

#include <cstdint>
#include <string>
#include <vector>

// one saved VS Scoobee (SlotHeader); a file that doesn't read is listed, not present (damaged)
struct VsSave {
    std::string name; // the saved game's name: "<machine> for VS"
    bool present = false;
    std::int64_t written = 0; // seconds since 1970, UTC (retail: a FILETIME)
    RosterBuild build;        // the loadout, every part resolved to its record and tune
    GlyphName mech = {-1, -1, -1, -1, -1, -1, -1, -1, -1}; // the machine's name
    GlyphName chip = {-1, -1, -1, -1, -1, -1, -1, -1, -1}; // its Chip's name
    std::int16_t pilot[pilot_name_length] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    // the keeper's button config when it was kept (hdr.stats): VS gives its player this one
    // (player_config_store at the battle start); a file from before it was kept has the default
    ButtonConfig buttons;
};

std::string vs_save_dir(const std::string& save_dir);
// every saved VS Scoobee, oldest first (vs_save_list_first: the Game Records save left out)
std::vector<VsSave> vs_saves_list(const std::string& save_dir);
// a new save or one written over (vs_save_write): false when the file can't be written
bool vs_save_store(const std::string& save_dir, const VsSave& save, std::string& error);
// save_list_delete
bool vs_save_remove(const std::string& save_dir, const std::string& name);
