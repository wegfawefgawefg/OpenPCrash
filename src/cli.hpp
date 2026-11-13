#pragma once
// Command line: where data/ is, what to load, and the headless capture path.
#include <string>
#include <vector>

// a scripted mouse drag for headless runs
struct Drag {
    int update = 0;
    float from[2] = {}, to[2] = {};
};
inline constexpr int drag_updates = 20;

struct Options {
    std::string data_dir;   // the extractor's output (docs/assets.md)
    std::string cooked_dir; // the cook's packs, made from data_dir
    int stage = 0;        // stage selector, stage * 2 + night (0..7)
    int build = 0;        // row of rumble_scoobee_loadouts to assemble
    std::string capture;  // --capture out.png: write the last frame there and quit
    int frames = 0;       // --frames N: frames to run before capturing (0: run until closed)
    int width = 1280;
    int height = 960;
    bool camera_given = false; // --camera x,y,z,yaw,pitch (units, degrees): the free camera,
    float camera[5] = {};      // no fighter in the world
    std::string script;        // --script pad.txt: player 1's pad from a file (input/script_input.hpp)
    int trace = 0;             // --trace N: print fighter 1's state every N steps
    // --hash N: the world hashes (world/world_hash.hpp) every N steps, "hash STEP WORLD FIGHT", in
    // the viewer and the front end's fights (the crossplay check, docs/numbers.md)
    int hash = 0;
    // --dummy BUILD: a target in slot 1, a still fighter of that build --dummy-distance units
    // ahead of player 1 (default 20), facing it (--dummy-away: facing away, for rear hits)
    int dummy = -1;
    float dummy_distance = 20;
    bool dummy_away = false;
    // computer fighters (src/ai): --opponents 12,40,o19 puts a build (N) or an opponent entry
    // (oN: its loadout, level, AI profile and pilot team) in slots 1.. as AIs; --ai-level L the
    // roster level of the plain builds (0 by default); --demo the AI-only demo battle (slot 0 an
    // AI too, every AI at level 99, phase 1); --follow N the chase camera on slot N's fighter
    // (seat 0's by default);
    // --ai-reach R every fitted weapon's reach until weapons are in the world (checking the fire
    // decisions); --trace-ai N prints every AI's state every N steps
    std::vector<std::string> opponents;
    int ai_level = 0;
    bool demo = false;
    // --intro: the viewer's fighters come in through the gates with the entrance scenes
    bool intro = false;
    // --seed N: the world generator's index at boot (retail seeds it from the clock); -1 the
    // clock, except in a capture (--frames) and the viewer, which start at 0 so they repeat
    int seed = -1;
    // --battle-script FILE: player 1's pad in the front end's fights from a script (every fight
    // plays it from its first step), for headless runs through a tournament
    std::string battle_script;
    // --route FILE: presses, clicks and fight dev actions keyed to the front end's states
    // (input/dev_route.hpp, docs/playthrough.md)
    std::string route;
    int follow = -1;
    float ai_reach = 0;
    int trace_ai = 0;
    int day = 0;            // --day N: the game day the stage lighting cycles by
    bool safe_rect = false; // --safe-rect: the 3D in the original's 576x448 of 640x480
    // --pose-compare: the build's fixed-point pose against the float walk over its clips, printed
    // (src/mech/pose_compare.cpp), then quit
    bool pose_compare = false;
    // the conversation debug player (src/talk/talk_player.cpp): --talk talk_5b | 0x5b | bark_...
    std::string talk;
    int talk_press = 0;  // --talk-press N: press accept every N frames (captures)
    int talk_chord = -1; // --talk-chord F: hold the skip chord from frame F on
    // --no-dialogue 1 / --dialogue 0: "Story conversations" and "Battle chatter" set to skip or
    // show in player 1's gubsy profile (kept for later runs); -1 leaves them
    int no_dialogue = -1;
    // the stage viewer instead of the game's boot: a battle on the stage with the build as player
    // 1's fighter and the chase camera, or the free camera with --camera (--viewer, or any of
    // --stage, --build, --camera, --script, --trace, --day, --safe-rect)
    bool viewer = false;
    // the front end: --state N enters application state N (hex) instead of the boot's 0x9E;
    // --press U:KEYS,... presses pad buttons at front end update U (a, b, x, y, start, back, up,
    // down, left, right, or a hex word, joined by +); --click U:X:Y left-clicks at 640x480 X, Y
    int start_state = -1;
    // --vs N: a split-screen VS round for N seats at once on --stage (seat 0 the keyboard and
    // mouse, the others PHANTOMCRASH_PADS's pads in turn), the stock builds in turn, then quit
    int vs = 0;
    std::vector<std::pair<int, unsigned>> presses;
    std::vector<std::pair<int, std::pair<float, float>>> clicks;
    // --hover U:X:Y,... moves the pointer to 640x480 X, Y at update U without a click
    std::vector<std::pair<int, std::pair<float, float>>> hovers;
    // --drag U:X:Y:X2:Y2,...: a left press at X, Y at update U, held while the pointer moves to
    // X2, Y2 over drag_updates updates, then let go
    std::vector<Drag> drags;
};

// false (with the usage printed) on a bad command line
bool parse_cli(int argc, char** argv, Options& options);
// the capture path for frame `frame` of a capture run: --capture on the last frame, and
// PHANTOMCRASH_DEV_SHOTS=F,F,... frames F to <capture>_F.png on the way; empty for none
std::string capture_path_for(const Options& options, int frame, bool last);
