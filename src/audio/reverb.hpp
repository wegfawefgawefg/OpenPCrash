#pragma once
// A stand-in for the Xbox APU's I3DL2 reverb (dsstdfx.bin's effect 0), driven by the same listener
// parameters (docs/audio.md, "Reverb"): the room level and its high-frequency cut on the way in,
// early reflections after reflections_delay, a late tail after reverb_delay more that dies by
// decay_time (and decay_time * decay_hf_ratio at hf_reference), diffusion and density shaping the
// tail. Mono in (the 3D voices' send), stereo out. Runs on the audio thread; no allocation once
// made.
#include "audio/audio.hpp"

#include <array>
#include <vector>

inline constexpr int reverb_lines = 8;     // the tail's feedback delay network
inline constexpr int reverb_diffusers = 4; // allpasses ahead of it

struct ReverbLine {
    std::vector<float> buffer;
    int length = 1; // samples, by density
    int at = 0;
    float gain = 0.0f; // per pass, for decay_time
    float damp = 0.0f; // one-pole low-pass coefficient, for decay_hf_ratio
    float low = 0.0f;  // its state
};

struct ReverbAllpass {
    std::vector<float> buffer;
    int length = 1;
    int at = 0;
};

struct Reverb {
    ReverbPreset preset;
    bool on = false;
    // the way in: room level and its HF cut (a one-pole low-pass)
    float room_gain = 0.0f;
    float in_coefficient = 1.0f;
    float in_low = 0.0f;
    // the pre-delay line both reflections and tail read from
    std::vector<float> delay;
    int delay_at = 0;
    int reflections_taps[2][4] = {}; // samples back, left and right
    float reflections_gain = 0.0f;
    int tail_tap = 0; // samples back
    float tail_gain = 0.0f;
    float diffusion = 0.5f; // the allpasses' coefficient
    std::array<ReverbAllpass, reverb_diffusers> allpasses;
    std::array<ReverbLine, reverb_lines> lines;
};

// buffers sized for the longest delays I3DL2 allows
void make_reverb(Reverb& reverb);
// a new preset: gains, taps and line lengths worked out (keeps the tail's state)
void set_reverb_preset(Reverb& reverb, const ReverbPreset& preset);
// adds frames of the reverb of `in` into `out` (stereo interleaved)
void run_reverb(Reverb& reverb, const float* in, float* out, int frames);
