#pragma once
// --route FILE (port-only, docs/playthrough.md): a headless run's presses keyed to the front end's
// states instead of update numbers, so a script through the campaign keeps working when a screen
// takes a few updates more or less. One step a line, taken in order:
//
//     STATE WAIT ACTION     # comment
//
// STATE is the application state in hex (or * for any); the step waits until the front end is in
// it, unblanked, for WAIT updates since it entered the state or took the last step. ACTION is
//   a+start, down, 0x110 ...   pad buttons pressed for one update (--press's names)
//   click:X:Y                  a left click at 640x480 X, Y
//   kill, killall, die, door   a fight's dev action (battle/battle_dev.hpp) on the next step
//   win, win:N                 in a fight, every WAIT updates: kill (the fixed opponent first)
//                              until the tournament's fixed opponent is down (or N kills, 30
//                              by default), then door; seat 0's fighter takes no hits meanwhile.
//                              Stays on this step until the state changes
//   every:A,B,...              every WAIT updates the actions A, B (pad words, kill) until the
//                              state changes: a lesson's lines accepted, its drones shot down
//   shot:NAME                 the next drawn frame captured to NAME (a .png path; a bare name
//                              beside --capture's picture)
//   pose                       seat 0's fighter's place, heading, pitch and twist printed
//   quit                       the run ends after the next frame (captured with --capture)
//   -                          nothing (waits for the state)
//
// A line "include FILE" puts another route's steps there (FILE beside this one unless absolute).
#include <string>
#include <vector>

struct World;

struct DevRouteStep {
    int state = -1; // -1 any
    int wait = 0;
    std::string action;
    std::string where; // file:line, for the log
};

struct DevRoute {
    std::vector<DevRouteStep> steps;
    std::size_t next = 0;
    int state_seen = -1;
    int since = 0;
    bool win_active = false; // a win or every step has started in its state
    // this update's asks
    unsigned press = 0;
    bool click = false;
    float click_x = 0, click_y = 0;
    unsigned fight = 0; // battle_dev bits, or-ed into the next world step's Inputs::dev
    unsigned hold = 0;  // and these into every step's (a win step's shield)
    bool start = false; // a press with START: a fight's pause reads it once (main)
    std::string shot;
    std::string shot_dir; // where a bare shot name goes (--capture's folder)
    bool quit = false;
};

bool load_dev_route(DevRoute& route, const std::string& path, std::string& error);
// once a front end update, before it runs: what the route presses this update
void dev_route_update(DevRoute& route, int state, bool blank, const World& world);
