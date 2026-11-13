#pragma once
// The garage-side menus' payloads (decomp docs/port/screens.md): the system menu (0x27), key
// config (0x10), screen adjust (0), records (0x1D), the help menu (0x13), Sonic Amp (0x14), the
// media player (0x21), the dealer (0x22), and the globals they keep between visits. The save menu
// (0x1F) uses the load screen's SaveScreen.
#include "screens/system/trophy_models.hpp"
#include "screens/ui/screen_text.hpp"
#include "talk/talk_task.hpp"
#include "world/game_state.hpp"

#include <cstdint>
#include <vector>

struct FrontEnd;
struct Ui;

// system_screen_* (0x27), payload 0x168C bytes
struct SystemScreen {
    int from = 0;
    int cursor = 0;
    int banner = -1; // +0x30, the garage banner's timer
    TextStrip help[5];
};

// key_config_screen_* (0x10)
struct KeyConfigScreen {
    int row = 0;
    ButtonConfig config; // the working copy of GameState +0x6018
    TextStrip help[13];
};

// adjust_screen_* (0)
struct AdjustScreen {
    int from = 0;
    int x = 0, y = 0;             // the live shift
    int entry_x = 0, entry_y = 0; // the shift on entry
    int answer = 1;               // 0 apply, 1 discard
    MsgBox box;
};

// a trophy the records screen draws this update (record_model_draw): the model of
// record_trophy_models at a menu-camera point, its pitch, and the shared spin
struct RecordTrophy {
    int model = 0; // index into record_trophy_models (1269, 1266..1268, 1273, 1270..1272)
    float at[3] = {};
    float pitch = 0;
    float spin = 0;
};

// record_* (0x1D), payload 0x120 bytes
struct RecordScreen {
    int menu = 0;       // +0: 0 rumble, 1 tournaments, 2 game, 3 exit
    int from = 0;       // +4
    int page = 0;       // +8: the tournament page's cursor, 0..2 an area, 3 the others, 4 back
    int leave = 0;      // +0x10: the leave fade's count
    int col = 0;        // +0x14, +0x18: the area grid's cursor
    int row = 0;
    int side_col = 0;   // +0x1C, +0x20: the other tournaments' cursor
    int side_row = 0;
    int clock = 0;      // +0x24: the page clock, updates
    int grid_clock = 0; // +0x28
    bool side_a = true; // +0x30: the other tournaments' first sheet
    int flip = 0;       // +0x34: the sheet turn's close and open
    float spin = 0;     // +0x64: the trophies' turn
    float percent = 0;  // +0x6C: record_percent at entry
    TextStrip help;     // the help line's own task (text_strip_task_spawn), killed on leaving
    bool help_alive = false;
    GlyphText units[8];  // record_unit_strings[0..7] as glyphs (+0xA0)
    GlyphText digits[10];
    // per tournament: its name, its fixed opponent's portrait and Scoobee portrait (-1 none)
    std::vector<GlyphText> names;
    std::vector<int> portrait, scoobee;
    std::vector<RecordTrophy> trophies; // this update's
    TrophyModels models;                // their 3D, freed when the screen goes
};

// pepper_talk_items rows (tables/pepper_talk.sexp)
struct HelpItem {
    int parent = 35; // 35 the top level, else the folder row's index
    int state = 0x9f; // 0x9F a folder (and End)
    GlyphText label;
};

// pepper_talk_* (0x13), the help menu: payload PepperTalk 0x9CE8 bytes
struct HelpScreen {
    int from = 0;
    std::vector<HelpItem> items;
    int count = 0;  // the page's rows (TalkList.count)
    int shown = 0;  // min(count, 5)
    int cursor = 0; // the row under the cursor, wrapped (TalkList.cursor)
    int next = 0;   // the state the leave goes to
    int banner = -1; // +0x9CB4
    // Pepper's portrait: the three-command script at pepper_talk_script on the talk interpreter
    TalkScript script;
    TalkInterp interp;
};

// the names the dealer and the media player hand to name entry (name_entry_target_set):
// kind 2 a bought Scoobee's chip (garage), kind 3 a playlist (playlist); -1 none set
struct NameEntryTarget {
    int kind = -1;
    int garage = -1;
    int playlist = -1;
};

// globals the original keeps outside a screen's payload for the session (not saved)
struct MenuGlobals {
    int selected_save = 0; // GameState +0x113C0: the slot last loaded or saved
    // the help menu's cursors (pepper_talk_saved_sels sel, pepper_talk_saved_tops top,
    // pepper_talk_saved_depth depth, talk_page) and where it was opened from
    // (pepper_talk_came_from)
    int help_sel[3] = {}, help_top[3] = {};
    int help_depth = 0;
    int help_page = 35;
    int help_came_from = 0;
    int free_play_lesson = -1; // GameState.free_play_lesson: 0..3 a lesson, -1 plain free play
    // the dealer (dealer_index 0x415E00 from the town map; the three values it keeps on leaving
    // for the spec page or name entry)
    int dealer_index = 0;
    int dealer_return_menu = 0;
    int dealer_return_row = -1;
    float dealer_return_card_fade = 1.0f;
    NameEntryTarget name_entry;
    // the spec page's machine (GameState +0x10F5C): the dealer's offer row of scoobees.sexp
    int spec_build = -1;
};

// menu_dispatch.cpp: the front end's hooks for every state above; false for a state not theirs
bool menu_screens_enter(FrontEnd& fe, Ui& ui, int state);
bool menu_screens_step(FrontEnd& fe, Ui& ui);
bool menu_screens_destroy(FrontEnd& fe, Ui& ui);
// the state's update: the screen's answer to the next state
bool menu_screens_next(const FrontEnd& fe, int result, int& next);
// once an update, whatever the state: the screen adjust's shift
void menu_screens_frame(FrontEnd& fe, Ui& ui);

// system_screens.cpp: 0x27 system menu, 0x1F save menu
void system_spawn(FrontEnd& fe, Ui& ui);
void system_step(FrontEnd& fe, Ui& ui);
void system_destroy(FrontEnd& fe, Ui& ui);
void save_menu_spawn(FrontEnd& fe, Ui& ui);
void save_menu_step(FrontEnd& fe, Ui& ui);
void save_menu_destroy(FrontEnd& fe, Ui& ui);

// key_config_screens.cpp: 0x10 key config, 0 screen adjust
void key_config_step(FrontEnd& fe, Ui& ui);
void key_config_destroy(FrontEnd& fe, Ui& ui);
void adjust_spawn(FrontEnd& fe, Ui& ui);
void adjust_step(FrontEnd& fe, Ui& ui);
void adjust_destroy(FrontEnd& fe, Ui& ui);

// record_screens.cpp, record_grids.cpp: 0x1D records
void record_spawn(FrontEnd& fe, Ui& ui);
void record_step(FrontEnd& fe, Ui& ui);
void record_destroy(FrontEnd& fe, Ui& ui);
// record_percent: (done rows not marked repeatable + tournaments won of
// record_percent_tournaments) / (their counts) x 100, unrounded
float record_percent(FrontEnd& fe, Ui& ui);
void record_tables_load(FrontEnd& fe, Ui& ui, RecordScreen& r);
void record_header_draw(FrontEnd& fe, Ui& ui);
// record_color_lerp: a to b by t per channel, b's alpha
std::uint32_t record_color_lerp(std::uint32_t a, float t, std::uint32_t b);
// a panel unrolling downwards (y scale = fade), centred
void record_unroll_draw(Ui& ui, int id, float x, float y, float fade, int blend);
// record_grids.cpp: the tournament pages
void record_tournament_page(FrontEnd& fe, Ui& ui);
void record_area_grid(FrontEnd& fe, Ui& ui);
void record_side_grid(FrontEnd& fe, Ui& ui);
// help_screens.cpp: 0x13 the help menu (Pepper's talk)
void help_spawn(FrontEnd& fe, Ui& ui);
void help_step(FrontEnd& fe, Ui& ui);
void help_destroy(FrontEnd& fe, Ui& ui);

// the record steps (Screen.step)
enum RecordStep : int {
    record_step_load = 0,
    record_step_fade_in,
    record_step_menu,
    record_step_rumble,
    record_step_game,
    record_step_tournaments,
    record_step_area_grid,
    record_step_side_grid,
    record_step_leave,
    record_step_done,
};
