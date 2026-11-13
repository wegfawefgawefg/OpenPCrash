#pragma once
// A dev-only dance for captures (--dance FILE): slot 0's build alone in the free camera, its own
// skeleton driven by a table of per-tick offsets from its idle pose (tools/dance.py writes one).
// The offsets go into clips built over the build's idle clips, so the SV graph walk and its IK
// chains pose it as they pose any clip (mech/mech_pose.cpp); the root channels turn and move the
// whole machine about its feet. Presentation only, never in the world.
//
// The file: "ticks N", then "channels NAME...", then N + 1 rows of values (ticks 0..N, the last
// equal to the first for a loop). A name is root.C or PART.TRACK.C: PART legs, arm_a or arm_b,
// TRACK the clip's track, C one of rx ry rz (radians) tx ty tz (units). Root turns apply y, then
// x, then z, about the fighter's origin, then its offset.
#include "assets/motion_sets.hpp"
#include "mech/mech_pose.hpp"

#include <string>
#include <vector>

struct MechParts;

struct DanceChannel {
    int part = 0;    // 0 legs, 1 arm A, 2 arm B, 3 root
    int track = 0;   // the clip's track (root: unused)
    int channel = 0; // 0..2 rotate x y z, 3..5 translate x y z
};

struct Dance {
    int ticks = 0;
    std::vector<DanceChannel> channels;
    std::vector<float> values;     // (ticks + 1) rows of channels.size()
    MotionClip legs, arm_a, arm_b; // the build's idle clips with the offsets in
    bool white = false;            // --dance-white: the clear colour white (difference matting)
};

bool load_dance(Dance& dance, const std::string& path, std::string& error);
// the clips for this build (after load_mech)
void dance_build_clips(Dance& dance, const MechParts& mech);
// the pose input at ticks (60 Hz, looped) with the fighter standing at `base`
MechPoseInput dance_pose_input(const Dance& dance, const MechParts& mech, float ticks,
                               const Mat4& base);
