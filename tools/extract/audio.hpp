#pragma once
// The audio family (docs/audio-assets.md): the four SE banks to WAV per cue with each bank's table
// in data/audio/se/<bank>.sexp, the music WAVs and the 202-name music table, the movies copied with
// their ADX sound decoded to WAV, and the I3DL2 reverb presets the stages set.
#include "audio_codec.hpp"

#include <cstdio>
#include <string>

struct Disc;
struct ExtractOptions;
struct Manifest;

// the whole family: SE banks, reverb and the DSP image here, then music and movies
bool extract_audio(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
// audio_streams.cpp: Media/sound/*.WAV and data/audio/music.sexp
bool extract_music(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
// audio_streams.cpp: Media/movie/*.sfd to data/movies/, their ADX to data/audio/adx/
bool extract_movies(const Disc& disc, const ExtractOptions& options, Manifest& manifest);

// a short decimal for lengths and levels (the exact values are the sample counts)
inline std::string sx_decimal(double value, int places) {
    char text[32];
    std::snprintf(text, sizeof text, "%.*f", places, value);
    return text;
}

inline std::string sx_levels(const Levels& levels) {
    if (levels.peak_db <= -200.0) {
        return "(silent)";
    }
    std::string text = "(peak-db " + sx_decimal(levels.peak_db, 2) + ") (rms-db " +
                       sx_decimal(levels.rms_db, 2) + ")";
    if (levels.clipped) {
        text += " (clipped " + std::to_string(levels.clipped) + ")";
    }
    return text;
}
