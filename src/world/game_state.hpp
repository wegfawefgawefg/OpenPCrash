#pragma once
// The rest of what the original's save holds besides the story's progress (src/story/story.hpp):
// the garages and what the player owns, the pilot's name, the records, the music player's
// playlists, the options and button config, and today's four RumbleContexts with their opponent
// queues and drawn rosters. Plain integers laid out as the original's GameState head (decomp
// include/game_state.h, docs/saves.md), so the world stays a copyable deterministic value and the
// garage, shop, records and roster code can use them as they come.
#include "fx/vec.hpp"
#include "story/story.hpp"
#include "world/fighter_slots.hpp"

#include <cstdint>

struct World;

// names the original keeps as font glyphs (name entry, sjis_to_glyphs 0xBDC51): -1 ends one
inline constexpr int glyph_name_length = 9;
using GlyphName = std::int16_t[glyph_name_length];

// --- garages (GameState +0xC3C..+0x2820) ---

inline constexpr int inventory_size = 255; // GameState.inventory
inline constexpr int garage_count = 10;    // GameState.garages
inline constexpr int color_count = 57;     // colors_owned[0x39]
inline constexpr int sticker_count = 206;  // stickers_owned[0xCE]
inline constexpr int no_sticker = 205;     // 0xCD, "none", owned from the start
inline constexpr int new_game_garages = 7; // garages 0..2 unlocked (game_state_new_garages)

// InventoryItem.kind: what a part is (the members also read 6 colour and 7 sticker)
enum ItemKind : std::int16_t {
    item_body = 0,
    item_leg = 1,
    item_arm = 2,
    item_back = 3,
    item_chip = 4,
    item_option = 5,
};

// one thing the player owns (InventoryItem, 0x18 bytes); part -1 is a free slot
struct InventoryItem {
    std::int8_t garage = -1; // the garage it's fitted to, -1 none
    std::int8_t tune = 0;    // the tune level its stat curves are read at, -99..99
    std::int16_t kind = -1;  // ItemKind
    std::int16_t part = -1;  // row of its part table (bodies, legs, arm/back weapons, chips, options)
    GlyphName name = {-1, -1, -1, -1, -1, -1, -1, -1, -1}; // a chip's name (the chip list prints it)
};

// a garage (Loadout, 0x36 bytes): inventory indexes of what's fitted, -1 for nothing
struct Garage {
    std::int16_t body = -1;
    std::int16_t leg = -1;
    std::int16_t arm[2] = {-1, -1};   // arm A, arm B
    std::int16_t back[2] = {-1, -1};  // back A, back B
    std::int16_t chip = -1;
    std::int16_t option[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    GlyphName name = {-1, -1, -1, -1, -1, -1, -1, -1, -1}; // the machine's name
    std::uint8_t color = 0;                                // row of colors
    std::uint8_t sticker = no_sticker;                     // row of stickers
};

struct Garages {
    InventoryItem inventory[inventory_size];
    Garage garages[garage_count];
    std::uint32_t unlocked = new_game_garages; // a bit per garage (garage_unlocked +0xD44)
    int selected = -1;                          // selected_garage, 5 signed bits at +0x9C8; -1 none
    std::uint8_t colors_owned[color_count] = {};     // bit 0: owned
    std::uint8_t stickers_owned[sticker_count] = {}; // bit 0: owned
};

// --- the pilot, records, music, options (GameState +0x9C8.., +0xAA8.., +0x280C, +0x5FE0..) ---

inline constexpr int pilot_name_length = 10; // pilot_name[10], -1 ends it
inline constexpr int playlist_count = 8;
inline constexpr int playlist_length = 12;
inline constexpr int sonic_amp_stock_size = 10;
// play_time and the play clock stop here (play_time_tick); shortest_battle starts here
inline constexpr int play_time_cap = 0xCDFD7F0;

// the record screen's numbers (records_clear); days_played and completed_cycles are the
// story's
struct Records {
    int total_earned = 0;            // +0x5FE0 "Total prize money", units of 10 NY
    int best_payout = 0;             // +0x5FE4 "Highest prize money earned"
    int battles = 0;                 // +0x5FE8 "Rumblings played"
    int wins = 0;                    // +0x5FEC "Number of returns"
    int kills = 0;                   // +0x5FF4 "Total enemies shot down"
    int drones_shot_down = 0;        // +0x5FF8
    int play_time = 0;               // +0x6000 vblanks, capped at play_time_cap
    int longest_battle = 0;          // +0x6004
    int shortest_battle = play_time_cap; // +0x6008, shown as 0 until a battle is played
    int save_count = 0;              // +0x600C, capped at 9999 (save_write_slot)
    int enemy_weapons_destroyed = 0; // +0x6010, capped at 9999
    int weapons_destroyed = 0;       // +0x6014
    int play_clock = 0;              // +0x9C8 bits 5..23, 19 bits; no reader found
};

// the media player's playlists and Sonic Amp's stock tonight
struct Music {
    int playlists[playlist_count][playlist_length] = {}; // catalogue ids, -1 empty (+0xAA8)
    bool playlist_random = false;                    // +0xD70, 0 sequential
    int playlist_selected = 0;                       // +0xD74
    GlyphName playlist_names[playlist_count] = {};   // +0xD78, -1 padded
    std::int16_t sonic_amp_stock[sonic_amp_stock_size] = {}; // +0xC28, catalogue ids
};

// the button config (PlayerConfig at GameState +0x6018, key_config_screen_* 0x27CDA..0x28109).
// The control type word (Standard/Preferred in bit 0, View Change Normal/Reverse in bit 1) is
// player_config_flags_update's of the two stick rows, so it isn't kept separately.
inline constexpr int button_action_count = 8;
// key[] is per action (arm A, arm B, back A, back B, right step, left step, jump, camouflage) the
// pad button: 0 A, 1 B, 2 X, 3 Y, 4 left trigger, 5 right trigger, 6 white, 7 black (D_001DD470)
inline constexpr int default_keys[button_action_count] = {5, 4, 1, 3, 7, 6, 0, 2}; // D_001DD490
struct ButtonConfig {
    int control = 0;     // stick_a: 0 Standard, 1 Preferred
    int view_change = 0; // stick_b: 0 Normal, 1 Reverse
    int beginner = 0;    // f_c: beginner mode 0 off, 1 on
    int keys[button_action_count] = {5, 4, 1, 3, 7, 6, 0, 2};
};
// player_config_flags_update: bit 0 for Standard, bit 1 for View Change Normal
int button_control_type(const ButtonConfig& buttons);

// the player's seat config the save keeps: the camera view and the button config
struct GameOptions {
    int camera_view = 1; // +0x6048, the player's view 0..5
    ButtonConfig buttons;
};

// the options screen's settings that belong to this machine, not the simulation (OptionBits at
// +0x9C8..+0x9CF): kept in State beside the world, never in it, so an online battle's world (the
// host's) can't set a guest's volume (docs/multiplayer.md "Change now", item 4). The save still
// writes them where retail's does (SaveImage::machine, docs/saves.md).
struct MachineOptions {
    int music_volume = 0xFF; // 0..255 (sound_volumes_default)
    int se_volume = 0xE1;    // 0..255
    int screen_shift_x = 0;  // -32..30, 6 signed bits (adjust_screen_*)
    int screen_shift_y = 0;  // -16..14
    int vibration = 0;       // the options' first item, 0 after defaults
    int flag_31 = 0;         // flag_9cc_31: cleared by the defaults, nothing else found
};

struct Profile {
    std::int16_t pilot_name[pilot_name_length] = {}; // the player's alias in font glyphs
    Records records;
    Music music;
    GameOptions options;
};

// --- today's tournaments (RumbleContext rumble[4] at +0x2820) ---

inline constexpr int rumble_count = 4;      // the day's three, then free play's
inline constexpr int queue_capacity = 0x48; // OpponentQueue.tickets
inline constexpr int roster_size = fighter_slots; // retail's 6
inline constexpr int roster_name_bytes = 40; // UTF-8 of the build's name, 0x12 SJIS bytes in retail

// an opponent waiting in a queue (OpponentTicket, 12 bytes): the opponent table's row and the
// progress byte a kill writes (flag_at, a GameState offset in retail; here the role from the
// ticket's role and the byte's place from flag_at). Cleared (OpponentQueue::clear):
// opponent -1, flag_at 0 (pool 0's primary), role -1.
struct RumbleTicket {
    int opponent = -1;
    OpponentTicket progress = {-1, 0, -1, -1};
};

// a queue a tournament draws opponents from (OpponentQueue, 0x370 bytes)
struct OpponentQueue {
    RumbleTicket tickets[queue_capacity]; // only [0, count) is ever read
    int drawable = 0; // how many from the front choose_opponent picks among
    int count = 0;
    int out = 0;      // drawn and in the arena now
    int ready = 0;
};

struct RosterWeapon {
    std::int16_t id = 0;
    std::int8_t tune = 0;
};
// a built machine (Scoobee / BuildRecord less its name pointer)
struct RosterBuild {
    std::int16_t unused_00 = 0; // +0x00 (f_00): no reader
    std::int16_t body = 0;
    std::int8_t body_tune = 0;
    std::int16_t leg = 0;
    std::int8_t leg_tune = 0;
    RosterWeapon weapon[4]; // arm A, arm B, back A, back B
    std::int16_t chip = 0;
    std::int8_t chip_tune = 0;
    std::int16_t color = 0;
    std::int16_t sticker = 0;
    std::int16_t option[10] = {};
};
// the pilot record copied into a roster entry (PilotRecord, 0x1C bytes)
struct RosterPilot {
    std::int16_t identity = 0;
    std::int16_t scoobee_identity = 0;
    std::int16_t team = 0;
    std::int16_t unused_06 = 0; // +0x06 (f_06): no reader
    int value_base = 0; // destruction_value, a CurveU: base, at_mid, at_max, mid, negative
    int value_at_mid = 0;
    int value_at_max = 0;
    std::int8_t value_mid = 0;
    std::int8_t value_negative = 0;
    std::int16_t reaction = 0;
    std::int16_t unused_1a = 0; // +0x1A (f_1a): no reader
};
// one fighter picked for the tournament (RosterEntry, 0x84 bytes). Cleared (fill_opponent_queues):
// all zero and player -2.
struct RosterEntry {
    RosterBuild build;
    RosterPilot pilot;
    std::int16_t level = 0;      // tuning, -99..99
    std::int16_t ai_profile = 0;
    std::int16_t player = -2;    // the pad's player; -1 an AI fighter, -2 an empty slot
    int destruction_value = 0;
    RumbleTicket ticket = {0, {0, 0, -1, -1}}; // +0x64, the ticket it was drawn from
    int queue = 0;               // 1 + the queue it was drawn from
    FxVec3 start;                // a position to enter at instead of a door (float in retail)
    bool start_set = false;
    char name[roster_name_bytes] = {}; // names[6][0x13], the build's name
};

// one RumbleContext (0xDF0 bytes); its tournament is Story.lineup[slot]
struct RumbleSlot {
    OpponentQueue queues[3]; // 0 not yet beaten, 1 beaten (rematches), 2 the reserve pool
    int regular_quota = 0;
    int entrants = 0;        // fighter slots, the player's included
    RosterEntry roster[roster_size];
    bool fixed_queued = false;
    bool pool_queued[8] = {};
};

// --- a new game ---

// game_state_new_garages and garages_clear (and selected_garage to -1)
void garages_new(Garages& garages);
// the non-story parts of game_state_new: the camera view and buttons to their defaults
// (the machine's options are the caller's: MachineOptions{}), the pilot
// name "Ｍｒ．ＵＳＡ", the music player (music_state_new, the stock left empty for the
// shop's roll), records cleared, camera view 1
void profile_new(Profile& profile);
// a RumbleContext as the boot leaves it: all zero but the queues' cleared tickets and empty rosters
void rumble_slot_clear(RumbleSlot& slot);
// game_state_new for the whole world's saved part: story_boot, garages, profile, rumble
void game_state_new(World& world, const StoryTables& tables);
// player_config_commit: the saved button config (GameState +0x6018) to a player's session
// copy, which the fighters read (on a load and when key config closes; player 0 only in retail)
void player_config_commit(World& world, int player);
