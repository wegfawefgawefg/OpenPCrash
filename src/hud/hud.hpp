#pragma once
// The battle HUD as presentation (decomp docs/port/hud.md; src/hand/u_04a840.c): the weapon
// readout, the gauges and warnings, the popups and counters, the reticle, tracking and lock boxes
// with their lock marks, the radar, name tags and cloak arrows, the VS marks, the bark window,
// READY/GO, the letterbox, the death card and the VS end menu.
//
// The HUD's gameplay half (the senses, reticle tracking, missile and melee locks, the lock
// warning's timer) runs in the world (battle/hud_senses.cpp, weapons/hud_aim.cpp,
// hud/hud_world.cpp); this half reads the world and its events and never writes back. hud_step
// runs after every world step and steps what retail steps once an update while the HUD shows
// (hud.md "Timers"); hud_draw runs once a frame and only draws, positions interpolated by alpha.
#include "hud/hud_cockpit.hpp"
#include "hud/hud_data.hpp"
#include "screens/ui/screen_text.hpp"
#include "screens/ui/ui.hpp"
#include "talk/talk_interp.hpp"
#include "view/view.hpp"
#include "weapons/combat.hpp"

#include <array>
#include <cstdint>
#include <string>

struct World;

inline constexpr int hud_popup_lines = 8;

// HudPopup: a "DAMAGED", "HIT" or "BINGO!" line (kind 0, 1, 2), its points and its ticks left
struct HudPopup {
    int kind = -1;
    int value = 0;
    float frames = 0;
};

// a lock mark's screen side (LockMark.offset and trail, lock_mark_start, lock_mark_step)
// in screen units of the drawn 3D: started when the world starts the mark, closed on
// even steps by the world's timer as retail closes it once an update
struct HudMarkTrail {
    int target = -1;
    float timer = 0; // the world's, as last seen
    float offset[2] = {};
    float trail[4][2] = {};
    float prev[4][2] = {}; // before the last close, for drawing between
};

// one fighter's HUD presentation (FighterHud's drawing fields and the Actor's rolling counters)
struct HudSlot {
    // actor_durability_bar_start: the bar rolls to a changed durability
    int bar_durability = 0, bar_from = 0, durability_seen = 0;
    float bar_timer = 0, bar_time = 0;
    // fighter_task_update's score roll: min(rise, 30) ticks
    int score_shown = 0, score_from = 0, score_seen = 0;
    float score_anim = 0, score_anim_len = 0;
    // weapon_gauge_refill_start: 120 ticks after a refill
    float resource_shown[4] = {}, resource_from[4] = {}, resource_anim[4] = {};
    // the popups: queued 8, shown 8, scrolled in
    HudPopup queued[hud_popup_lines], shown[hud_popup_lines];
    int popup_count = 0;
    float shown_scroll = 0, shown_scroll_prev = 0;
    // the cockpit instruments (hud_draw_meters): the walk cycle counts steps while drawn
    int walk_frame = 0;
    float jump_gauge = 0, speed_gauge = 0;
    float bob = 0;
    float bob_body_y = 0; // body_point.y: the body's height at the last update
    bool bob_seen = false;
    // the view switch (hud_update): the cockpit set (the instruments, the readout in the world)
    // or the gauge set, and how far it has slid out (0 in place, 1 gone)
    float view_switch_alpha = 0;
    bool cockpit_set = false;
    float fade_in = 0; // the VS mark's scale in, ticks to 30
    // the lock marks this fighter holds
    std::array<HudMarkTrail, lock_mark_count> marks;
    HudMarkTrail melee;
    // the world's screen points of the last two steps (0 before, 1 after) in screen units, x the
    // right way round: the reticle's centre, the tracking point, each other fighter's sense
    float center[2][2] = {}, track[2][2] = {};
    float sense[2][6][2] = {};
    bool shows = false; // the HUD showed on the last step
};

// the scenes' 2D (driven by the battle: the intro, the letterbox, the death scene, VS)
struct HudScenes {
    int intro_timer = 0;     // a human's intro scene timer, ticks (READY 90..32, GO 30..4)
    int letterbox_state = 0; // Battle.letterbox_state: 0 off, 1 in, 2 coming in, 3 going out
    float letterbox_time = 0, letterbox_prev = 0;
    int death_killer = -1; // the death card's killer while its timer runs
    int death_timer = 0;   // ticks of the card left (scene timer over 330)
    bool vs_mode = false;
    bool vs_in_play[6] = {true, true, true, true, true, true};
    int vs_end_state = 0; // Battle.vs_end_state
    float vs_end_timer = 0;
    int vs_end_cursor = 0;
    // the world's VS end menu as last seen (src/battle/vs_battle.hpp), for its sounds, and the
    // quit box (vs_quit_box, "The current play data will be deleted") with its answer
    bool vs_quit_open = false;
    int vs_quit_cursor = 1;
    MsgBox vs_quit_box;
};

// data/tables/barks.sexp (the scenes family): by Bark kind who may say it (bark_speaker_rule: 0
// anyone, 1 humans, 2 fixed opponents) and the chance a request is kept (bark_chance_*), and the
// Area Rankers' defeat lines by area (ranker_defeat_scripts)
struct BarkTables {
    bool loaded = false;
    int speaker_rule[35] = {};
    float chance_human[35] = {}, chance_fixed[35] = {}, chance_ai[35] = {};
    std::uint32_t ranker_scripts[3] = {};
};

// the bark window: one event script at a time (a bark, or the battle's scene text), its
// interpreter run once an update (every other step), its last page kept for the frames between
struct HudBarks {
    bool running = false;
    TalkScript script;
    TalkInterp interp;
    TalkContext context;
    Draw2D page;
    std::array<int, 6> pending{}; // bark_pending by slot, 0x23 none
    std::uint32_t random = 1;     // CRT rand for the chances: presentation only
    BarkTables tables;
    bool skipped = false; // PC: "Battle chatter: Skip" (battle_chatter_skipped), set by the run
    // player 0's pad as the interpreter reads it (seat 0's, set by the run every step): accept
    // gathers new presses of A or START until the interpreter's next update, the chord is held
    TalkPads pads;
};

struct HudView {
    HudData data;
    bool ready = false;
    std::array<HudSlot, 6> slots;
    int viewer = 0;     // the fighter whose HUD this machine draws
    int view_count = 1; // Battle.view_count: 1, 2 or 3-4 split views
    std::uint32_t task_timer = 0; // the blinks' clock (task_timer), 1 a step
    std::int64_t frame = 0;       // Battle.frame in steps: the clock, pulses and alarms
    std::uint32_t random = 1;     // the lock marks' wobble (CRT rand): presentation only
    HudScenes scenes;
    HudBarks barks;
    // the viewer's cockpit instruments this frame (view 0, one view), queued by the effects
    HudCockpit cockpit;
    // PC: each seat's mouse target still to turn, radians right and up (InputDevices::owed_*),
    // set by main every frame: a dim tracking dot shows where the mouse asked to aim
    std::array<std::array<float, 2>, 4> mouse_target{};
};

// battle entry: the sheets every battle loads (game, game_quest, game_vs), the tables, every
// slot's counters from the world as it starts
bool hud_init(HudView& hud, Ui& ui, const World& world, std::string& error);
// after each world step: its events (popups, lock sounds, barks, alarms), the rolls, the lock
// mark trails, the popup scroll and the cockpit gauges while the HUD shows, the scenes' timers
void hud_step(HudView& hud, Ui& ui, const World& world);
// once a frame into ui.draw: the viewer's HUD for the drawn camera (the renderer's window size and
// whether the 3D is in the safe rectangle place the 3D the reticle and marks sit on)
struct HudScreen {
    CameraPose camera;
    int width = 640, height = 480;
    bool safe_rect = false;
    // split screen: the view this HUD is drawn in (its sprites get Sprite.view, render.md), retail's
    // rectangle for it and where the port draws its 3D, 640x480 pixels; view -1 is the one view
    int view = -1;
    float rect[4] = {32, 16, 576, 448};
    float drawn[4] = {0, 0, 640, 480};
    // a wide window centres a view's 2D on its drawn 3D (render/sprite_frame.hpp SpriteFrame::shift):
    // how far across, screen pixels
    float sprite_shift = 0;
    float shake = 1; // PC CONFIG's CAMERA SHAKE, 0..1: the hit shake's move of the screen points
};
void hud_draw(HudView& hud, Ui& ui, const World& world, const HudScreen& screen, float alpha);
// split screen: one player's HUD in its view (hud_update's drawing for slot), then once over every
// view the scenes (READY / GO, the VS end menu, the empty quarter, the quit box) and the barks
void hud_draw_player(HudView& hud, Ui& ui, const World& world, const HudScreen& screen,
                     float alpha, int slot);
void hud_draw_overlay(HudView& hud, Ui& ui, const World& world, float alpha);

// what the battle drives: a human's intro (scene_intro_update's timer), the letterbox state
// (battle_letterbox_draw), a human's death card (scene_player_death_update, killer known)
void hud_intro_start(HudView& hud, int ticks);
void hud_letterbox_set(HudView& hud, int state);
void hud_death_card_start(HudView& hud, int killer);
// hud_barks.cpp: the bark tables from the core pack; how long a scene text script plays, ticks
// (run to its end with no input: script_flags 0x80000001 lets nothing hurry it), 0 for none
struct Pack;
bool bark_tables_load(BarkTables& out, const Pack& core, std::string& error);
int scene_text_ticks(Ui& ui, std::uint32_t address);
// whether the main HUD shows for a slot now (hud_update's third step)
bool hud_shows(const HudView& hud, const World& world, int slot);
