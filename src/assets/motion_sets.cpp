#include "assets/motion_sets.hpp"

const MotionClip* motion_slot(const MotionSet& set, int slot) {
    if (slot < 0 || slot >= static_cast<int>(set.slot_clips.size())) {
        return nullptr;
    }
    const int clip = set.slot_clips[slot];
    return clip >= 0 && clip < static_cast<int>(set.clips.size()) ? &set.clips[clip] : nullptr;
}

float motion_curve_value(const MotionCurve& curve, float frame) {
    if (!curve.present) {
        return 0.0f;
    }
    const std::vector<float>& v = curve.values;
    const int count = static_cast<int>(curve.times.size());
    auto value = [&](int i) { return i >= 0 && i < static_cast<int>(v.size()) ? v[i] : 0.0f; };
    if (curve.mode == 0) {
        return value(0);
    }
    // the first key at or after the frame (the original walks on past the last one: the port stops
    // there)
    int key = 0;
    while (key < count - 1 && static_cast<float>(curve.times[key]) < frame) {
        ++key;
    }
    const float t0 = key == 0 ? 0.0f : static_cast<float>(curve.times[key - 1]);
    const float t1 = static_cast<float>(count ? curve.times[key] : 0);
    if (curve.mode == curve_step) {
        return value(key);
    }
    // stride of a key's run of floats, and where its value, tangents and the next value sit
    const int stride = curve.mode == curve_linear || curve.mode == curve_ease ? 1
                       : curve.mode == curve_both_tangents                    ? 3
                                                                              : 2;
    const int at = key * stride;
    if (t0 == frame || t1 == t0) {
        return value(at);
    }
    const float v0 = value(at);
    const float v1 = value(at + stride);
    const float dt = frame - t0;
    const float u = dt / (t1 - t0);
    const float u1 = u - 1.0f;
    const float shape = u1 + u1 - 1.0f; // 2u - 3: with u^2 (v0 - v1) the cubic ease
    const float eased = shape * (v0 - v1) * u * u + v0;
    switch (curve.mode) {
    case curve_linear:
        return (v1 - v0) * dt / (t1 - t0) + v0;
    case curve_ease:
        return eased;
    case curve_out_tangent: // out tangent only
        return eased + u1 * dt * (u * value(at + 1));
    case curve_in_tangent: // in tangent only
        return eased + u1 * dt * (u1 * value(at + 1));
    case curve_both_tangents: // in and out, three floats a key
        return eased + u1 * dt * (u1 * value(at + 1) + u * value(at + 2));
    default: // curve_in_then_out: in, then the out tangent after the next value
        return eased + u1 * dt * (u1 * value(at + 1) + u * value(at + 3));
    }
}
