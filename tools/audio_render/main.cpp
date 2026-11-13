// pc_audio_render: plays sounds and music through the engine (src/audio) and prints the levels,
// offline into a WAV, or live through SDL's device (for the dummy or disk driver; never run it on
// the owner's speakers). docs/audio.md, "Checking".
//
//   pc_audio_render --data DIR [--out mix.wav] [--seconds 5] [--device]
//       [--event "T COMMAND ..."]...
//
// Commands at T seconds: "bank SLOT NAME", "sound BANK CUE [loop] [once] [at X Y Z]",
// "stop BANK CUE", "stop-all", "pitch BANK CUE VOICE P", "move BANK CUE VOICE X Y Z",
// "pause-sounds MODE", "level-sounds L", "music TRACK [loop] [fade S]", "stop-music [S]",
// "level-music L", "pause-music MODE", "reverb NAME", "listener X Y Z FX FY FZ", "movie PATH".
#include "audio/audio.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Event {
    double at = 0.0;
    std::string command;
};

void run(Audio& audio, const std::string& line) {
    std::istringstream in(line);
    std::string verb;
    in >> verb;
    if (verb == "bank") {
        int slot = 0;
        std::string name;
        in >> slot >> name;
        load_sound_bank(audio, slot, name);
    } else if (verb == "sound") {
        int bank = 0, cue = 0;
        in >> bank >> cue;
        unsigned flags = 0;
        float at[3] = {};
        std::string word;
        while (in >> word) {
            if (word == "loop") {
                flags |= sound_loop;
            } else if (word == "once") {
                flags |= sound_once;
            } else if (word == "at") {
                in >> at[0] >> at[1] >> at[2];
                flags |= sound_positioned;
            }
        }
        const int voice = play_sound(audio, bank, cue, flags, at);
        std::printf("  sound %d/%d -> voice %d\n", bank, cue, voice);
    } else if (verb == "stop") {
        int bank = 0, cue = 0;
        in >> bank >> cue;
        stop_sound(audio, bank, cue);
    } else if (verb == "stop-all") {
        stop_all_sounds(audio);
    } else if (verb == "pitch") {
        int bank = 0, cue = 0, voice = 0, pitch = 0;
        in >> bank >> cue >> voice >> pitch;
        set_sound_pitch(audio, bank, cue, voice, pitch);
    } else if (verb == "move") {
        int bank = 0, cue = 0, voice = 0;
        float at[3] = {};
        in >> bank >> cue >> voice >> at[0] >> at[1] >> at[2];
        set_sound_position(audio, bank, cue, voice, at);
    } else if (verb == "pause-sounds") {
        int mode = 0;
        in >> mode;
        pause_sounds(audio, mode);
    } else if (verb == "level-sounds") {
        int level = 225;
        in >> level;
        set_sound_level(audio, level);
    } else if (verb == "music") {
        int track = 0;
        in >> track;
        bool loop = false;
        float fade = 0.0f;
        std::string word;
        while (in >> word) {
            loop = loop || word == "loop";
            if (word == "fade") {
                in >> fade;
            }
        }
        play_music(audio, track, loop, 1.0f, fade);
        std::printf("  music %d -> playing %d\n", track, music_track(audio));
    } else if (verb == "stop-music") {
        float fade = 0.0f;
        in >> fade;
        stop_music(audio, fade);
    } else if (verb == "level-music") {
        int level = 255;
        in >> level;
        set_music_level(audio, level);
    } else if (verb == "pause-music") {
        int mode = 0;
        in >> mode;
        pause_music(audio, mode);
    } else if (verb == "reverb") {
        std::string name;
        in >> name;
        ReverbPreset preset;
        if (find_reverb(audio, name, preset)) {
            set_reverb(audio, preset);
        } else {
            std::printf("  no reverb preset %s\n", name.c_str());
        }
    } else if (verb == "listener") {
        float at[3] = {}, forward[3] = {0, 0, 1};
        const float up[3] = {0, 1, 0};
        in >> at[0] >> at[1] >> at[2] >> forward[0] >> forward[1] >> forward[2];
        set_listener(audio, at, forward, up);
    } else if (verb == "movie") {
        std::string path;
        in >> path;
        std::printf("  movie %s: %s\n", path.c_str(), play_movie_audio(audio, path) ? "ok" : "no");
    } else {
        std::printf("  unknown command: %s\n", line.c_str());
    }
}

double db(double x) {
    return x > 1e-9 ? 20.0 * std::log10(x) : -180.0;
}

void write_wav(const std::string& path, const std::vector<float>& mix) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return;
    }
    const std::uint32_t data = static_cast<std::uint32_t>(mix.size() * 2);
    auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, file); };
    auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, file); };
    std::fwrite("RIFF", 1, 4, file);
    u32(36 + data);
    std::fwrite("WAVEfmt ", 1, 8, file);
    u32(16);
    u16(1);
    u16(2);
    u32(audio_rate);
    u32(audio_rate * 4);
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, file);
    u32(data);
    for (float x : mix) {
        const auto s =
            static_cast<std::int16_t>(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f));
        std::fwrite(&s, 2, 1, file);
    }
    std::fclose(file);
}

} // namespace

int main(int argc, char** argv) {
    std::string data_dir = "data";
    std::string out;
    double seconds = 5.0;
    bool device = false;
    std::vector<Event> events;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out = argv[++i];
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::atof(argv[++i]);
        } else if (arg == "--device") {
            device = true;
        } else if (arg == "--event" && i + 1 < argc) {
            std::istringstream in(argv[++i]);
            Event event;
            in >> event.at;
            std::getline(in >> std::ws, event.command);
            events.push_back(event);
        } else {
            std::fprintf(stderr,
                         "usage: %s --data DIR [--out mix.wav] [--seconds S] [--device] "
                         "[--event \"T COMMAND\"]...\n",
                         argv[0]);
            return 2;
        }
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const Event& a, const Event& b) { return a.at < b.at; });

    AudioOptions options;
    options.device = device;
    options.stream_thread = device;
    AudioPtr audio = init_audio(data_dir, options);
    if (!audio) {
        return 1;
    }

    // a quarter second at a time: events due, then the mix (offline) or a wait (live)
    constexpr int window = audio_rate / 4;
    std::vector<float> mix;
    std::vector<float> block(static_cast<std::size_t>(window) * 2);
    std::size_t next = 0;
    const int windows = static_cast<int>(std::ceil(seconds * 4.0));
    const std::uint64_t start = SDL_GetTicksNS();
    std::printf("   time   rms L   rms R    peak\n");
    for (int w = 0; w < windows; ++w) {
        const double now = w * 0.25;
        while (next < events.size() && events[next].at < now + 0.25) {
            std::printf("%7.2f %s\n", events[next].at, events[next].command.c_str());
            run(*audio, events[next].command);
            ++next;
        }
        if (device) {
            while (static_cast<double>(SDL_GetTicksNS() - start) * 1e-9 < now + 0.25) {
                update_audio(*audio);
                SDL_Delay(5);
            }
            std::printf("%7.2f  peak %6.1f dB\n", now, db(audio_peak(*audio)));
            continue;
        }
        double sl = 0.0, sr = 0.0;
        float peak = 0.0f;
        // 10 ms blocks with the streams read between, as the reader thread would
        for (int done = 0; done < window; done += 480) {
            pump_audio_streams(*audio);
            render_audio(*audio, block.data() + done * 2, std::min(480, window - done));
        }
        for (int i = 0; i < window; ++i) {
            const float l = block[static_cast<std::size_t>(i) * 2];
            const float r = block[static_cast<std::size_t>(i) * 2 + 1];
            sl += l * l;
            sr += r * r;
            peak = std::max({peak, std::fabs(l), std::fabs(r)});
        }
        std::printf("%7.2f  %6.1f  %6.1f  %6.1f dB\n", now, db(std::sqrt(sl / window)),
                    db(std::sqrt(sr / window)), db(peak));
        mix.insert(mix.end(), block.begin(), block.end());
    }
    if (!out.empty() && !device) {
        write_wav(out, mix);
        std::printf("wrote %s\n", out.c_str());
    }
    audio.reset();
    SDL_Quit();
    return 0;
}
