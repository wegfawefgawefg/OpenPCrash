#pragma once
// The front end's battle (state 6): what battle_task_begin loads and builds, in its order
// (decomp docs/port/battle.md "The first update"), from today's rumble context of the area picked:
// the stage and every fighter's assets, the battle state, each present roster entry spawned in
// slot order (the human at its gate, the opponents at theirs, with their entrance scenes and AIs),
// then the stage with the tournament's crate weights. Then it steps and draws the fight under the
// front end's 2D, and loads a replacement's mech when the world draws one. Presentation and setup
// only: everything the fight does is step_world's. One session for every fight the front end
// runs: the campaign's, free play's, the test drive's, the result screen's demo battle, VS's split
// screen (battle_vs.cpp) and the replay (battle_replay.cpp).
#include "battle/battle_flow.hpp"
#include "battle_replay.hpp"
#include "effects/effects.hpp"
#include "hud/hud.hpp"
#include "input/pad_rumble.hpp"
#include "render/scenery.hpp"
#include "stage/stage_audio.hpp"
#include "view/combat_sounds.hpp"
#include "view/fighter_sounds.hpp"
#include "view/scene_camera.hpp"
#include "view/split_views.hpp"
#include "world/game_state.hpp"
#include "world/inputs.hpp"

#include <memory>
#include <string>

struct State;
struct FrontEnd;
struct Renderer;
struct PackLibrary;
struct Audio;
struct Ui;

struct BattleRun {
    bool active = false;
    std::unique_ptr<Scenery> scenery;
    StageAudio stage_audio;
    CombatSounds combat_sounds; // the weapons' sounds (src/view/combat_sounds.hpp)
    FighterSounds fighter_sounds; // the fighters' own (src/view/fighter_sounds.hpp)
    int mech_rig[6] = {-1, -1, -1, -1, -1, -1}; // the rig row each slot's mech was loaded as
    std::unique_ptr<Effects> effects; // the battle's effects (src/effects), drawn by the scenery
    HudView hud;                      // the human's HUD (src/hud), slot 0's
    // the scene cameras over the fighters' own (src/view/scene_camera.hpp)
    SceneCameraTables camera_tables;
    SceneCameraState cameras;
    // what the battle's GPU uploads and packs went through, for battle_run_stop to give back
    Renderer* renderer = nullptr;
    PackLibrary* packs = nullptr;
    // PHANTOMCRASH_DEV_BATTLE_LOOP=STEPS:COUNT (headless memory checks): the fight starts again
    // every STEPS steps, COUNT times, through battle_run_stop and battle_run_start
    FrontEnd* fe = nullptr;
    int dev_loops_done = 0;
    // the result screen's demo battle behind its sheet (Campaign::battle_wanted 2): what it
    // borrowed, put back when it stops; the screen's own 3D (its trophy, render pass 13) drawn
    // over it from the Ui's scene
    bool demo = false;
    RumbleSlot demo_rumble;
    int demo_lineup = -1;
    BattleResult demo_result;
    Ui* ui = nullptr;
    // VS (state 6 with vs_mode, battle_vs.cpp): the split screen's views, and the save's rumble
    // context the round borrowed, put back when it stops
    bool vs = false;
    SplitLayout layout;
    // the replay (battle_replay.cpp): the last tournament fight's record, kept across fights, and
    // its playback (Campaign::battle_wanted 3)
    BattleReplay replay;
    RumbleSlot vs_rumble;
    int vs_rumble_slot = 0;
    // the pads' rumble from the hits (input/pad_rumble.hpp): started here, sent by main
    PadRumble rumble;
};

bool battle_run_start(BattleRun& run, State& state, FrontEnd& fe, Ui& ui, Renderer& renderer,
                      PackLibrary& packs, Audio* audio, std::string& error);
// one world step and the presentation after it (the stage, the effects, the HUD); a drawn
// replacement's mech loaded
void battle_run_step(BattleRun& run, State& state, Ui& ui, Renderer& renderer, PackLibrary& packs,
                     Audio* audio, const Inputs& inputs);
// the HUD into ui.draw for this frame (over the front end's 2D)
void battle_run_hud(BattleRun& run, State& state, Ui& ui, const Renderer& renderer, float alpha);
// the frame: the stage and the fighters (parked ones hidden) under the 2D already handed over
bool battle_run_draw(BattleRun& run, State& state, Renderer& renderer, Audio* audio, float alpha,
                     const std::string& capture);
void battle_run_stop(BattleRun& run, State& state, Audio* audio);

// battle_vs.cpp: the VS round's start (from FrontEnd::vs), its views and HUDs, its stop
bool battle_vs_start(BattleRun& run, State& state, FrontEnd& fe, Ui& ui, Renderer& renderer,
                     PackLibrary& packs, Audio* audio, std::string& error);
bool battle_vs_draw(BattleRun& run, State& state, Renderer& renderer, float alpha,
                    const std::string& capture);
void battle_vs_hud(BattleRun& run, State& state, Ui& ui, const Renderer& renderer, float alpha);
void battle_vs_stop(BattleRun& run, State& state, Audio* audio);
// where every VS sound is heard from (vs_sound_pos)
const float* battle_vs_sound_point();
// PHANTOMCRASH_VS_KO's knock-out, before a VS step
void battle_vs_debug_step(World& world);
// battle_replay.cpp: the replay's start (state 5) from the record
bool battle_replay_start(BattleRun& run, State& state, FrontEnd& fe, Renderer& renderer,
                         PackLibrary& packs, Audio* audio, std::string& error);
