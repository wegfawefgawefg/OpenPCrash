#include "audio/reverb.hpp"

#include "audio/audio_state.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float pi = 3.14159265f;
// the tail's lines at full density, mutually prime, 30 to 68 ms
constexpr int line_lengths[reverb_lines] = {1427, 1777, 1973, 2099, 2269, 2557, 2879, 3251};
// the diffusers, Schroeder's
constexpr int allpass_lengths[reverb_diffusers] = {142, 107, 379, 277};
// the reflections' taps after reflections_delay, seconds, left and right
constexpr float reflection_offsets[2][4] = {{0.0f, 0.0043f, 0.0071f, 0.0113f},
                                            {0.0019f, 0.0057f, 0.0089f, 0.0131f}};
// I3DL2's longest: reflections 0.3 s, reverb 0.1 s after them, plus the taps' spread
constexpr int delay_samples = static_cast<int>(0.42f * audio_rate) + 1024;

// the coefficient a of a one-pole low-pass y += a (x - y) whose gain at `hz` is `gain` (0..1)
float one_pole_for(float gain, float hz) {
    gain = std::clamp(gain, 0.0001f, 1.0f);
    if (gain >= 0.9999f) {
        return 1.0f;
    }
    const float c = std::cos(2.0f * pi * std::min(hz, audio_rate * 0.49f) / audio_rate);
    const float g2 = gain * gain;
    // |H|^2 = a^2 / (1 - 2 (1 - a) c + (1 - a)^2) = g2, for b = 1 - a
    const float p = 1.0f - g2 * c;
    const float q = 1.0f - g2;
    const float b = (p - std::sqrt(std::max(0.0f, p * p - q * q))) / q;
    return 1.0f - b;
}

float mb_gain(int mb) {
    return mb <= -10000 ? 0.0f : std::pow(10.0f, static_cast<float>(mb) / 2000.0f);
}

} // namespace

void make_reverb(Reverb& reverb) {
    reverb.delay.assign(static_cast<std::size_t>(delay_samples), 0.0f);
    for (int i = 0; i < reverb_diffusers; ++i) {
        reverb.allpasses[i].buffer.assign(static_cast<std::size_t>(allpass_lengths[i]), 0.0f);
        reverb.allpasses[i].length = allpass_lengths[i];
    }
    for (int i = 0; i < reverb_lines; ++i) {
        reverb.lines[i].buffer.assign(static_cast<std::size_t>(line_lengths[i]), 0.0f);
    }
    set_reverb_preset(reverb, reverb_off);
}

void set_reverb_preset(Reverb& reverb, const ReverbPreset& p) {
    const bool was_on = reverb.on;
    reverb.preset = p;
    reverb.room_gain = mb_gain(p.room);
    reverb.on = reverb.room_gain > 0.0f && (p.reflections > -10000 || p.reverb > -10000);
    // the room's HF level is the input's gain at hf_reference
    reverb.in_coefficient = one_pole_for(mb_gain(std::min(0, p.room_hf)), p.hf_reference);

    // reflections: four taps a side after the pre-delay, sharing the level between them
    const float rate = static_cast<float>(audio_rate);
    const float reflections_delay = std::clamp(p.reflections_delay, 0.0f, 0.3f);
    const float reverb_delay = std::clamp(p.reverb_delay, 0.0f, 0.1f);
    for (int side = 0; side < 2; ++side) {
        for (int t = 0; t < 4; ++t) {
            const float at = (reflections_delay + reflection_offsets[side][t]) * rate;
            reverb.reflections_taps[side][t] = std::max(1, static_cast<int>(at));
        }
    }
    reverb.reflections_gain = mb_gain(p.reflections) * 0.5f;
    reverb.tail_tap = std::max(1, static_cast<int>((reflections_delay + reverb_delay) * rate));

    // the tail: line lengths by density, each line's loss for decay_time and its low-pass for the
    // HF decay (decay_time * decay_hf_ratio at hf_reference)
    const float density = 0.4f + 0.6f * std::clamp(p.density, 0.0f, 100.0f) / 100.0f;
    const float decay = std::clamp(p.decay_time, 0.1f, 20.0f);
    const float decay_hf = decay * std::clamp(p.decay_hf_ratio, 0.1f, 2.0f);
    float mean_gain = 0.0f;
    for (int i = 0; i < reverb_lines; ++i) {
        ReverbLine& line = reverb.lines[i];
        line.length = std::max(64, static_cast<int>(line_lengths[i] * density));
        line.at %= line.length;
        const float seconds = static_cast<float>(line.length) / rate;
        line.gain = std::pow(10.0f, -3.0f * seconds / decay);
        const float hf_gain = std::pow(10.0f, -3.0f * seconds / decay_hf);
        line.damp = one_pole_for(std::min(1.0f, hf_gain / line.gain), p.hf_reference);
        mean_gain += line.gain / reverb_lines;
    }
    // the tail's level: the reverb level, with the network's own build-up taken out (each line
    // holds 1/8 of the input's energy times 1 / (1 - g^2); a side sums four lines at half level)
    reverb.tail_gain = mb_gain(p.reverb) *
                       std::sqrt(static_cast<float>(reverb_lines) * (1.0f - mean_gain * mean_gain));
    reverb.diffusion = 0.7f * std::clamp(p.diffusion, 0.0f, 100.0f) / 100.0f;

    // switched on from off: no tail from before
    if (reverb.on && !was_on) {
        std::fill(reverb.delay.begin(), reverb.delay.end(), 0.0f);
        for (ReverbAllpass& a : reverb.allpasses) {
            std::fill(a.buffer.begin(), a.buffer.end(), 0.0f);
        }
        for (ReverbLine& line : reverb.lines) {
            std::fill(line.buffer.begin(), line.buffer.end(), 0.0f);
            line.low = 0.0f;
        }
        reverb.in_low = 0.0f;
    }
}

void run_reverb(Reverb& reverb, const float* in, float* out, int frames) {
    const int size = static_cast<int>(reverb.delay.size());
    const float inject = 1.0f / std::sqrt(static_cast<float>(reverb_lines));
    for (int i = 0; i < frames; ++i) {
        // in: the room level and its HF cut
        const float x = in[i] * reverb.room_gain;
        reverb.in_low += reverb.in_coefficient * (x - reverb.in_low);
        reverb.delay[static_cast<std::size_t>(reverb.delay_at)] = reverb.in_low;
        auto back = [&](int samples) {
            int at = reverb.delay_at - samples;
            at += at < 0 ? size : 0;
            return reverb.delay[static_cast<std::size_t>(at)];
        };

        // early reflections
        float early[2] = {0.0f, 0.0f};
        for (int side = 0; side < 2; ++side) {
            for (int t = 0; t < 4; ++t) {
                const float tap = back(reverb.reflections_taps[side][t]);
                early[side] += (t & 1) ? -tap : tap;
            }
            early[side] *= reverb.reflections_gain;
        }

        // the tail's input through the diffusers
        float t = back(reverb.tail_tap);
        for (ReverbAllpass& a : reverb.allpasses) {
            float& slot = a.buffer[static_cast<std::size_t>(a.at)];
            const float y = -reverb.diffusion * t + slot;
            slot = t + reverb.diffusion * y;
            t = y;
            a.at = a.at + 1 == a.length ? 0 : a.at + 1;
        }

        // the network: each line's output damped and lost, mixed by an 8-point Hadamard, fed back
        float o[reverb_lines];
        for (int k = 0; k < reverb_lines; ++k) {
            ReverbLine& line = reverb.lines[k];
            const float v = line.buffer[static_cast<std::size_t>(line.at)];
            line.low += line.damp * (v - line.low);
            o[k] = line.low * line.gain;
        }
        float h[reverb_lines];
        for (int k = 0; k < reverb_lines; ++k) {
            h[k] = o[k];
        }
        for (int span = 1; span < reverb_lines; span *= 2) {
            for (int k = 0; k < reverb_lines; k += span * 2) {
                for (int j = k; j < k + span; ++j) {
                    const float a = h[j], b = h[j + span];
                    h[j] = a + b;
                    h[j + span] = a - b;
                }
            }
        }
        for (int k = 0; k < reverb_lines; ++k) {
            ReverbLine& line = reverb.lines[k];
            const float sign = (k * 5 + 1) % 3 == 0 ? -1.0f : 1.0f;
            line.buffer[static_cast<std::size_t>(line.at)] = h[k] * inject + sign * inject * t;
            line.at = line.at + 1 >= line.length ? 0 : line.at + 1;
        }
        const float late_left = (o[0] + o[2] + o[4] + o[6]) * 0.5f * reverb.tail_gain;
        const float late_right = (o[1] + o[3] + o[5] + o[7]) * 0.5f * reverb.tail_gain;

        out[i * 2] += early[0] + late_left;
        out[i * 2 + 1] += early[1] + late_right;
        reverb.delay_at = reverb.delay_at + 1 == size ? 0 : reverb.delay_at + 1;
    }
}
