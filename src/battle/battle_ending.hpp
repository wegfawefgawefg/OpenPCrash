#pragma once
// The in-engine ending's shots (scene_ending_shots, data/tables/scene_ending.sexp from
// the scenes family): scene states 8..0x12, each a camera curve (scene_camera_rows, presentation's)
// played after a lead-in, with its fades, the supply craft's route, its line of scene text and the
// frame capture between shots. The world steps them (battle_ending.cpp); presentation draws the
// fades, the last shot's signal and the shots' cameras from the player's scene.
#include "battle/scene_states.hpp"
#include "fx/fx.hpp"

#include <string>

struct Pack;
struct World;
struct Fighter;

inline constexpr int ending_shot_count = scene_ending_last - scene_ending_first + 1;
// the shots' scene text pointers: scene_ending_shots + row x 0x34 + 0x2C (the address the
// scripts family's used-by names)
inline constexpr int ending_script_field = 0x242E90 + 0x2C;

// the paper storm's anchors a shot sets as it enters (scene_ending_enter: GameState +0x10EF8..
// +0x10F0D): three rows of the point table at 0x98CA14 (-1 none) with a byte each, and one more
// byte (+0x10F0D). Presentation's (the effects' paper emitters 0x41..0x4E)
struct EndingPaper {
    int point[3] = {-1, -1, -1};
    int flag[3] = {};
    int last = 0;
};

struct EndingShot {
    Fx length;      // updates of lead-in (scene.door_wait counts up to it)
    Fx fade_in;     // the fade in started as the shot begins, ticks (0 none)
    Fx fade_out_at; // the scene timer the fade out starts at, curve frames (0 none)
    Fx capture;     // the frame capture on the shot's exit, frames (0 none)
    int route = -1; // the supply craft's ending route, -1 the craft off
    bool script = false;
    Fx script_at;   // the curve frame its scene text starts at
    EndingPaper paper;
};

struct EndingShots {
    bool loaded = false;
    EndingShot shots[ending_shot_count];
    int beeps[16] = {}; // the last shot's beep schedule (D_001B5B14), curve frames, -1 ended
    int beep_count = 0;
};

bool load_ending_shots(EndingShots& out, const Pack& core, std::string& error);
// the paper anchors the player's shot has set, null outside the ending (battle phase 3)
const EndingPaper* ending_paper(const World& world);

// scene_table rows 8..0x12 (scene_ending_enter, scene_ending_update,
// scene_ending_exit), one step each
void ending_enter(World& world, Fighter& f);
void ending_update(World& world, Fighter& f);
void ending_exit(World& world, Fighter& f);
