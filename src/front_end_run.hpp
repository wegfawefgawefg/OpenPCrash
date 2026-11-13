#pragma once
// The front end's loop (src/front_end_run.cpp) and its fixed steps (src/front_end_steps.cpp): pump
// input, run 60 Hz steps with a catch-up cap, draw. A fight draws every frame with an interpolation
// alpha; a menu draws every frame at the display's refresh (docs/settings.md, FRAME LIMIT). main
// runs it after init, or the stage viewer for --viewer.
#include "battle_run.hpp"
#include "input/dev_route.hpp"
#include "input/script_input.hpp"
#include "screens/flow/front_end.hpp"
#include "state.hpp"
#include "talk/draw2d.hpp"

#include <SDL3/SDL.h>
#include <cstdint>
#include <string>

struct Renderer;
struct PackLibrary;
struct Audio;
class GubsyRuntime;
struct InputDevices;
struct PcHost;

// the loop's locals: main's systems, the front end and its fight, the headless runs' scripts
struct FrontEndRun {
    State& state;
    SDL_Window* window;
    Renderer& renderer;
    PackLibrary& packs;
    Audio* audio;
    GubsyRuntime& gubsy;
    InputDevices& devices;
    PcHost& pc;
    Ui ui{};
    FrontEnd fe{};
    BattleRun battle{};          // the fight a battle state asks for (src/battle_run.hpp)
    Draw2D front_draw{};         // the front end's 2D as its last update left it
    InputScript battle_script{}; // --battle-script: seat 0's pad in a fight
    DevRoute route{};            // --route FILE: a headless run's presses by state
    std::string error{};
    bool capturing = false; // --frames: one step a frame (PHANTOMCRASH_DEV_FAST's N)
    int fast = 1;
    int between = 1;           // PHANTOMCRASH_DEV_BETWEEN's frames a step in a capture
    int between_phase = 0;     // the capture's frame within its step
    bool updated = false;      // the front end updated in this pass's steps
    int fe_phase = 0;          // a 30 Hz state's steps: it updates on the even ones
    std::uint16_t carried = 0; // pad presses of a skipped step
    double previous_time = 0.0;
    int pads_frame = -1; // the frame PHANTOMCRASH_MOUSE_MOVE and the rest last read
    // the pause's settings panel (screens/pc/pause_settings.hpp): Escape read as Back (kept till
    // it is let go after the panel closed)
    bool panel_keys = false;
    bool mouse_captured = false; // a played, unpaused fight holds the mouse (relative mode)
};

int run_front_end(State& state, SDL_Window* window, Renderer& renderer, PackLibrary& packs,
                  Audio* audio, GubsyRuntime& gubsy, InputDevices& devices, PcHost& pc);

// the steps the accumulator holds (at most max_steps_per_frame): a fight started or stopped, the
// pads read, the world stepped, the front end updated at its state's cadence; how many ran
int front_end_steps(FrontEndRun& run);
