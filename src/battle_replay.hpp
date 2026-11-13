#pragma once
// The replay (state 5; decomp docs/port/architecture.md "Replays", src/od/u_049bf0.c): retail
// records each human's 20-byte control record every update and re-simulates the battle from a
// snapshot of what it started from. The port's world is a pure function of its inputs, so a
// replay is the fight's starting world (taken once battle_task_begin's spawns and the stage are
// done) and every step's Inputs, stepped again in battle phase 2. The live world is put back when
// it ends, so a replay changes nothing (retail saves and restores the rumble context, the pools
// and the won flags for the same reason). Each live step's fight hash (world/world_hash.hpp) is
// kept, and the replay compares its own against them step by step.
#include "world/inputs.hpp"

#include <cstdint>
#include <memory>
#include <vector>

struct World;

// replay_inputs' 0x4650 updates (10 minutes at 30 Hz): 36,000 steps at 60
inline constexpr int replay_max_steps = 36000;

struct BattleReplay {
    // the record: a live tournament fight's (battle_phase 0, state 6, not VS), overwritten by the
    // next one, kept in memory only (retail saves nothing either)
    bool recording = false;
    std::shared_ptr<World> start;           // the world as the fight's first step found it
    std::vector<Inputs> inputs;             // every step's input
    std::vector<std::uint64_t> live_hashes; // the fight hash after each
    // the playback
    bool playing = false;
    bool done = false; // played to its end (or the fight's end): the screen leaves
    int played = 0;
    std::shared_ptr<World> live; // the world the replay borrowed
    int first_mismatch = -1;     // the first step whose hash differs from the live one's, -1 none
    int view = 5;                // replay_view: the camera view, 0..5 (5 the rails)
};

// the fight starts: its first world kept, the record cleared
void replay_record_start(BattleReplay& replay, const World& world);
// after each live step
void replay_record_step(BattleReplay& replay, const Inputs& inputs, const World& after);
// a record to play (state 5 with no record goes straight back to the result screen)
bool replay_available(const BattleReplay& replay);
// the playback: the live world kept, the start put in its place in phase 2
void replay_play_start(BattleReplay& replay, World& world);
// the input for the next replay step; false once it has run out
bool replay_next_inputs(BattleReplay& replay, Inputs& out);
// after each replay step: the hash checked, the end found (the recorded length, or the door or a
// ranker kill ending the fight: replay_end_request)
void replay_play_step(BattleReplay& replay, const World& after);
// the live world back
void replay_play_stop(BattleReplay& replay, World& world);
