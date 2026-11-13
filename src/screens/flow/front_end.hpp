#pragma once
// The application state machine (decomp docs/port/architecture.md "Application states"): one
// AppState task whose state number picks its entry (app_state_entry_table) and update,
// and the screen task each entry spawns. A screen is a chain of steps; it answers its state by
// setting `result` (task_header_set), and the state's update turns the answer into the next state
// (menu_set_state), which kills the screen, runs app_transition_resolve (so conversations ride on
// state changes) and enters the new state on the next update. State numbers are the original's.
#include "input/menu_input.hpp" // menu_cursor's flags
#include "movie/movie.hpp"
#include "movie/movie_screens.hpp"
#include "screens/flow/screens.hpp"
#include "screens/pc/report_panel.hpp"
#include "screens/shops/music_player.hpp"
#include "screens/ui/ui.hpp"
#include "screens/vs/vs_setup.hpp"
#include "story/story.hpp"
#include "story/story_tables.hpp"
#include "talk/talk_skip.hpp"
#include "talk/talk_task.hpp"
#include "world/app_states.hpp"
#include "world/game_state.hpp"

#include <array>
#include <string>
#include <vector>

struct World;

// per state, from data/tables/states.sexp (src/data/state_tables.c)
struct StateInfo {
    std::string name;
    int cadence = 60;           // frame_cadence_table
    bool pause_allowed = false; // state_pause_allowed
    int restart = 0x28;     // app_state_restart_table (a dead debug reset)
    int music = -1;         // state_music
    int pad_pause = 0;      // state_pad_pause
};

struct FrontEnd {
    std::array<StateInfo, app_state_count> states;
    // AppState: result, state, previous (menu_request_state also writes the globals)
    int state = app_none;
    int previous = app_none;
    int entering = app_phantagram_logo; // the entry to run on the next update, -1 none
    int result = -1;           // the screen's answer, -1 until it has one
    Screen screen;
    bool screen_alive = false;
    bool screen_new = false; // spawned this update: a task made during the frame runs from the next
    // globals the screens keep between states
    int options_entry_state = 0;   // D_00415DF8: options opened from 0x15 or 0x27
    bool second_logo_shown = false; // D_00415D9E: logo_second_draw sets it
    bool nested = false;           // 0x1C has made its inner machine (new_game_spawn)
    std::string save_dir; // <pref path>/saves (src/save)
    MenuGlobals menu;     // the garage-side menus' session globals (menu_screens.hpp)
    MusicPlayer music;    // the catalogue, the battle playlist, the menus' shuffle, the stock
    ExtraGlobals extra;   // the media player's rename copy, free play's setup, the pad watch
    // the parts other agents own that the screens use
    World* world = nullptr;
    MachineOptions* machine = nullptr; // this machine's options (State::machine), not the world's
    StoryTables story_tables;
    MoviePlayer movies;
    std::vector<EndingSubtitle> subtitles;
    TalkContext talk_context;
    TalkScript talk_script;
    TalkTask talk;
    // PC: conversation skips (src/talk/talk_skip.hpp, docs/story.md "Skipping")
    TalkSkip talk_skip;              // the skip binds over the conversation up now
    bool talk_chain_skip = false;    // a group skip: the rest of the chain runs unseen
    bool story_talk_skipped = false; // "Story conversations: Skip" (main sets it every update)
    int updates = 0; // updates run, for --press scripts
    // the part tables, the garage being edited, the name entry's target, the spec page's build,
    // Wild Machines' saved menu and the model stand (src/screens/flow/garage_screens.hpp)
    ShopGlobals shop;
    // VS: who sits where (main's, src/input/seats.hpp), the VS setup's session, GameState.vs_mode
    Seats* seats = nullptr;
    VsSession vs;
    bool vs_mode = false;
    Campaign campaign; // the day round (screens/campaign/campaign.hpp)
    // the PC's settings (main's) and the letters PC CONFIG cuts from the original's art
    PcHost* pc = nullptr;
    PcArt pc_art;
    ReportPanel report; // the bug reports' panel over any screen
    // PC CONFIG open over a paused fight (screens/pc/pause_settings.hpp), on screen.pc
    bool pause_settings = false;
};

bool init_front_end(FrontEnd& fe, Ui& ui, World& world, MachineOptions& machine,
                    std::string& error);
// every SDL event before the pads and the pointer: true when a screen took it (PC CONFIG
// listening for a binding)
bool front_end_event(FrontEnd& fe, Ui& ui, const SDL_Event& event);
// one 60 Hz update of the AppState task and the task under it, then the fade (main_loop_frame's
// order: tasks_run, then screen_fade_frame); the pads have been read for it
void front_end_update(FrontEnd& fe, Ui& ui);
// what read_pads needs: a fade blanks the pads (pads_read_mask 0)
bool front_end_pads_blank(const FrontEnd& fe, const Ui& ui);
void shutdown_front_end(FrontEnd& fe, Ui& ui);

// state_dispatch.cpp: the state's entry (menu_request_state, menu_input_mark, the screen spawned),
// its screen's step and end (task_kill), and its update: the screen's answer to the next state
void front_end_enter_state(FrontEnd& fe, Ui& ui, int state);
void front_end_step_screen(FrontEnd& fe, Ui& ui);
void front_end_destroy_screen(FrontEnd& fe, Ui& ui);
void front_end_update_state(FrontEnd& fe, Ui& ui);

// menu_set_state: kill the screen, resolve the conversations, set the entry
void menu_set_state(FrontEnd& fe, Ui& ui, int state);
// PC (talk_skips.cpp, docs/story.md "Skipping"): the conversations a group skip or "Story
// conversations: Skip" passes over, each run unseen and the resolver asked again from it as its
// 0x9F would ask, until the chain lands on a state that isn't a trigger conversation (help talks
// always show); returns that state. The last policy's music and the black screen are kept.
int talk_chain_skip(FrontEnd& fe, Ui& ui, int state);
// would the chain go on past the talk state up now (the hint's "skip all")
bool talk_chain_continues(const FrontEnd& fe);
// debug: PHANTOMCRASH_STORY_DUMP=path writes the world's saved part and the story's unsaved fields
// at shutdown, to diff a run with conversations shown against one with them skipped
void story_dump(const FrontEnd& fe);
// debug: PHANTOMCRASH_TALK_CHAIN="SRC DST won A-B done C-D" (hex states, decimal ranges) starts a
// new game with tournaments A..B won and trigger rows C..D done and makes the change SRC -> DST
// from black, so a chain of conversations can be played or skipped headless
void talk_chain_test(FrontEnd& fe, Ui& ui);
// the options screen's volumes, screen shift and vibration (GameState +0x9C8..), this machine's
// (State::machine); the camera view and buttons are the world's profile.options
MachineOptions& game_options(FrontEnd& fe);
// the pilot's name for the conversations' -5 (from the world's profile)
void talk_pilot_name_update(FrontEnd& fe);
// the screen's answer
// decomp: task_header_set 0xA1B5A
inline void task_header_set(FrontEnd& fe, int result) {
    fe.result = result;
}
// name_entry_target_pilot, name_entry_target_garage, name_entry_target_set
void name_target_pilot(FrontEnd& fe);
// PHANTOMCRASH_DEV_BUILDS and PHANTOMCRASH_DEV_MONEY (port-only, headless runs) on the world
void dev_world_seed(FrontEnd& fe);
// PHANTOMCRASH_DEV_WON, PHANTOMCRASH_DEV_BEATEN and PHANTOMCRASH_DEV_DAY (dev_seed.cpp)
void dev_story_seed(FrontEnd& fe);
void name_target_garage(FrontEnd& fe);
void name_target_set(FrontEnd& fe, int kind, std::int16_t* buffer);
// state_music_play: the state's track looped, or a stop for -1
void state_music_play(FrontEnd& fe, Ui& ui, int state);
// the pad's accept or a left click on rect (anywhere when rect is null); cancel or a right click
bool menu_accept(Ui& ui, const PointerRect* rect = nullptr);
bool menu_cancel(Ui& ui);
// pads_cursor_move for player 0 with the screen's sound, and the pointer: hovering
// rects[i] moves the cursor to lo + i with the same sound. Returns the step.
int menu_cursor(Ui& ui, int& value, int lo, int count, unsigned flags, int bank, int cue,
                const PointerRect* rects = nullptr);
// PC: the pad for a screen that turns pages on the triggers: the mouse's buttons press them
// through the bindings (they fire the arms in a fight), so while a click lands or the left
// button is held they are taken off, and a click is not a page turn (name_entry.cpp does the same)
PadRecord menu_pad_unclicked(const Ui& ui);
// PC: the wheel on a cursor the d-pad's up and down don't move (a horizontal menu, a wheel of
// items): away from the player -1, towards +1, wrapping or stopping at the ends, with the screen's
// sound (none for bank -1). Returns the step.
int menu_wheel(Ui& ui, int& value, int lo, int count, bool wrap, int bank, int cue);
// PC: the pointer on a scrolling list (menu_list_move's cursor and scroll): entering row i's
// rectangle lights it, a click on `up` or `down` (null for none) steps the cursor as the d-pad
// does, scrolling at the edge. Returns the step; the screen plays its sound and marks its input
// time as it does for the pad.
int menu_list_pointer(Ui& ui, int& cursor, int& scroll, int total, int rows, unsigned flags,
                      const PointerRect* row_rects, const PointerRect* up, const PointerRect* down);
