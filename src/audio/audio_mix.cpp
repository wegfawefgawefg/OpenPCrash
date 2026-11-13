// The mix (docs/audio.md, "Mix"): the main thread's commands run first (audio_commands.cpp), then
// every playing voice of every bank, the ghosts of cut voices, the reverb of the 3D voices and the
// streams, into 48 kHz stereo float. Runs on the device's thread (or an offline render's) under
// mix_lock, which the main thread takes only for a full ring (audio_state.hpp); allocates and
// frees nothing.
#include "audio/audio_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// DirectSound 3D as GenkiLib sets it up (func_000DE550, se_voice_clear_spatial): game units halved
// (func_000E1630), minimum distance 1, rolloff factor 0.1, Doppler factor 0
constexpr float ds_units_per_game_unit = 0.5f;
constexpr float min_distance = 1.0f;
constexpr float rolloff = 0.1f;
// a 2D voice's six mix bins at -500 mB (se_bank_load)
constexpr int mix_bin_2d_millibels = -500;
// the ghosts' fade, 4 ms
constexpr float ghost_frames = 192.0f;

// the APU's 3D distance attenuation (I3DL2's, read in the XBE's DirectSound at 0x152CAD:
// -2000 log10(1 + rolloff (d / min - 1)) mB past the minimum distance)
float distance_gain(float game_distance) {
    const float d = game_distance * ds_units_per_game_unit;
    return d <= min_distance ? 1.0f : 1.0f / (1.0f + rolloff * (d / min_distance - 1.0f));
}

// the level a bank's voices get: se_volume_set's level times the pad's scale through the millibel
// table ((unsigned char)(int)(level * scale), func_000DDC50)
float bank_gain(const Audio& audio, const Bank& bank) {
    const int k = static_cast<int>(static_cast<float>(audio.mix.sound_level) * bank.volume) & 0xFF;
    return level_gain(audio, k);
}

// one interleaved source frame at a fractional position, as mono or as left and right
inline void sample_at(const Sample& s, double position, bool looping, float& left, float& right) {
    const int i = static_cast<int>(position);
    const float t = static_cast<float>(position - i);
    int j = i + 1;
    if (j >= s.frames) {
        j = looping ? 0 : i;
    }
    constexpr float scale = 1.0f / 32768.0f;
    if (s.channels == 1) {
        const float a = s.pcm[static_cast<std::size_t>(i)];
        const float b = s.pcm[static_cast<std::size_t>(j)];
        left = right = (a + (b - a) * t) * scale;
        return;
    }
    const std::int16_t* p = s.pcm.data();
    const float al = p[i * 2], ar = p[i * 2 + 1], bl = p[j * 2], br = p[j * 2 + 1];
    left = (al + (bl - al) * t) * scale;
    right = (ar + (br - ar) * t) * scale;
}

// a voice's gains for this block: left, right, the reverb send, and its rear shade
void place_voice(const Audio& audio, const Cue& cue, Voice& voice, float base, float& left,
                 float& right, float& send) {
    if (!cue.is3d) {
        left = right = base * millibels_gain(mix_bin_2d_millibels);
        send = 0.0f;
        voice.shade = 1.0f;
        return;
    }
    const Listener& l = audio.mix.listener;
    const float d[3] = {voice.place[0] - l.position[0], voice.place[1] - l.position[1],
                        voice.place[2] - l.position[2]};
    const float distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float side = 0.0f, front = 1.0f;
    if (distance > 1e-4f) {
        side = (d[0] * l.right[0] + d[1] * l.right[1] + d[2] * l.right[2]) / distance;
        front = (d[0] * l.forward[0] + d[1] * l.forward[1] + d[2] * l.forward[2]) / distance;
    }
    // the HRTF's stand-in: both ears at full level ahead, the near one +3 dB and the far one
    // -10 dB at the side, and darker behind
    const float near = base * distance_gain(distance);
    left = near * std::sqrt(1.0f - 0.9f * side);
    right = near * std::sqrt(1.0f + 0.9f * side);
    const float cutoff = front >= 0.0f ? 20000.0f : 20000.0f + (5000.0f - 20000.0f) * -front;
    voice.shade = 1.0f - std::exp(-2.0f * 3.14159265f * cutoff / audio_rate);
    // I3DL2's room send falls off by the room rolloff factor, 0 in every preset the game sets
    send = base;
}

void mix_voice(Audio& audio, Cue& cue, int index, float base, float* out, int frames) {
    Voice& voice = cue.voices[static_cast<std::size_t>(index)];
    float left = 0.0f, right = 0.0f, send = 0.0f;
    place_voice(audio, cue, voice, base, left, right, send);
    if (!voice.gains_known) {
        voice.gain_left = left;
        voice.gain_right = right;
        voice.send = send;
        voice.gains_known = true;
    }
    const Sample& s = cue.sample;
    const double step = s.rate_ratio * std::exp2(voice.pitch / 4096.0);
    const float ramp = 1.0f / static_cast<float>(frames);
    const float dl = (left - voice.gain_left) * ramp;
    const float dr = (right - voice.gain_right) * ramp;
    const float ds = (send - voice.send) * ramp;
    float gl = voice.gain_left, gr = voice.gain_right, gs = voice.send;
    float* reverb_send = audio.mix.send;
    for (int i = 0; i < frames && voice.playing; ++i) {
        gl += dl;
        gr += dr;
        gs += ds;
        float a = 0.0f, b = 0.0f;
        sample_at(s, voice.position, voice.looping, a, b);
        if (cue.is3d) {
            // a 3D voice is mono: the shade's one-pole low-pass, then placed
            const float mono = s.channels == 1 ? a : 0.5f * (a + b);
            voice.shade_low += voice.shade * (mono - voice.shade_low);
            a = b = voice.shade_low;
            reverb_send[i] += gs * mono;
        }
        out[i * 2] += gl * a;
        out[i * 2 + 1] += gr * b;
        voice.position += step;
        if (voice.position >= s.frames) {
            if (voice.looping) {
                voice.position = std::fmod(voice.position, static_cast<double>(s.frames));
            } else {
                // played out: the main thread hears this start has ended
                voice.playing = false;
                cue.ended[static_cast<std::size_t>(index)].store(voice.serial,
                                                                 std::memory_order_release);
            }
        }
    }
    voice.gain_left = left;
    voice.gain_right = right;
    voice.send = send;
}

void mix_ghost(Audio& audio, Ghost& ghost, float* out, int frames) {
    const Sample& s = *ghost.sample;
    for (int i = 0; i < frames && ghost.fade > 0.0f; ++i) {
        float a = 0.0f, b = 0.0f;
        sample_at(s, ghost.position, ghost.looping, a, b);
        out[i * 2] += ghost.fade * ghost.gain_left * a;
        out[i * 2 + 1] += ghost.fade * ghost.gain_right * b;
        audio.mix.send[i] += ghost.fade * ghost.send * 0.5f * (a + b);
        ghost.fade -= 1.0f / ghost_frames;
        ghost.position += ghost.step;
        if (ghost.position >= s.frames) {
            if (!ghost.looping) {
                ghost.fade = 0.0f;
            } else {
                ghost.position = std::fmod(ghost.position, static_cast<double>(s.frames));
            }
        }
    }
    if (ghost.fade <= 0.0f) {
        ghost.sample = nullptr;
    }
}

// a stream's frames for this block from its ring, at its gain; its end told to the main thread
void mix_stream(Stream& stream, float* out, int frames) {
    if (!stream.on || stream.paused) {
        return;
    }
    // whether more is coming first, then what is there: a reader done has put all of it
    const bool more = stream.reading.load(std::memory_order_acquire);
    FrameRing& ring = stream.ring;
    const std::uint64_t taken = ring.taken.load(std::memory_order_relaxed);
    const std::uint64_t queued = ring.written.load(std::memory_order_acquire) - taken;
    const int have = static_cast<int>(std::min<std::uint64_t>(queued, frames));
    for (int i = 0; i < have; ++i) {
        const std::size_t at = static_cast<std::size_t>((taken + i) & (FrameRing::size - 1)) * 2;
        // toward the target by the fade's step a frame (a step of 1 gets there at once)
        if (stream.gain < stream.target) {
            stream.gain = std::min(stream.target, stream.gain + stream.fade_step);
        } else if (stream.gain > stream.target) {
            stream.gain = std::max(stream.target, stream.gain - stream.fade_step);
        }
        out[i * 2] += stream.gain * ring.data[at];
        out[i * 2 + 1] += stream.gain * ring.data[at + 1];
    }
    ring.taken.store(taken + static_cast<std::uint64_t>(have), std::memory_order_release);
    stream.heard.fetch_add(static_cast<std::uint64_t>(have), std::memory_order_relaxed);
    // done: the file's end heard, or faded out to be stopped
    const bool drained = have < frames && !more;
    const bool faded = stream.stop_at_silence && stream.gain <= 0.0f;
    if (drained || faded) {
        stream.on = false;
        std::uint32_t mine = stream.gen;
        stream.live.compare_exchange_strong(mine, 0, std::memory_order_acq_rel);
    }
}

} // namespace

float place_gain(const Listener& l, const float place[3]) {
    const float d[3] = {place[0] - l.position[0], place[1] - l.position[1],
                        place[2] - l.position[2]};
    return distance_gain(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
}

void render_audio(Audio& audio, float* out, int frames) {
    const std::lock_guard<std::mutex> hold(audio.mix_lock);
    run_commands(audio);
    Mixer& mix = audio.mix;
    float loudest = 0.0f;
    int playing = 0;
    for (int done = 0; done < frames;) {
        const int n = std::min(mix_block, frames - done);
        float* o = out + done * 2;
        std::memset(o, 0, sizeof(float) * static_cast<std::size_t>(n) * 2);
        std::memset(mix.send, 0, sizeof(float) * static_cast<std::size_t>(n));

        // sound effects
        playing = 0;
        for (Bank* bank : mix.banks) {
            if (!bank) {
                continue;
            }
            const float base = bank_gain(audio, *bank);
            for (Cue& cue : bank->cues) {
                for (int v = 0; v < static_cast<int>(cue.voices.size()); ++v) {
                    const Voice& voice = cue.voices[static_cast<std::size_t>(v)];
                    if (voice.playing && voice.paused == 0) {
                        mix_voice(audio, cue, v, base, o, n);
                        playing += 1;
                    }
                }
            }
        }
        for (Ghost& ghost : mix.ghosts) {
            if (ghost.sample) {
                mix_ghost(audio, ghost, o, n);
            }
        }

        // the 3D voices' reverb
        if (mix.reverb.on) {
            run_reverb(mix.reverb, mix.send, o, n);
        }

        // the PC's effects volume over the effects and their reverb
        for (int i = 0; i < n * 2; ++i) {
            o[i] *= mix.pc_effects;
        }

        // music and the movie, at the PC's music volume
        float music[mix_block * 2];
        std::memset(music, 0, sizeof(float) * static_cast<std::size_t>(n) * 2);
        for (Stream& stream : audio.streams) {
            mix_stream(stream, music, n);
        }
        for (int i = 0; i < n * 2; ++i) {
            o[i] += music[i] * mix.pc_music;
        }

        // what leaves: at the PC's master volume, clipped, as the DAC would
        for (int i = 0; i < n * 2; ++i) {
            const float x = o[i] * mix.pc_master;
            loudest = std::max(loudest, std::fabs(x));
            o[i] = std::clamp(x, -1.0f, 1.0f);
        }
        done += n;
    }
    audio.voices_playing.store(playing, std::memory_order_relaxed);
    std::uint32_t seen = audio.peak_bits.load(std::memory_order_relaxed);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &loudest, sizeof bits);
    float seen_value = 0.0f;
    std::memcpy(&seen_value, &seen, sizeof seen_value);
    while (loudest > seen_value &&
           !audio.peak_bits.compare_exchange_weak(seen, bits, std::memory_order_relaxed)) {
        std::memcpy(&seen_value, &seen, sizeof seen_value);
    }
}

void set_pc_volumes(Audio& audio, float master, float music, float effects) {
    audio.pc_master = std::clamp(master, 0.0f, 1.0f);
    audio.pc_music = std::clamp(music, 0.0f, 1.0f);
    audio.pc_effects = std::clamp(effects, 0.0f, 1.0f);
    Command c;
    c.kind = CommandKind::pc_volumes;
    c.f[0] = audio.pc_master;
    c.f[1] = audio.pc_music;
    c.f[2] = audio.pc_effects;
    send_command(audio, c);
}

float audio_peak(Audio& audio) {
    const std::uint32_t bits = audio.peak_bits.exchange(0, std::memory_order_relaxed);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}
