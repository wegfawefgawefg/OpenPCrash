// The output device and the engine's start and end (docs/audio.md, "Device"), on Mans In My
// House's audio_device.cpp: a stream of our own on the system's default playback device pulls the
// mix, so the device can be lost and opened again with everything kept as it was. Each ask is
// counted here (atomics only); audio_follow.cpp reads the counts on the main thread, follows the
// device and says what it sees.
#include "audio/audio_state.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace {

constexpr int pull_frames = 1024; // mixed at most at once
constexpr std::uint64_t slow_ns = 80'000'000;

// denormals flushed to zero on the device's thread: a reverb tail dying away into them costs a
// hundred times a normal number's work
void flush_denormals() {
#if defined(__SSE2__) || defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#elif defined(__aarch64__)
    std::uint64_t fpcr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr | (1ull << 24)));
#endif
}

void raise_to(std::atomic<std::uint64_t>& most, std::uint64_t value) {
    std::uint64_t seen = most.load(std::memory_order_relaxed);
    while (value > seen && !most.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
    }
}

// when it asked, the gap since the last, and how that sits with the usual (a gap over twice the
// usual is very likely an underrun)
void note_ask(AudioOut& out, std::uint64_t now) {
    const std::uint64_t last = out.last_pull.exchange(now, std::memory_order_relaxed);
    if (last == 0) {
        out.first_pull.store(now, std::memory_order_relaxed);
    }
    if (last == 0 || now <= last) {
        return;
    }
    const std::uint64_t gap = now - last;
    raise_to(out.worst_gap, gap);
    const std::uint64_t usual = out.usual_gap.load(std::memory_order_relaxed);
    if (usual > 0 && gap > 2 * usual) {
        out.late.fetch_add(1, std::memory_order_relaxed);
    }
    // eased, and never by a late one
    if (usual == 0) {
        out.usual_gap.store(gap, std::memory_order_relaxed);
    } else if (gap <= 2 * usual) {
        out.usual_gap.store(usual - usual / 16 + gap / 16, std::memory_order_relaxed);
    }
}

// the device wants more: the mix, a block at a time; a NaN or an infinity goes out as silence
void SDLCALL pull(void* data, SDL_AudioStream* stream, int additional, int) {
    Audio& audio = *static_cast<Audio*>(data);
    AudioOut& out = audio.out;
    const std::uint64_t asked_at = SDL_GetTicksNS();
    flush_denormals();
    // SDL's PipeWire backend runs this on the stream's own thread at the ordinary priority, which
    // a machine under load starves (SDL raises its own audio threads on the other backends); once
    // a stream, on its thread (MIMH b7c7f1277)
    if (out.raise_priority.exchange(false, std::memory_order_relaxed)) {
        out.priority.store(SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL) ? 1 : -1,
                           std::memory_order_relaxed);
        out.priority_took.store(SDL_GetTicksNS() - asked_at, std::memory_order_relaxed);
    }
    note_ask(out, asked_at);
    out.pulls.fetch_add(1, std::memory_order_relaxed);
    out.in_pull.store(true, std::memory_order_relaxed);
    static float block[pull_frames * 2];
    constexpr int frame_bytes = 2 * static_cast<int>(sizeof(float));
    while (additional > 0) {
        const int frames = std::min(pull_frames, (additional + frame_bytes - 1) / frame_bytes);
        render_audio(audio, block, frames);
        // x * 0 is 0 for every number but a NaN or an infinity
        float poison = 0.0f;
        for (int i = 0; i < frames * 2; ++i) {
            poison += block[i] * 0.0f;
        }
        if (poison != 0.0f) {
            std::fill(block, block + frames * 2, 0.0f);
            out.bad_blocks.fetch_add(1, std::memory_order_relaxed);
        }
        SDL_PutAudioStreamData(stream, block, frames * frame_bytes);
        out.frames.fetch_add(static_cast<std::uint64_t>(frames), std::memory_order_relaxed);
        additional -= frames * frame_bytes;
    }
    const std::uint64_t took = SDL_GetTicksNS() - asked_at;
    raise_to(out.worst_pull, took);
    if (took > slow_ns) {
        raise_to(out.slow_pull, took);
    }
    out.in_pull.store(false, std::memory_order_relaxed);
}

// SDL's word of a playback device going or coming (any thread), for the follow
bool SDLCALL watch(void* data, SDL_Event* event) {
    if ((event->type != SDL_EVENT_AUDIO_DEVICE_REMOVED &&
         event->type != SDL_EVENT_AUDIO_DEVICE_ADDED) ||
        event->adevice.recording) {
        return true;
    }
    Audio& audio = *static_cast<Audio*>(data);
    const std::lock_guard<std::mutex> hold(audio.out.events_lock);
    if (audio.out.events.size() < 256) {
        audio.out.events.push_back(
            {event->type == SDL_EVENT_AUDIO_DEVICE_ADDED, event->adevice.which});
    }
    return true;
}

} // namespace

double audio_seconds() {
    return static_cast<double>(SDL_GetTicksNS()) * 1.0e-9;
}

std::string audio_device_name(SDL_AudioDeviceID id) {
    const char* name = id ? SDL_GetAudioDeviceName(id) : nullptr;
    return name ? name : "";
}

bool open_output(Audio& audio, SDL_AudioDeviceID id) {
    AudioOut& out = audio.out;
    // the mix is 48 kHz stereo float, the APU's rate; SDL converts to whatever the device is
    const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, audio_rate};
    out.stream = SDL_OpenAudioDeviceStream(id, &spec, pull, &audio);
    if (!out.stream) {
        return false;
    }
    out.device = SDL_GetAudioStreamDevice(out.stream);
    out.chosen = id != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    out.on =audio_device_name(out.device);
    out.opened_at = audio_seconds();
    out.pulls.store(0, std::memory_order_relaxed);
    out.last_pull.store(0, std::memory_order_relaxed);
    out.first_pull.store(0, std::memory_order_relaxed);
    out.usual_gap.store(0, std::memory_order_relaxed);
    out.raise_priority.store(true, std::memory_order_relaxed);
    out.heard = false;
    out.quiet_from = -1.0;
    out.slow_said = false;
    out.state_said = false;
    out.off_default = 0;
    ++out.opens;
    SDL_ResumeAudioStreamDevice(out.stream);
    return true;
}

void close_output(Audio& audio) {
    if (audio.out.stream) {
        SDL_DestroyAudioStream(audio.out.stream);
    }
    audio.out.stream = nullptr;
    audio.out.device = 0;
    audio.out.on.clear();
}

void update_audio(Audio& audio) {
    free_released_banks(audio);
    if (audio.out.wanted) {
        follow_output(audio);
    }
}

void AudioDeleter::operator()(Audio* audio) const {
    if (!audio) {
        return;
    }
    if (audio->out.wanted) {
        SDL_RemoveEventWatch(watch, audio);
        close_output(*audio);
    }
    free_streams(*audio);
    delete audio;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

AudioPtr init_audio(const std::string& data_dir, const AudioOptions& options) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("audio: SDL audio: %s", SDL_GetError());
        return nullptr;
    }
    AudioPtr audio(new Audio);
    audio->data_dir = data_dir;
    audio->options = options;
    const char* trace = SDL_getenv("PHANTOMCRASH_SOUND_TRACE");
    audio->trace = trace && *trace && *trace != '0';
    load_volume_table(*audio);
    load_music_table(*audio);
    load_reverb_presets(*audio);
    make_reverb(audio->mix.reverb); // before any device asks
    make_streams(*audio);
    if (options.device) {
        audio->out.wanted = true;
        SDL_AddEventWatch(watch, audio.get());
        SDL_Log("audio: start driver=%s default=\"%s\"",
                SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none",
                audio_device_name(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK).c_str());
        follow_output(*audio); // the first open
    }
    return audio;
}
