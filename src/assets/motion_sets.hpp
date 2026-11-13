#pragma once
// Motion curve sets in the port frame (the extractor's motion family, cooked into packs), and the
// curve evaluation of motion_curve_eval. Times are 60 Hz ticks. The cook keeps the keys
// and modes exactly: a curve is evaluated at any time the original asks, so resampling it would
// change the poses.
#include <cstdint>
#include <string>
#include <vector>

struct Pack;

// MotionCurve.mode (motion_curve_value): how keys are read, 0 a constant
inline constexpr std::uint32_t curve_step = 0x2000;
inline constexpr std::uint32_t curve_linear = 0x4000;
inline constexpr std::uint32_t curve_ease = 0x6000;
inline constexpr std::uint32_t curve_out_tangent = 0x8000;   // Hermite, out tangent only
inline constexpr std::uint32_t curve_in_tangent = 0xA000;    // in tangent only
inline constexpr std::uint32_t curve_both_tangents = 0xC000; // in and out, three values a key
inline constexpr std::uint32_t curve_in_then_out = 0xE000;   // in, then out after the next value

// MotionTrack.flags (NodeAnim): the channels a track has
inline constexpr std::uint32_t track_rotate = 0x700;   // rotate x y z (0x400, 0x200, 0x100)
inline constexpr std::uint32_t track_translate = 0xE0; // translate x y z (0x80, 0x40, 0x20)
inline constexpr std::uint32_t track_channels = 0x7E0; // both
inline constexpr std::uint32_t track_steps = 0x3800;   // the footstep streams

struct MotionCurve {
    std::uint32_t mode = 0; // curve_* or 0
    std::vector<std::uint16_t> times;
    std::vector<float> values;
    bool present = false; // a null track's curves read as 0
};

struct MotionTrack {
    std::uint32_t flags = 0; // track_* bits
    bool null = false;
    MotionCurve rotate[3];
    MotionCurve translate[3];
    MotionCurve steps[3];
};

using MotionClip = std::vector<MotionTrack>;

struct MotionSet {
    int asset = -1;
    std::vector<int> slot_clips; // slot -> clip, -1 for none
    std::vector<MotionClip> clips;
};

// the motion set `asset` from a pack
bool load_motion_set(const Pack& pack, int asset, MotionSet& out, std::string& error);
// the clip a slot plays, or null
const MotionClip* motion_slot(const MotionSet& set, int slot);
// one curve at a time in ticks, as motion_curve_eval reads it (from its first key: the port keeps
// no forward-only cursor, which gives the same value while time only moves forward)
float motion_curve_value(const MotionCurve& curve, float frame);

// pack layout (src/cook/pack_format.hpp)
template <class A> void io(A& a, MotionCurve& c) {
    a(c.mode);
    a(c.present);
    a(c.times);
    a(c.values);
}
template <class A> void io(A& a, MotionTrack& t) {
    a(t.flags);
    a(t.null);
    for (int k = 0; k < 3; ++k) {
        io(a, t.rotate[k]);
        io(a, t.translate[k]);
        io(a, t.steps[k]);
    }
}
template <class A> void io(A& a, MotionSet& s) {
    a(s.asset);
    a(s.slot_clips);
    a(s.clips);
}
