#pragma once
// --pose FILE (dev only, for the site's posed screenshots): a scene for the stage viewer set up
// from a short text file. The stage, the machines with their builds, places and headings, each one
// either an AI or driven by its own pad lines (so a dash, a jump or a volley lands where and when
// the file says), and a camera placed in the world or in a fighter's frame. The fight then runs
// in the world as any other does, so the shots, hits, smoke and blasts are the game's own; the
// capture is the world at `steps`. With an `eye` no HUD; without one the game's own camera and HUD,
// seat 0's (its first pad fighter must be slot 0).
//
// One line each, `#` a comment:
//   stage S                       stage selector (stage * 2 + night)
//   day D | seed N                the lighting day, the world generator's index
//   fighter BUILD X Z HEADING [pad|ai|still [Y]]
//                                 the next slot: a build row or oN (an opponent entry; not slot
//                                 0), at X, Z on the floor, heading in degrees (0 along +z); pad
//                                 by default (seats 0..3 in turn), still: run by nobody; Y the
//                                 height where the floor under X, Z has several levels
//   durability SLOT N             that fighter's durability (a low one goes down at its next hit)
//   pad SLOT steps lx ly rx ry buttons
//                                 a line of that fighter's pad, as --script's (input/script_input)
//   eye SLOT R U F | look SLOT R U F
//                                 the camera and the point it looks at, R U F right, up and
//                                 forward in the fighter's frame (its heading, from its feet);
//                                 SLOT -1 for the world's X Y Z
//   fov DEG | roll DEG            the lens (52 the game's) and a tilt
//   steps N                       the world steps before the capture
#include "input/script_input.hpp"
#include "world/pad_record.hpp"

#include <string>
#include <vector>

struct Options;
struct World;
struct CameraPose;
struct FighterPose;
struct Inputs;

struct PoseFighter {
    std::string build = "0";
    float x = 0, z = 0, heading = 0;
    bool height_given = false;
    float y = 0;
    int seat = -1; // the pad seat, -1 an AI, -2 nobody (still)
    int durability = -1;
};

// a camera point: in a fighter's frame (right, up, forward) or the world's (slot -1)
struct PosePoint {
    int slot = -1;
    float at[3] = {};
};

struct Pose {
    int stage = 0, day = 0, seed = 0, steps = 60;
    std::vector<PoseFighter> fighters;
    InputScript pads[max_pad_players];
    PosePoint eye, look;
    bool camera = false; // an eye given: the pose's camera, no HUD
    float fov = 52, roll = 0;
};

bool load_pose(Pose& pose, const std::string& path, std::string& error);
// the viewer's options from the pose: stage, day, seed, slot 0's build, the others as
// --opponents, each slot's seat, the frames to run
void pose_options(const Pose& pose, Options& options);
// after the spawn: each fighter at its place on the floor, its durability
void pose_place(World& world, const Pose& pose);
// each pad fighter's pad for the next step
void pose_pads(Pose& pose, Inputs& inputs);
// the camera the pose asks for, from the fighters as drawn
void pose_camera(const Pose& pose, const FighterPose* fighters, CameraPose& camera);
