#pragma once
// The game save (docs/saves.md): exactly what the original's save holds, the first 0x60C4 bytes of
// its GameState, as named fields in a versioned .sexp file, one file per slot. PC-only settings
// (graphics, mouse, key bindings) live in gubsy's settings, never here.
//
// Saving and loading follow the original: a save copies the world's saved part into an image
// (save_write_slot bumps the save count first, and takes it back if the write fails); a
// load copies an image back over it (save_commit_selected_load), and the caller then does
// the rest of what that function does: copy the button config to player 0, apply the two volumes,
// and rebuild the music player's queue from the selected playlist.
#include "world/world.hpp"

#include <cstdint>
#include <string>
#include <string_view>

inline constexpr int save_version = 1;
// phantomcrash.sav holds three slots, but every screen loop in retail runs over slot 0 only
inline constexpr int save_slot_count = 3;

// one slot: the saved fields of the world, and when it was written
struct SaveImage {
    bool present = false;
    std::int64_t written = 0; // seconds since 1970, UTC (retail: a FILETIME)
    // only the saved fields of the story are written and loaded: not the at-entry counts or the
    // interrupted transition. rumble_selected and map_spin_money sit past +0x60C4 in retail and
    // aren't saved there; the port saves them (docs/bugs.md)
    Story story;
    Garages garages;
    Profile profile;
    RumbleSlot rumble[rumble_count];
    MachineOptions machine; // the volumes, screen shift and vibration: State's, not the world's
};

// the world's saved part and the machine's options into an image, stamped now (the count bump is
// save_slot_write's)
void save_image_take(SaveImage& image, const World& world, const MachineOptions& machine,
                     std::int64_t now);
// an image's saved part back over the world and the machine's options; the story's unsaved
// fields keep the world's values
void save_image_put(const SaveImage& image, World& world, MachineOptions& machine);

// the .sexp text of an image, and back (src/save/save_text.cpp, save_parse.cpp)
std::string save_text(const SaveImage& image);
bool save_parse(std::string_view text, SaveImage& image, std::string& error);

// files (src/save/save_files.cpp): <dir>/slot<N>.sexp
enum class SaveRead : std::uint8_t {
    Ok,
    Missing, // no file: the slot is empty (retail's "no save data")
    Damaged, // unreadable, unparsable, or another version (retail's damaged-file message)
};
// the per-user folder: SDL_GetPrefPath's, then saves/
std::string save_dir_default();
std::string save_slot_path(const std::string& dir, int slot);
SaveRead save_slot_read(const std::string& dir, int slot, SaveImage& image, std::string& error);
// save_write_slot: the save count up (to 9999), the image written to a temporary file and moved
// over the slot, the count taken back down if that fails
bool save_slot_write(const std::string& dir, int slot, World& world, const MachineOptions& machine,
                     std::string& error);
// the newest present slot of those read, as the load screen picks it (-1 for none)
int save_newest_slot(const SaveImage* images, int count);
