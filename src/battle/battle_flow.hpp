#pragma once
// The battle around the fighters (decomp docs/port/battle.md; src/hand/battle_frame.c
// battle_update, battle_replacements_update; src/hand/u_088880.c the scenes and
// battle_result_build; src/hand/loadout_assets.c actor_door_check): who is in a scene, the
// entrance through the gates (scenes 1..5), the door (6), the player's death (7), a boss's defeat
// (0x13..0x17), the replacements as fighters leave, how the battle ends and what it hands back.
// World state: everything here is in World::battle_state and steps in step_world.
//
// At 60 Hz: tick timers count 1 a step (they convert themselves); Battle.frame counts steps, so
// `elapsed` (frame x ticks) is the same in ticks; update counts are doubled where the comment says.
#include "battle/part_records.hpp"
#include "battle/scene_states.hpp"
#include "fx/angle.hpp"
#include "fx/vec.hpp"
#include "world/app_states.hpp"
#include "world/control_record.hpp"
#include "world/fighter_slots.hpp"

#include <array>
#include <cstdint>

struct World;
struct Fighter;
struct RosterEntry;

inline constexpr int battle_slots = fighter_slots;

// the lessons' control masks (control_allow): the moves, every fire button, and all but the fire
// buttons and the views (0xFFFFC03F, -0x3FC1)
inline constexpr std::uint32_t control_moves = control_dash | control_jump_held |
                                               control_jump_pressed | control_step_left |
                                               control_step_right; // 0x37
inline constexpr std::uint32_t control_fire_all =
    control_fire_arm_a | control_fire_arm_b | control_fire_back_a | control_fire_back_b;
inline constexpr std::uint32_t lesson_allow = 0xffffc03f;

// TerminalScene (Actor.scene, include/actor.h)
struct FighterScene {
    Fx timer;     // ticks
    FxVec3 focus; // where the camera holds; the door's entry point
    Angle heading;
    Angle door_heading; // progress[1]: the door scene's target heading
    int state = 0;      // SceneState
    int step = 1;       // 0 enter, 1 update, 2 exit
    int killer = 0;     // the death scene's killer slot (-1 none); the gate in 1..6
    Fx door_wait;       // ticks an entering AI waits for a busy gate
    Fx progress[2];     // the lessons' counters (TerminalScene.progress)
    // ticks left of the line the scene started (scene_text_start: the Area Ranker's
    // defeat line), which scene_boss_defeat_update waits for (Battle.scene_text_task alive)
    Fx text_left;
    bool hid_others[battle_slots] = {};
};

// GameState.result (BattleResult, +0x6194): what the battle hands the result screen
struct BattleResult {
    bool won = false;
    bool rented = false;
    bool fixed_opponent_destroyed = false;
    int destruction_count = 0;
    int elapsed = 0; // 60 Hz ticks
    int money_before = 0;
    int prize = 0;
    int weapon_repair[4] = {};
    int body_leg_repair = 0;
    int repair_total = 0;
    int damaged_weapons = 0;
    // replay_snapshot_won: today's tournament already won when the fight began (no trophy then)
    bool tournament_won_at_start = false;
};

// what presentation hears of the battle flow this step (sounds, music, the scene sprites)
enum class BattleEventKind : std::uint8_t {
    Sound, // bank a, cue b (sfx_play)
    Music, // track a, loop b (music_play)
    Theme, // the tournament's theme (tournament_theme_play), tournament a
    // scene_text_start: the line of area a (ranker_defeat_scripts) starts over any bark,
    // or with a = -1 a running bark or line stops (the First Ranker's defeat); the HUD plays it.
    // Barks themselves are asked through Fighter::barks
    SceneText,
};
struct BattleEvent {
    BattleEventKind kind = BattleEventKind::Sound;
    std::int64_t step = 0; // World::step it happened on (CombatEvent's)
    int a = 0, b = 0;
};
inline constexpr int battle_event_max = 16;

struct BattleSlot {
    FighterScene scene;
    bool ai_frozen = false;
    int door_progress = 0;
    bool killed_by_player = false;
    bool parked = false; // the fighter task's flags 0x24: battle_pause_others or retired
    int retire_left = 0; // fighter_retire_wait's countdown, steps
    // the slot's build as a part record row with its tunes (the roster entry's Scoobee), and the
    // row of the battle's builds it is posed and drawn as (its rig, BattleData::rigs)
    BuildRecord build;
    int rig = 0;
};

struct BattleState {
    bool live = false;
    int menu_state = app_battle; // the AppState fighting it: battle, free play or test drive
    int rumble = 0;        // the rumble context (rumble_context_by_selector)
    int end_state = 0;     // 0 fighting, 1 door or ranker kill, 2 the player's death, 3 a demo
    int frame = 0;         // Battle.frame, steps here (ticks)
    Fx replace_clock;      // ticks
    Fx replace_wait;       // ticks; nothing reads it
    int letterbox_state = 0; // 0 off, 1 full, 2 sliding in, 3 sliding out
    Fx letterbox_time;       // ticks 0..30
    bool boss_scene_done = false;
    int view_count = 0;
    int loading_slot = -1; // replacement_loading_slot: drawn, waiting for its mech
    // GameState fields the battle reads
    int player_gate[4] = {};
    int ranker_roll = -1;
    int free_play_lesson = -1; // GameState.free_play_lesson: 0..2 the lessons, 3 plain free play
    // scene text (scene_text_start, Battle.scene_text_task): the script asked for (its
    // pointer's address in the lesson tables, docs/port/progression.md), counted so presentation
    // can say which one ended (Inputs::scene_text_done); alive until it says so
    int scene_text = 0;
    int scene_text_serial = 0;
    bool scene_text_alive = false;
    BattleResult result;
    bool ending_requested = false; // battle_ending_request: the First Ranker's defeat
    // demo_battle_enter_next (phase 1): the slot the camera follows, its clock in ticks,
    // whether a fighter was ever followed (the camera is presentation's: view/scene_camera.cpp)
    int demo_slot = 0;
    int demo_clock = 0;
    bool demo_followed = false;
    BattleSlot slots[battle_slots];
    std::array<BattleEvent, battle_event_max> events{};
    int event_count = 0;
};

// battle_task_begin's part of the battle state: zeroed, the context, end_state, the fixed opponent
// queued (rumble_fixed_opponent_queue)
void battle_state_begin(World& world, int menu_state);
// battle_update's part before the fighters: the letterbox, the replacements, the frame
// and the replacement clock
void battle_update(World& world);
// actor_motion_or_scene: a fighter in a scene runs it instead of the motion states
// (true: fighter_step skips motion_dispatch); demo battles (phase 1) skip scenes
bool battle_scene_step(World& world, Fighter& fighter);
// battle_tutorial.cpp: free play's lessons, scene states 0x18..0x2E (scene_tutorial_18_enter
// .. scene_tutorial_2e_exit). The scene runs before the fighter's step (enter,
// exit, the update's rules on the controls); battle_tutorial_after measures the step's motion
void battle_tutorial_step(World& world, Fighter& fighter);
struct TutorialBefore {
    Angle heading;
    FxVec3 position;
    int state = 0, step = 0;
};
void battle_tutorial_after(World& world, Fighter& fighter, const TutorialBefore& before);
// actor_motion_or_scene's free play part (phase 4): every fighter's empty weapons refilled each
// update (weapons_refill_empty)
void battle_free_play_refill(World& world, Fighter& fighter);
// the scene text presentation says has ended (Inputs::scene_text_done): no longer alive
void battle_scene_text_done(World& world, int serial);
// the end of actor_step: actor_door_check
void battle_door_check(World& world, Fighter& fighter);
// actor_settle_damage's battle part for one killer of the victim: records, the beaten
// byte, the boss scenes; and for a human victim the death scene (scene 7)
void battle_kill(World& world, Fighter& victim, Fighter& killer);
void battle_death(World& world, Fighter& victim, int killer_slot, bool killer_known);
// battle_result_build (phase 0 only): the human's result
void battle_result_build(World& world, const Fighter& fighter);
// battle_other_in_scene
bool battle_other_in_scene(const World& world, int slot);
// tournament_is_ranker_bout and actor_gets_boss_scenes
bool battle_ranker_bout(const World& world);
bool roster_gets_boss_scenes(const World& world, const RosterEntry& entry);
// the slot's build (the roster entry's), what fighter_spawn and the AI read
const BuildRecord& battle_slot_build(const World& world, int slot);
void battle_event(World& world, BattleEventKind kind, int a, int b = 0);

// battle_barks.cpp: the bark requests of actor_settle_damage (durability crossing a quarter or a
// half with `total` pending, before it lands; a kill's for the victim and the killer, before
// battle_kill marks the opponent beaten), of actor_step (the ammo crossing a half) and of
// actor_weapon_break_apply (after the break is marked; `back` the back mounts)
void battle_damage_barks(World& world, Fighter& f, int total);
void battle_kill_barks(World& world, Fighter& victim, Fighter& killer);
void battle_ammo_barks(Fighter& f);
void battle_break_barks(Fighter& f, bool back);

// battle_settle.cpp: the money (battle_result_settlement) and the records
// (battle_result_commit_stats, battle_result_clear)
struct Story;
struct Profile;
int battle_result_settlement(const BattleResult& result, const Story& story);
void battle_result_commit(BattleResult& result, Profile& profile, Story& story, int settlement);
