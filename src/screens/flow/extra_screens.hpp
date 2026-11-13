#pragma once
// The screens ported after the garage group (decomp docs/port/screens.md): Sonic Amp (0x14), the
// media player (0x21), the dealer (0x22), the ending's movie states (0xB, 0x25, 0xC), free play
// and the lessons (0x2A, the battle entry's), and the lost-controller box over the menu states.
// Their payloads, the globals they keep between visits, and their dispatch.
#include "screens/ui/screen_text.hpp"
#include "screens/ui/screen_widgets.hpp"
#include "talk/talk_interp.hpp"
#include "talk/talk_script.hpp"
#include "world/game_state.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct FrontEnd;
struct Ui;

// sonic_amp_* (0x14), SonicAmp (0x1474 bytes); names as the doc's "names that mislead" correct them
struct SonicAmpScreen {
    int from = 0;
    int menu = 0;     // +4: 0 buy, 1 listen, 2 back
    int chart = 0;    // +8: tab - 1
    int tab = 0;      // +0x14: 0 picks, 1..3 charts, 4 leave
    bool in_list = true; // +0x1D: the grid, else the buy/listen/back menu
    int col = 0, row = 0, page = 0;
    int arrow_left = 0, arrow_right = 0; // the page arrows' flash
    int listen_frames = 0;  // +0x34
    int tip_frames = 0;     // the RANK ticker's 11 s
    int tip = 9;            // the rank shown, counting down
    int message_frames = 0; // +0x3C
    int message = 0;        // +0x40
    int fade_frames = 0;    // +0x44
    int zoom = 15;          // +0x48 reveal_frames: the zoom panel's counter
    bool on_leave = false;  // +0x4C leaving: the tab cursor is on leave
    int title_frames = 0;   // +0x54
    int arrows_a = 0, arrows_b = 0; // +0x58, +0x5C
    int playing = -1;       // +0x60
    int intro_frames = 0;
    int picks[11] = {};     // sonic_amp_picks: today's stock
    TextStrip ticker;       // +0x90 (help)
    TextStrip artist;       // +0x508, built and never drawn
    TextStrip banner;       // +0x980 (bio): "Best Selection in Sonic Amp"
    TextStrip title;        // +0xDF8: the cell under the cursor
    // the song's bio on the interpreter (bio_task +0x8C)
    TalkScript bio_script;
    TalkInterp bio;
    bool bio_alive = false;
};

// media_* (0x21), MediaScreen (0x7E8 bytes)
struct MediaScreen {
    int from = 0;
    int menu = 0;        // +4: 0 choose, 1 edit, 2 RANDOM, 3 rename, 4 exit
    int list = 0;        // +8: the panel's list
    int list_before = 0; // +0xC: the playing list
    int col = 0, row = 0, page = 0; // +0x10.. the catalogue's cursor
    int arrow_left = 0, arrow_right = 0;
    int counts[playlist_count] = {};
    int tracks[playlist_count][playlist_length] = {}; // +0x38
    int total = 0, pages = 1;
    int ids[81] = {};    // +0x1C4: the owned ids in order, -1 after
    int fade_frames = 0; // +0x308
    int random = 0;      // +0x30C
    int random_choice = 0; // +0x310
    int preview_frames = 0; // +0x31C
    int panel_clock = 0;    // +0x320
    int edit_clock = 0;     // +0x324
    int sweep = 0;          // +0x328, 4 s
    int bars = 0;           // +0x32C, 1 s
    int nudge = 0;          // +0x330
    int transition = 0;     // +0x334
    int label_pop = 0;      // +0x338
    bool nudge_down = false; // +0x33C
    bool random_box = false; // +0x33D
    int question = 0;        // +0x33E: 1 asked, 2 kept
    TextStrip text;          // +0x340: the track's title in the catalogue
    TextStrip help;          // +0x7E4: the blank help line's own task (options_help)
    bool help_alive = false;
    TalkScript bio_script;
    TalkInterp bio;
    bool bio_alive = false;
};

// dealer_* (0x22), payload 0x2DDC bytes (no struct in the decomp; offsets in the comments)
struct DealerScreen {
    int from = 0;          // +0
    int fade = 0;          // +4
    int cursor = 0;        // +8
    int cursor_was = 0;    // +0xC
    int item_before = 0;   // +0x10
    int row = -1;          // +0x14: the machine under the cursor, -1 none
    int sub = 0;           // +0x18: the machine submenu
    int money = 0;         // +0x4C: the copy the screen works on
    int result = 0;        // +0x50: what the leave answers
    int dealer = 0;        // +0x54
    int wait = 0;          // +0x58
    float intro = 0;       // +0x5C
    float plate = 0;       // +0x60: the menu plate
    int pop = 0;           // +0x64: the profile panel
    int frame = 0;         // +0x70: the backdrop loop
    float scroll = 0;      // +0x74
    float trail[15] = {};  // +0x78
    float card_fade = 1;   // +0xB4
    int message = 0;       // +0xBC
    int pick = 1;          // +0xC0: board yes/no
    int garage = 0;        // +0xC4: the wheel's cursor
    int garage_was = 0;    // +0xC8
    bool eligible[garage_count] = {}; // +0xCC
    int slide = 8;         // +0xD8
    int bought = -1;       // the garage the machine went to
    int machines[3] = {-1, -1, -1};  // the day's offers: scoobees.sexp rows
    int cards[3] = {-1, -1, -1};     // their cards (dealer_machine_card_sets by display)
    int buttons[5][2] = {};          // the menu buttons, lit and plain (1..3 by the legs)
    TextStrip strips[5];   // +0x5A4
    TextStrip sub_strips[3]; // +0x1BFC
    TextStrip wheel_strip;   // +0x2964
    MsgBox boxes[9];
    TalkScript talk_script;  // the maker profile (+0x48)
    TalkInterp talk;
    bool talk_alive = false;
};

// the battle entry free play hands over (free_play_screen_update): who fights where.
// The campaign's battle entry reads this; tournament and builds index its tables.
struct FreePlaySetup {
    int lesson = 3;        // GameState.free_play_lesson: 0 basic controls, 1 attacks, 2 camouflage,
                           // 3 plain free play
    int phase = 4;         // GameState.battle_phase
    int selector = 6;      // rumble_selected: rumble[3]
    int tournament = 0;    // fill_opponent_queues: 0 for lessons 0 and 3, 55 for 1 and 2
    int build = 0x411;     // rumble_scoobee_loadout_at(0x411) for roster entry 0 (player 0, team 0)
    bool second = false;   // lesson 2: entry 1 the same build, an AI (player -1) on team 0xB
    int second_team = 0xb;
    int scene = 0x18;      // the player's entrance: tutorial scene 0x18, 0x23, 0x2A; -1 a gate
    bool default_pads = true; // player_config_load_defaults_a unless lesson 3
};

// the lost-controller box (pad_missing_*, decomp "Lost controller")
// the lost-controller watch (pad_watch.cpp): outside VS, player 0's pad, the one that pressed
// START on the title return (title_return_screen_step3 sets GameState.input_mask to its port)
struct PadWatch {
    int watched = -1;           // its SDL joystick id; -1 none (the keyboard or the mouse)
    bool lost = false;          // it has gone
    std::vector<int> pads_left; // the pads still here when it went: one plugged in since is it
    bool forced = false;        // PHANTOMCRASH_DEV_PAD_LOST's pull (headless captures)
    bool up = false;            // kind 1: the box over a menu state
    MsgBox box;
    int drawn = -2; // the front end update the box was last drawn in
};

// what these screens keep between visits (outside a screen's payload)
struct ExtraGlobals {
    // media_rename_start's copy at media_screen_saved, restored on the way back from name entry
    MediaScreen media_saved;
    bool media_saved_valid = false;
    FreePlaySetup free_play;
    PadWatch pads;
};

// pad_watch.cpp: before the screen's step, the lost-controller watch; true blanks the screen's
// pads this update
bool pad_watch_frame(FrontEnd& fe, Ui& ui);
// after the screen's step: the reconnect box over it
void pad_watch_draw(FrontEnd& fe, Ui& ui);
// the title return's START: the pad that pressed it is watched from now on (none for the keyboard)
void pad_watch_title_start(FrontEnd& fe);
// the watched pad is gone (players_without_pad outside VS)
bool pad_watch_lost(const FrontEnd& fe);
// the keyboard took over from a lost pad: nothing is watched until the next title START
void pad_watch_keyboard(FrontEnd& fe);
// dealer_index_set, for the town map's leave: spot 7 Aeron, 6 Photon, 5 Holy
void dealer_index_set(FrontEnd& fe, int spot);
// pad_missing_message_set 0x3B18B1 for the battle pause: flag 1 adds "and press START"
void pad_missing_box_draw(FrontEnd& fe, Ui& ui, int flag);

// sonic_amp.cpp, sonic_amp_draw.cpp
void sonic_amp_spawn(FrontEnd& fe, Ui& ui);
void sonic_amp_step(FrontEnd& fe, Ui& ui);
void sonic_amp_destroy(FrontEnd& fe, Ui& ui);
void sonic_amp_screen_draw(FrontEnd& fe, Ui& ui, SonicAmpScreen& s);
void sonic_amp_banner_draw(Ui& ui, SonicAmpScreen& s);
void sonic_amp_picks_draw(FrontEnd& fe, Ui& ui, SonicAmpScreen& s);
void sonic_amp_chart_draw(FrontEnd& fe, Ui& ui, SonicAmpScreen& s);
void sonic_amp_listen_draw(FrontEnd& fe, Ui& ui, SonicAmpScreen& s);
void sonic_amp_arrows_draw(Ui& ui, SonicAmpScreen& s);

// media_player.cpp, media_draw.cpp
void media_spawn(FrontEnd& fe, Ui& ui);
void media_step(FrontEnd& fe, Ui& ui);
void media_destroy(FrontEnd& fe, Ui& ui);
void media_header_draw(FrontEnd& fe, Ui& ui, MediaScreen& m);
void media_panel_draw(FrontEnd& fe, Ui& ui, MediaScreen& m);
void media_menu_draw(FrontEnd& fe, Ui& ui, MediaScreen& m);
void media_catalogue_draw(FrontEnd& fe, Ui& ui, MediaScreen& m);
void media_preview_draw(FrontEnd& fe, Ui& ui, MediaScreen& m);
void media_list_cursor_draw(Ui& ui, MediaScreen& m);

// dealer.cpp, dealer_draw.cpp
void dealer_spawn(FrontEnd& fe, Ui& ui);
void dealer_step(FrontEnd& fe, Ui& ui);
void dealer_destroy(FrontEnd& fe, Ui& ui);
void dealer_screen_draw(FrontEnd& fe, Ui& ui, DealerScreen& d, bool pulse);
void dealer_machine_menu_draw(Ui& ui, DealerScreen& d);
void dealer_garage_list_draw(FrontEnd& fe, Ui& ui, DealerScreen& d, bool lit);
// price_with_tax of offer i, units of 10 NY
int dealer_price(FrontEnd& fe, const DealerScreen& d, int i);

// ending_screens.cpp: 0xB, 0x25, 0xC through src/movie's screens, free play's setup
// state 0xA, the in-engine ending's scene (ending_scene_enter and its chain)
void ending_scene_spawn(FrontEnd& fe, Ui& ui);
void ending_scene_step(FrontEnd& fe, Ui& ui);
void ending_scene_destroy(FrontEnd& fe, Ui& ui);
void ending_spawn(FrontEnd& fe, Ui& ui, int state);
void ending_step(FrontEnd& fe, Ui& ui);
void ending_destroy(FrontEnd& fe, Ui& ui);
// free_play_screen_update's setup for a lesson (GameState.free_play_lesson)
FreePlaySetup free_play_setup(int lesson);
void free_play_spawn(FrontEnd& fe, Ui& ui);
void free_play_step(FrontEnd& fe, Ui& ui);

// small pieces these screens share (screen_kit.cpp)
// SpriteRow::draw_scaled and draw_anchored 0xBBE73 (kind 0: about the sprite's middle)
void row_scaled(Ui& ui, SpriteSlot row, float sx, float sy);
void row_anchored(Ui& ui, SpriteSlot row, float sx, float sy);
// a table's row (a copy, sprite -1 when missing)
SpriteSlot slot_at(Ui& ui, const char* screen, const char* table, int row);
// the string a data word points at (text/strings.sexp's used-by (data VA)), "" when none
std::string string_at(Ui& ui, unsigned data_va);
// an event script on the interpreter over the screen (bios, the dealers' profiles)
bool script_start(FrontEnd& fe, Ui& ui, TalkScript& script, TalkInterp& interp,
                  const std::string& name);
void script_step(FrontEnd& fe, Ui& ui, TalkScript& script, TalkInterp& interp);
