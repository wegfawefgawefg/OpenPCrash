#pragma once
// The day's round as screens (decomp docs/port/screens.md "The day flow"): the town map (0x29),
// Club Wired (8), area select and the job menu (3), the battle's state (6), the result screen
// (0x1E) and the night (0x18), with what the campaign keeps between them. Each screen's payload
// is the original's task payload by field; the steps are its chain of update functions.
#include "ai/ai_data.hpp"
#include "assets/stage_tables.hpp"
#include "screens/campaign/keep_sv.hpp"
#include "screens/garage/hangar.hpp"
#include "screens/system/trophy_models.hpp"
#include "screens/ui/screen_header.hpp"
#include "screens/ui/screen_text.hpp"
#include "screens/ui/screen_widgets.hpp"
#include "story/roster_tables.hpp"
#include "talk/talk_interp.hpp"

struct FrontEnd;
struct Ui;
struct BattleState;

// town_map_screen_* (0x29), payload 0x2424 bytes
struct TownMapScreen {
    int from = 0;          // +0 the previous state
    int spot = 0;          // +4
    int prev_spot = 0;     // +8
    int flight = 8;        // +0xC flight frames 0..8
    bool mirrored = false; // +0x10
    int fade_count = 0;    // +0x1C
    int frame = 0;         // +0x20 updates since entry
    int changed_at = 0;    // +0x24
    int shown_at = -1;     // +0x28 the fade in ended
    int left_at = -1;      // +0x2C the accept
    bool stick_held = false;  // +0x30
    float stick_angle = 0;    // +0x34 radians
    float stick_start = 0;    // +0x38
    int landed_prev = -1;     // +0x3C
    int landed_at = -1;       // +0x40 -1 while flying
    TextStrip labels[8];      // +0x58
    int banner_timer = -1;    // +0x2418
    int visit_spin = 0;       // +0x241C (no reader)
    float spin_sum = 0;       // +0x2420
    bool spin_drag = false;   // PC: the left button grabbed the skater to spin him
    int result = 0;
};

// club_wired_screen_* (8), payload 0x1AF4 bytes
struct ClubWiredScreen {
    int from = 0;      // +0
    int cursor = 0;    // +4
    int shown = 0;     // +8 the item the wheel shows
    bool turning = false; // +0xC
    int turn_frame = 0;   // +0x10
    int fade_count = 0;   // +0x14
    TextStrip lines[6];   // +0x20
    int banner_timer = -1; // +0x1AF0
};

// day_advance_* (0x18), payload one int
struct NightScreen {
    int counter = 0;
};

// JobMenu (area_select_screen_* and job_*, state 3), payload 0x3ADC bytes
struct AreaStrip {
    int shown = 0;
    int enter = -1; // also the tab slide timer
    int leave = -1;
    int start = -1; // the TV-static switch, task_timer at its start
    int pick = 0;
    bool playing = false; // retail's +0 (the port keeps the area shown in `shown`)
};
// a fighter walking the job map (job_wanderer_*, 0x60 bytes at 0x416610 per roster entry), in
// the port's frame, float as retail's (front-end state; only its route target reaches the world)
struct JobWanderer {
    float pos[3] = {};  // +0
    float dir[3] = {};  // +0xC
    float speed = 0;    // +0x18 10 / frame_cadence a update
    int timer = 0;      // +0x20 updates on the map, or off it
    int body_class = 7; // +0x24
    int leg_kind = 8;   // +0x28
    // the AiRoute (+0x2C): node index, segment, progress, the point steered at (+0x48)
    bool placed = false;
    int node = -1;
    int point_index = 0;
    int point_count = 0;
    float progress = 0;
    float from[3] = {};
};

struct AreaSelectScreen {
    int from = 0;         // +0
    int area = 0;         // the area cursor
    int previous_area = 0;
    int item = 0;         // the job page's cursor
    int gate = 0;
    int character = 1;    // +0x64
    int frames = 0;       // the fade counts
    int mode = 0;         // area_select_draw's mode: 0 still, 1 to the job page, 2 back
    int slide_start = 0;  // task_timer at a page change
    int map_clock = 0;    // job_menu_enter's clock
    int banner_timer = -1; // +0x38
    int ranker_gate[4] = {}; // +0x70
    int jobs[4] = {-1, -1, -1, -1};
    bool fee_ok[4] = {};
    bool entry_ok = false;
    int box = -1;         // the refusal box open, -1 none
    int box_return = 0;   // the step it returns to
    int result = 0;
    int leave_page = 0;   // area_select_leave's page: 1 areas, 0 jobs lit, 2 jobs and gates
    PopTimer card;        // +0x3A98: the card's pop
    int card_up = 0;      // 1 CHARACTERS, 2 CONTEST INFO (their help lines)
    JobWanderer wanderers[6];
    AreaStrip strip;
    TextStrip area_help[4], item_help[6], gate_help, character_help, contest_help;
    TextStrip* help = nullptr; // the line drawn
    MsgBox boxes[7];
    bool rented = false;
    int page = 0;        // 0 the area page, 1 the job page
    int page_change = 0; // task_timer at a slide between the pages
    PointerRect tab_rects[4], item_rects[6], gate_rects[3];
};

// ResultScreen (0x1E), payload 0x564 bytes
struct ResultScreen {
    int from = 0;
    int countdown = 16;
    int choice = 0;
    float clock = 0;        // ticks
    float trophy_clock = 180;
    float timer = 0;
    int rank = 1;
    int trophy = -1;
    int settlement = 0;
    int shown_destruction = 0, shown_elapsed = 0, shown_prize = 0;
    int shown_weapon_repair[4] = {};
    int shown_body_leg_repair = 0, shown_repair_total = 0;
    int rows[11] = {};
    float trophy_yaw = 0;
    TextStrip text;
    TrophyModels models; // the trophy's, freed when the screen goes
};

// state 6's own task (battle_spawn's 0xC bytes: the result word and a counter) and the pause
struct BattleScreen {
    int counter = 0;
    int result = 0;      // +4: battle_result_set
    int answer = -1;     // what the header gets
    // VS's pause: the quit question (pause_quit_open, pause_quit_cursor[1], vs_quit_box)
    bool quit_open = false;
    int quit_cursor = 1;
    MsgBox quit_box;
};

// the ending's scene (ending_scene_spawn's 0xC bytes: a counter, the battle, a timer) and what
// presentation follows of the player's shots
struct EndingSceneScreen {
    int counter = 0;
    std::uint32_t tint_start = 0; // task_timer as the last shot ended (timer_start_a)
    int shot_state = -1, shot_step = -1;
    float shot_timer = 0;
    // the dissolve after a shot with a capture (frame_capture_fade_draw): its start (task_timer)
    // and length in ticks, 0 none
    std::uint32_t dissolve_start = 0;
    int dissolve_ticks = 0;
};

// the replay's own task (replay_spawn's 0x14 bytes: its wait; the view cursor is Campaign's)
struct ReplayScreen {
    int wait = 0x10; // +4
};

// schedule_* (0x20), payload 0x23F3C bytes
struct ScheduleScreen {
    int from = 0;        // +0
    int ids[3][60] = {}; // +8 every day's tournament per area slot
    int col = 0;         // +0x2D8 the day column
    int row = 0;         // +0x2DC the area row, 3 Exit
    int first = 0;       // +0x2E0 the first day shown
    int page = 0;        // +0x2E4
    int timer = 0;       // +0x2E8
    int fade_a = 0, fade_b = 0; // +0x2EC, +0x2F0
    int shift_px = 0;    // +0x2F4
    int anim = 0;        // +0x2FC a day scrolling in: 1 squeeze, 2 slide, 3 grow
    int anim_timer = 0;  // +0x300
    int shift = 0;       // +0x304 days still to scroll, signed
    float alpha = 1;     // +0x308 the day numbers' fade
    bool card = false;   // +0x30C
    int card_delay = 0;  // +0x310
    TextStrip title;     // +0x33C
    bool leaving = false, fade_step = false;
    PointerRect cells[3][5], exit_rect;
};

struct CampaignScreens {
    TownMapScreen town;
    ClubWiredScreen club;
    NightScreen night;
    AreaSelectScreen area;
    ResultScreen result;
    BattleScreen battle;
    HangarScreen hangar;     // 0xE (hangar.hpp)
    ScheduleScreen schedule; // 0x20 (campaign_dispatch.cpp's)
    ReplayScreen replay;     // 5 (replay_screen.cpp)
    KeepSvScreen keep_sv;    // 0x2B (keep_sv.cpp)
    EndingSceneScreen ending_scene; // 0xA (ending_screens.cpp)
};

// the battle's scene text playing (scene_text.cpp): the world's serial it answers to
struct SceneTextView {
    int serial = -1;
    bool running = false;
    TalkInterp interp;
    TalkScript script;
};

// what the campaign keeps across states (FrontEnd::campaign) and hands main's battle session
struct Campaign {
    RosterTables roster;
    int hangar_return_area = 0; // GameState.hangar_return_area: the job page's Change the Scoobee
    int hangar_mode = 0;        // GameState +0x10FBC: the hangar's mode kept while it's away
    int rental_build = -1;      // rental_build_id: 0x54..0x57 for a RENTAL
    // main's battle session (src/battle_run.hpp): the state asks, main loads and runs it
    // 1 a fight (state 6), 2 the result screen's demo battle, 3 the replay (state 5)
    int battle_wanted = 0;
    bool battle_running = false;
    bool battle_stop = false; // the state that showed it is gone
    bool paused = false;      // the pause task is up (world steps stop)
    int pause_cursor = 0;
    // the lost-pad pause (pause_for_missing_pad kind 2): the watched seats whose device is gone
    // (bit per seat), and those that were last step, so a pull pauses once
    unsigned pads_missing = 0;
    unsigned pads_missing_before = 0;
    int battle_phase = 0;     // the phase the next fight starts in (free play's 4)
    // the replay: its camera view (replay_view, 0..5) for main's session, and its end (the
    // record or the fight ran out, or there was none) back from it
    int replay_view = 5;
    bool replay_done = false;
    KeepSvKept keep_sv; // KEEP SV's window and cursor for its next visit
    // the attract demo's: the tournament its demo battle draws from (-1 today's), and its
    // updates left (attract_demo_enter's frame_cadence x 60)
    int demo_tournament = -1;
    int attract_left = 0;
    // the job map's walkers' ground: the area's day stage route graph and entry points
    // (ai_nav_resource_select(area * 2), stage_entry_points)
    NavGraph job_nav;
    int job_nav_stage = -1;
    StageTables stages;
    bool stages_loaded = false;
    // PC: the scene text (tutorial lines) that ended, for the world's next input (battle runs)
    int scene_text_done = -1;
    SceneTextView scene_text;
};

// the hub screens' shared pieces (hub_widgets.cpp)
// the day, money, numbers and garage banners are screen_header.hpp's
// time_draw_seconds: hh:mm:ss of a count of 60 Hz ticks... seconds
void time_draw_seconds(Ui& ui, int ticks, int x, int y, int blend, std::uint32_t color, int font);
// date_draw: yyyy.mm.dd in a number font from x (the font's glyph 14 between)
void date_draw(Ui& ui, int year, int month, int day, int x, int y, int blend, std::uint32_t color,
               int font);
// time_hms_draw: hh:mm:ss in a number font from x
void time_hms_draw(Ui& ui, int hours, int minutes, int seconds, int x, int y, int blend,
                   std::uint32_t color, int font);
// the table string a data address points at (text/strings.sexp used-by), "" for none
std::string string_at_data(Ui& ui, unsigned address);
// a table string (UTF-8) to the font's glyphs
GlyphText glyphs_of(Ui& ui, const std::string& text);

// town_screens.cpp: 0x29 town map, 0x18 the night
void town_map_spawn(FrontEnd& fe, Ui& ui);
void town_map_step(FrontEnd& fe, Ui& ui);
void town_map_destroy(FrontEnd& fe, Ui& ui);
void night_step(FrontEnd& fe, Ui& ui);
void night_destroy(FrontEnd& fe, Ui& ui);
// club_wired.cpp: 8
void club_wired_spawn(FrontEnd& fe, Ui& ui);
void club_wired_step(FrontEnd& fe, Ui& ui);
void club_wired_destroy(FrontEnd& fe, Ui& ui);
// area_select.cpp: 3
void area_select_spawn(FrontEnd& fe, Ui& ui);
void area_select_step(FrontEnd& fe, Ui& ui);
void area_select_destroy(FrontEnd& fe, Ui& ui);
// result_screen.cpp: 0x1E
void result_screen_spawn(FrontEnd& fe, Ui& ui);
void result_screen_step(FrontEnd& fe, Ui& ui);
void result_screen_destroy(FrontEnd& fe, Ui& ui);
// battle_screen.cpp: 6, its pause
void battle_screen_spawn(FrontEnd& fe, Ui& ui);
void battle_screen_step(FrontEnd& fe, Ui& ui);
// free play's pause quit (free_play_screen_step3): the fight ends, the fade, answer 0
void battle_screen_quit(FrontEnd& fe, Ui& ui);
void battle_screen_destroy(FrontEnd& fe, Ui& ui);
// the pause toggle (pause_toggle_update): Start on any pad in a battle that may pause,
// and the lost-pad watch (app_pad_watch, kind 2); called by main every step the battle runs
void battle_pause_toggle(FrontEnd& fe, Ui& ui, bool start_pressed);
// VS's pause menu (pause_menu_update's VS arm): continue, or quit with its question, on every
// entered seat's pad
void vs_pause_step(FrontEnd& fe, Ui& ui);
// pause_menu_update's mask: player 0's pad, or in VS every entered seat's at once
PadRecord pause_pads(const FrontEnd& fe, const Ui& ui);
// battle_letterbox_draw
void letterbox_draw(Ui& ui, const BattleState& b);
// the battle flow's sounds and music of the step just run (World::battle_state.events)
void battle_events_play(FrontEnd& fe, Ui& ui);
// scene_text.cpp: the battle's scene text (the lessons' lines), once a front end update
void scene_text_step(FrontEnd& fe, Ui& ui);
// every lesson table entry's address that points at a script, sorted (BattleData::scene_text_scripts)
std::vector<int> scene_text_addresses(Ui& ui);
// replay_screen.cpp: 5
void replay_screen_spawn(FrontEnd& fe, Ui& ui);
void replay_screen_step(FrontEnd& fe, Ui& ui);
void replay_screen_destroy(FrontEnd& fe, Ui& ui);
// schedule_screen.cpp: 0x20
void schedule_spawn(FrontEnd& fe, Ui& ui);
void schedule_step(FrontEnd& fe, Ui& ui);
void schedule_destroy(FrontEnd& fe, Ui& ui);
// campaign_dispatch.cpp: the hangar (0xE), the schedule (0x20), the test drive (0x2F), keep SV
// (0x2B), the replay (5) and the ending's scene (0xA) in the AppState machine
bool campaign_screens_enter(FrontEnd& fe, Ui& ui, int state);
bool campaign_screens_step(FrontEnd& fe, Ui& ui);
bool campaign_screens_destroy(FrontEnd& fe, Ui& ui);
bool campaign_screens_next(const FrontEnd& fe, int r, int& next);
