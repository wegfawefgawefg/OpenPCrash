// Following the output device, each frame on the main thread (docs/audio.md "Device"), as Mans
// In My House's audio_device_follow.cpp after its sound died under load (MIMH b7c7f1277). The
// stream is opened on the system's default device and SDL moves it when the default changes.
// Ours is only closed and opened again when it is ours that went wrong:
// - SDL said the device it's on went away. A stream on a device we named is gone then; one on
//   the default is given a moment for SDL to move it, then opened again, and if a device of the
//   lost one's name comes back it's opened on that.
// - It stopped asking for sound for 3 s (not judged across a long main-thread frame, nor while one
//   ask is still being answered: that's our own mix late, and closing would wait on it), or a new
//   one never asked in 2 s.
// - It isn't on the system's default for 1.5 s.
// A device coming or going that isn't ours (a virtual sink, a screen share's capture, a headset
// elsewhere) leaves it be. Each open is on the default; after three misses in a row the devices
// SDL lists are tried in turn, with the default between. Everything is said with SDL_Log, so the
// run log (src/run_log.cpp) has it, and every 10 s a health line.
#include "audio/audio_state.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace {

constexpr double stall_seconds = 3.0;     // a device that asked before and hasn't for this long
constexpr double first_ask_seconds = 2.0; // a new stream that never asked
constexpr double move_seconds = 1.5;      // a default stream's device gone: SDL's time to move it
constexpr double quiet_seconds = 0.25;    // the asks stopped this long: said
constexpr double look_seconds = 0.5;      // the default and our device's names looked at
constexpr int off_default_looks = 3;      // looks in a row not on the default before moving
constexpr double health_seconds = 10.0;

double backoff(int misses) {
    return std::min(8.0, 0.25 * static_cast<double>(1 << std::clamp(misses, 0, 5)));
}

int ms(double seconds) {
    return static_cast<int>(seconds * 1000.0 + 0.5);
}

const std::string* known_name(const AudioOut& out, SDL_AudioDeviceID id) {
    for (const auto& [known, name] : out.names) {
        if (known == id) {
            return &name;
        }
    }
    return nullptr;
}

void remember(AudioOut& out, SDL_AudioDeviceID id, const std::string& name) {
    for (auto& [known, n] : out.names) {
        if (known == id) {
            n = name;
            return;
        }
    }
    if (out.names.size() < 64) {
        out.names.emplace_back(id, name);
    }
}

// what SDL said came and went since last frame; true if ours is gone for good
bool take_events(AudioOut& out, double now) {
    std::vector<DeviceEvent> events;
    {
        const std::lock_guard<std::mutex> hold(out.events_lock);
        events.swap(out.events);
    }
    bool gone = false;
    for (const DeviceEvent& event : events) {
        if (event.added) {
            const std::string name = audio_device_name(event.id);
            remember(out, event.id, name);
            const bool back = !out.lost.empty() && name == out.lost;
            SDL_Log("audio: device added id=%u name=\"%s\"%s", event.id, name.c_str(),
                    back ? " (the lost one back)" : "");
            out.back = back ? event.id : out.back;
            out.retry_at = out.stream ? out.retry_at : now; // none open: the time to try
            continue;
        }
        const std::string* known = known_name(out, event.id);
        std::string name = known ? *known : audio_device_name(event.id);
        // our stream's logical device (SDL drops a stream on a device we named with it), or the
        // device it's on, by name
        const bool logical = out.stream && event.id == out.device;
        const bool ours = logical || (out.stream && !name.empty() && name == out.on);
        name = logical ? out.on : name;
        SDL_Log("audio: device removed id=%u name=\"%s\"%s", event.id,
                name.empty() ? "?" : name.c_str(), ours ? " (ours)" : "");
        if (ours && out.lost.empty()) {
            out.lost = name;
            out.lost_at = now;
            out.back = 0;
        }
        gone = gone || (ours && (logical || out.chosen));
    }
    return gone;
}

// where to open: the lost device come back, else the default; after three misses in a row the
// devices SDL lists in turn with the default between
SDL_AudioDeviceID pick_device(AudioOut& out) {
    if (out.back) {
        return std::exchange(out.back, 0);
    }
    if (out.misses < 3 || out.misses % 2 == 1) {
        return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    }
    std::vector<SDL_AudioDeviceID> tries;
    int count = 0;
    if (SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count)) {
        for (int i = 0; i < count; ++i) {
            const std::string name = audio_device_name(ids[i]);
            remember(out, ids[i], name);
            if (name != out.lost) {
                tries.push_back(ids[i]);
            }
        }
        SDL_free(ids);
    }
    if (tries.empty()) {
        return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    }
    return tries[static_cast<std::size_t>(out.misses / 2 - 1) % tries.size()];
}

void open_now(Audio& audio, double now) {
    AudioOut& out = audio.out;
    const SDL_AudioDeviceID id = pick_device(out);
    const std::string asked = id == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK
                                  ? std::string("default")
                                  : "\"" + audio_device_name(id) + "\"";
    const double started = audio_seconds();
    if (!open_output(audio, id)) {
        ++out.misses;
        out.retry_at = now + backoff(out.misses);
        if (out.misses < 6 || out.misses % 20 == 0) {
            SDL_Log("audio: open %s no (%s), try %d, next in %dms", asked.c_str(), SDL_GetError(),
                    out.misses, ms(out.retry_at - now));
        }
        return;
    }
    SDL_AudioSpec have{};
    int frames = 0;
    SDL_GetAudioDeviceFormat(out.device, &have, &frames);
    SDL_Log("audio: open %s as=%u on=\"%s\" device %d ch %d Hz %d frames, took %dms, opens=%d",
            asked.c_str(), out.device, out.on.c_str(), have.channels, have.freq, frames,
            ms(audio_seconds() - started), out.opens);
}

// the default's name, said as it changes; whether ours is on it
bool on_default(AudioOut& out) {
    const std::string fallback = audio_device_name(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    if (!fallback.empty() && fallback != out.default_name) {
        SDL_Log("audio: default now=\"%s\"", fallback.c_str());
        out.default_name = fallback;
    }
    out.on = audio_device_name(out.device);
    if (fallback.empty() || out.on == fallback) {
        return true;
    }
    // a ghost default (the lost device's, not listed) isn't one to move to
    int count = 0;
    bool listed = false;
    if (SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count)) {
        for (int i = 0; i < count; ++i) {
            listed = listed || audio_device_name(ids[i]) == fallback;
        }
        SDL_free(ids);
    }
    return !listed;
}

int voices_playing(Audio& audio) {
    return audio.voices_playing.load(std::memory_order_relaxed); // the mix's, at its last ask
}

// every 10 s: how the device asked and what the effect calls did since the last line
void health(Audio& audio, double now) {
    AudioOut& out = audio.out;
    if (now - out.health_at < health_seconds) {
        return;
    }
    const double span = out.health_at > 0.0 ? now - out.health_at : health_seconds;
    out.health_at = now;
    static std::uint64_t pulls_then = 0, frames_then = 0;
    static SoundTally tally_then;
    const std::uint64_t pulls = out.pulls.load(std::memory_order_relaxed);
    const std::uint64_t frames = out.frames.load(std::memory_order_relaxed);
    const double gap = static_cast<double>(out.worst_gap.exchange(0)) * 1.0e-9;
    const double ask = static_cast<double>(out.worst_pull.exchange(0)) * 1.0e-9;
    const unsigned late = out.late.exchange(0);
    const SoundTally& t = audio.tally;
    const int priority = out.priority.load(std::memory_order_relaxed);
    SDL_Log("audio: health asks/s=%.0f frames/s=%.0f gap<=%dms ask<=%.1fms late=%u voices=%d "
            "starts=%llu cut=%llu refused=%llu stopped=%llu ring-full=%llu nan=%u on=\"%s\" "
            "default=\"%s\" opens=%d priority=%s",
            static_cast<double>(pulls >= pulls_then ? pulls - pulls_then : pulls) / span,
            static_cast<double>(frames - frames_then) / span, ms(gap), ask * 1000.0, late,
            voices_playing(audio), static_cast<unsigned long long>(t.starts - tally_then.starts),
            static_cast<unsigned long long>(t.cuts - tally_then.cuts),
            static_cast<unsigned long long>(t.refused - tally_then.refused),
            static_cast<unsigned long long>(t.stops - tally_then.stops),
            static_cast<unsigned long long>(t.overflows - tally_then.overflows),
            out.bad_blocks.load(std::memory_order_relaxed), out.on.c_str(),
            out.default_name.c_str(), out.opens,
            priority > 0 ? "raised" : priority < 0 ? "ordinary" : "?");
    pulls_then = pulls;
    frames_then = frames;
    tally_then = t;
}

} // namespace

void follow_output(Audio& audio) {
    AudioOut& out = audio.out;
    const double now = audio_seconds();
    // a long frame here (a stage loading, an open that waited on the sound server) is no time to
    // judge the device by
    const bool hitched = out.checked_at > 0.0 && now - out.checked_at > 1.0;
    out.checked_at = now;
    const bool gone = take_events(out, now);
    std::string why;
    if (out.stream) {
        const std::uint64_t last = out.last_pull.load(std::memory_order_relaxed);
        if (last != 0 && !out.heard) {
            // asking for sound: back to the default first if it goes again
            out.heard = true;
            out.misses = 0;
            out.lost.clear();
            out.back = 0;
            out.on = audio_device_name(out.device);
            const int priority = out.priority.load(std::memory_order_relaxed);
            const double first =
                static_cast<double>(out.first_pull.load(std::memory_order_relaxed)) * 1.0e-9;
            const double raising =
                static_cast<double>(out.priority_took.load(std::memory_order_relaxed)) * 1.0e-9;
            SDL_Log("audio: asking on=\"%s\" (%s) after=%dms priority=%s%s", out.on.c_str(),
                    out.chosen ? "named" : "default", ms(first - out.opened_at),
                    priority > 0   ? "raised in "
                    : priority < 0 ? "ordinary (SDL couldn't raise it)"
                                   : "?",
                    priority > 0 ? (std::to_string(ms(raising)) + "ms").c_str() : "");
        }
        if (!out.state_said && now - out.opened_at > 1.0) {
            out.state_said = true;
            SDL_Log("audio: a second on: %s, %d voices playing, gain %.2f master %.2f",
                    last != 0 ? "asking" : "not asking yet", voices_playing(audio),
                    SDL_GetAudioStreamGain(out.stream), audio.pc_master);
        }
        if (const std::uint64_t slow = out.slow_pull.exchange(0, std::memory_order_relaxed)) {
            SDL_Log("audio: one ask took %dms (the mix made late: the machine busy, or a lock "
                    "held)",
                    ms(static_cast<double>(slow) * 1.0e-9));
        }
        const double quiet = last != 0 ? now - static_cast<double>(last) * 1.0e-9
                                       : now - out.opened_at;
        if (out.heard && !hitched) {
            if (quiet > quiet_seconds && out.quiet_from < 0.0) {
                out.quiet_from = now - quiet;
                SDL_Log("audio: asks stopped on=\"%s\"", out.on.c_str());
            } else if (quiet < quiet_seconds && out.quiet_from >= 0.0) {
                SDL_Log("audio: asks back after %dms on=\"%s\"",
                        ms(now - quiet - out.quiet_from), audio_device_name(out.device).c_str());
                out.quiet_from = -1.0;
                out.slow_said = false;
            }
        }
        const bool judge = !hitched && now >= out.retry_at;
        if (gone) {
            why = "our device went away (" + out.lost + ")";
        } else if (!out.lost.empty()) {
            // a stream on the default whose device went: SDL moves it, or the lost one comes back
            const std::string at = audio_device_name(out.device);
            if (out.back) {
                why = "the lost device came back";
            } else if (!at.empty() && at != out.lost) {
                SDL_Log("audio: moved by SDL to on=\"%s\"", at.c_str());
                out.on = at;
                out.lost.clear();
            } else if (judge && now - out.lost_at > move_seconds &&
                       now - out.opened_at > move_seconds) {
                why = "still on the lost device after " + std::to_string(ms(now - out.lost_at)) +
                      "ms";
                ++out.misses;
            }
        } else if (judge && !out.heard && now - out.opened_at > first_ask_seconds) {
            why = "the new stream never asked for sound";
            ++out.misses;
        } else if (judge && out.heard && quiet > stall_seconds &&
                   out.in_pull.load(std::memory_order_relaxed)) {
            // stuck making the mix: a new device wouldn't help, and closing this one waits on it
            if (!out.slow_said) {
                out.slow_said = true;
                SDL_Log("audio: one ask has taken %dms (the mix itself); waiting", ms(quiet));
            }
        } else if (judge && out.heard && quiet > stall_seconds) {
            why = "the device stopped asking for sound (" + std::to_string(ms(quiet)) + "ms)";
            ++out.misses;
        } else if (now - out.looked_at > look_seconds) {
            out.looked_at = now;
            out.off_default = on_default(out) ? 0 : out.off_default + 1;
            if (out.off_default >= off_default_looks) {
                why = "not on the default (on=\"" + out.on + "\" default=\"" +
                      out.default_name + "\")";
            }
        }
        if (why.empty()) {
            health(audio, now);
            return;
        }
        SDL_Log("audio: %s; opening again (miss %d)", why.c_str(), out.misses);
        close_output(audio);
        // straight away the first time, then backing off
        out.retry_at = out.misses > 1 ? now + backoff(out.misses - 1) : now;
    }
    if (!out.stream && now >= out.retry_at) {
        open_now(audio, now);
    }
}
