#pragma once
// Everything the game is doing right now (AGENTS.md, "Style"). The world is the deterministic part;
// the view is this machine's presentation of it and never writes back.
#include "cli.hpp"
#include "input/seats.hpp"
#include "view/view.hpp"
#include "world/world.hpp"

// the loop's fixed step; steps run at most a frame, more backlog is dropped, never played as slow
// motion (AGENTS.md, "Simulation")
inline constexpr double step_seconds = 1.0 / steps_per_second;
inline constexpr int max_steps_per_frame = 8;

struct State {
    World world;
    // this machine's options the save keeps but the world doesn't (volumes, screen shift,
    // vibration: docs/multiplayer.md "Change now", item 4)
    MachineOptions machine;
    View view;
    Options options;
    Seats seats; // who plays at this machine on what (src/input/seats.hpp)
    bool running = true;
    double accumulator = 0.0; // wall time not yet stepped, seconds
    int frames = 0;           // frames drawn
};
