// Music and the movie's audio (docs/audio.md, "Music"): WAV files streamed, read ahead by the
// reader thread as GenkiLib's stream thread reads 0x24000-byte blocks
// (src/hand/genkilib_sound_cmd.c), converted there to the mix's 48 kHz float by an SDL_AudioStream
// of the reader's own and handed to the mix through the stream's FrameRing (audio_state.hpp). A
// track loops whole, from its data's start, as func_000E13A0 does; the cue and LIST chunks' loop
// marks are never honoured.
#include "audio/audio_state.hpp"
#include "audio/wav.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr int read_bytes = 0x24000;   // one of the original's stream packets
constexpr float ahead_seconds = 1.0f; // read this far ahead of the mix
constexpr int convert_frames = 4096;  // frames moved from convert to the ring at once

const SDL_AudioSpec mix_spec = {SDL_AUDIO_F32, 2, audio_rate};

bool wav_spec(const WavInfo& info, SDL_AudioSpec& spec) {
    spec.channels = info.channels;
    spec.freq = info.rate;
    if (info.format == 3) {
        spec.format = SDL_AUDIO_F32LE;
    } else if (info.bits == 8) {
        spec.format = SDL_AUDIO_U8;
    } else if (info.bits == 16) {
        spec.format = SDL_AUDIO_S16LE;
    } else if (info.bits == 32) {
        spec.format = SDL_AUDIO_S32LE;
    } else {
        return false; // 24-bit: the extractor writes 16
    }
    return true;
}

// the converted frames waiting in convert put into the ring, as many as it has room for; the
// frames moved
int to_ring(Stream& stream) {
    static float block[convert_frames * 2];
    FrameRing& ring = stream.ring;
    const std::uint64_t written = ring.written.load(std::memory_order_relaxed);
    const std::uint64_t room =
        FrameRing::size - (written - ring.taken.load(std::memory_order_acquire));
    const int want = static_cast<int>(std::min<std::uint64_t>(room, convert_frames));
    if (want <= 0) {
        return 0;
    }
    const int got = std::max(0, SDL_GetAudioStreamData(stream.convert, block,
                                                       want * 2 * static_cast<int>(sizeof(float))));
    const int frames = got / (2 * static_cast<int>(sizeof(float)));
    for (int i = 0; i < frames; ++i) {
        const std::size_t at = static_cast<std::size_t>((written + i) & (FrameRing::size - 1)) * 2;
        ring.data[at] = block[i * 2];
        ring.data[at + 1] = block[i * 2 + 1];
    }
    ring.written.store(written + static_cast<std::uint64_t>(frames), std::memory_order_release);
    return frames;
}

// reads ahead into the stream's ring, for the start the mix has taken only; under files_lock
void fill(Stream& stream) {
    if (!stream.file || !stream.reading.load(std::memory_order_acquire) ||
        stream.mix_gen.load(std::memory_order_acquire) != stream.want) {
        return;
    }
    static unsigned char block[read_bytes];
    const std::uint64_t ahead = static_cast<std::uint64_t>(ahead_seconds * audio_rate);
    while (stream.ring.written.load(std::memory_order_relaxed) -
               stream.ring.taken.load(std::memory_order_acquire) <
           ahead) {
        if (to_ring(stream) > 0) {
            continue;
        }
        // convert has nothing ready: more of the file, or the end
        if (stream.read_at >= stream.data_end) {
            if (!stream.loop) {
                if (!stream.flushed) {
                    SDL_FlushAudioStream(stream.convert);
                    stream.flushed = true;
                    continue;
                }
                // all of it in the ring: the mix may end the stream once it has played it
                stream.reading.store(false, std::memory_order_release);
                return;
            }
            stream.read_at = stream.data_start;
        }
        std::int64_t want = std::min<std::int64_t>(read_bytes, stream.data_end - stream.read_at);
        want -= want % stream.block_align;
        std::fseek(stream.file, static_cast<long>(stream.read_at), SEEK_SET);
        const std::size_t got =
            want > 0 ? std::fread(block, 1, static_cast<std::size_t>(want), stream.file) : 0;
        if (got == 0) {
            // a file shorter than its header says (or unreadable): it ends here, unlooped, so a
            // bad file can't spin the reader
            stream.read_at = stream.data_end;
            stream.loop = false;
            continue;
        }
        SDL_PutAudioStreamData(stream.convert, block, static_cast<int>(got));
        stream.read_at += static_cast<std::int64_t>(got);
    }
}

// a stream stopped where it is: the mix cuts it (a few ms' fade), and the reader lets its file go
void cut_stream(Audio& audio, int index) {
    Command c;
    c.kind = CommandKind::stream_cut;
    c.bank = index;
    send_command(audio, c);
    Stream& stream = audio.streams[index];
    const std::lock_guard<std::mutex> hold(audio.files_lock);
    if (stream.file) {
        std::fclose(stream.file);
        stream.file = nullptr;
    }
    stream.reading.store(false, std::memory_order_release);
}

// opens a WAV on a stream from its start and has the mix hear it from `gain` (faded in over
// fade_seconds, 0 at once) once it takes the start; false (and the stream left stopped) when the
// file can't be read. Whatever played on the stream is cut first, as the original's
// stream_cmd_start closes the stream before it opens the next
bool start_stream(Audio& audio, int index, const std::string& path, bool loop, float gain,
                  float fade_seconds, int track) {
    cut_stream(audio, index);
    Stream& stream = audio.streams[index];
    std::uint32_t gen = 0;
    {
        const std::lock_guard<std::mutex> hold(audio.files_lock);
        std::FILE* file = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
        WavInfo info;
        SDL_AudioSpec spec{};
        if (!file || !read_wav_header(file, info) || !wav_spec(info, spec)) {
            if (file) {
                std::fclose(file);
                SDL_Log("audio: can't stream %s", path.c_str());
            }
            return false;
        }
        SDL_ClearAudioStream(stream.convert);
        SDL_SetAudioStreamFormat(stream.convert, &spec, &mix_spec);
        stream.file = file;
        stream.data_start = info.data_start;
        stream.data_end = info.data_start + info.data_size;
        stream.read_at = info.data_start;
        stream.block_align = info.block_align;
        stream.loop = loop;
        stream.flushed = false;
        audio.stream_gen = audio.stream_gen + 1 == 0 ? 1 : audio.stream_gen + 1;
        gen = audio.stream_gen;
        stream.want = gen;
        stream.reading.store(true, std::memory_order_release);
    }
    stream.track = track;
    stream.live.store(gen, std::memory_order_release);
    Command c;
    c.kind = CommandKind::stream_start;
    c.bank = index;
    c.serial = gen;
    c.f[0] = gain;
    c.f[1] = fade_seconds;
    send_command(audio, c);
    return true;
}

void fade_stream(Audio& audio, int index, float fade_seconds) {
    audio.streams[index].faded = ++audio.fades;
    Command c;
    c.kind = CommandKind::stream_fade;
    c.bank = index;
    c.f[0] = fade_seconds;
    send_command(audio, c);
}

float music_gain(const Audio& audio) {
    // stream_volume_set: (unsigned char)(int)(level * scale) through the millibel table
    const int k =
        static_cast<int>(static_cast<float>(audio.music_level) * audio.music_scale) & 0xFF;
    return level_gain(audio, k);
}

bool stream_live(const Stream& stream) {
    return stream.live.load(std::memory_order_acquire) != 0;
}

int SDLCALL read_ahead(void* data) {
    Audio& audio = *static_cast<Audio*>(data);
    // the music's feed outranks the game's own threads: a reader put off the CPU starves the mix
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    while (!audio.quit.load(std::memory_order_acquire)) {
        pump_audio_streams(audio);
        SDL_Delay(10);
    }
    return 0;
}

} // namespace

void make_streams(Audio& audio) {
    const SDL_AudioSpec cd = {SDL_AUDIO_S16LE, 2, 44100};
    for (Stream& stream : audio.streams) {
        stream.convert = SDL_CreateAudioStream(&cd, &mix_spec);
    }
    if (audio.options.stream_thread) {
        audio.reader = SDL_CreateThread(read_ahead, "audio reader", &audio);
    }
}

void free_streams(Audio& audio) {
    audio.quit.store(true, std::memory_order_release);
    if (audio.reader) {
        SDL_WaitThread(audio.reader, nullptr);
        audio.reader = nullptr;
    }
    for (Stream& stream : audio.streams) {
        if (stream.file) {
            std::fclose(stream.file);
            stream.file = nullptr;
        }
        if (stream.convert) {
            SDL_DestroyAudioStream(stream.convert);
            stream.convert = nullptr;
        }
    }
}

void pump_audio_streams(Audio& audio) {
    const std::lock_guard<std::mutex> hold(audio.files_lock);
    for (Stream& stream : audio.streams) {
        fill(stream);
    }
}

void play_music(Audio& audio, int track, bool loop, float scale, float fade_seconds) {
    // func_000DE190: a track outside the table is ignored
    if (track < 0 || track >= static_cast<int>(audio.music_files.size())) {
        return;
    }
    // the playing track goes first, whether or not the new one opens (func_000E1A60)
    stop_music(audio, fade_seconds);
    // the music stream not heard, else the one faded out longest ago (the quieter)
    const Stream* s = audio.streams;
    int index = stream_live(s[0]) && !stream_live(s[1]) ? 1 : 0;
    if (stream_live(s[0]) && stream_live(s[1])) {
        index = s[0].faded <= s[1].faded ? 0 : 1;
    }
    const float kept_scale = audio.music_scale;
    audio.music_scale = std::clamp(scale, 0.0f, 1.0f);
    if (start_stream(audio, index, audio.music_files[static_cast<std::size_t>(track)], loop,
                     music_gain(audio), fade_seconds, track)) {
        audio.music_now = index;
    } else {
        audio.music_scale = kept_scale; // the scale is the started track's
    }
}

void stop_music(Audio& audio, float fade_seconds) {
    if (audio.music_now >= 0) {
        fade_stream(audio, audio.music_now, fade_seconds);
        audio.music_now = -1;
    }
}

void set_music_level(Audio& audio, int level, float scale) {
    scale = std::clamp(scale, 0.0f, 1.0f);
    level = std::clamp(level, 0, 255);
    // stream_volume_set skips an unchanged level at scale 1
    if (level == audio.music_level && scale == 1.0f) {
        return;
    }
    audio.music_level = level;
    audio.music_scale = scale;
    if (audio.music_now >= 0) {
        Command c;
        c.kind = CommandKind::stream_gain;
        c.bank = audio.music_now;
        c.f[0] = music_gain(audio);
        send_command(audio, c);
    }
}

void pause_music(Audio& audio, int mode) {
    if (audio.music_now < 0) {
        return;
    }
    Command c;
    c.kind = CommandKind::stream_pause;
    c.bank = audio.music_now;
    c.i0 = mode;
    send_command(audio, c);
}

int music_track(const Audio& audio) {
    const int now = audio.music_now;
    return now >= 0 && stream_live(audio.streams[now]) ? audio.streams[now].track : -1;
}

bool music_active(const Audio& audio) {
    return music_track(audio) >= 0;
}

bool play_movie_audio(Audio& audio, const std::string& path) {
    // movie_play: the millibel table at the music level, scaled by 960/10000 into ADX's 0.1 dB
    // steps, so 0.96 of the music's level in dB; movie_play also sets every mix-bin channel to
    // -500 mB, the 2D effects' cut (docs/movies.md)
    const int mb = audio.millibels[audio.music_level];
    return start_stream(audio, movie_stream, path, false, millibels_gain(mb * 96 / 100 - 500), 0.0f,
                        -1);
}

void stop_movie_audio(Audio& audio) {
    if (stream_live(audio.streams[movie_stream])) {
        fade_stream(audio, movie_stream, 0.0f);
    }
}

double movie_audio_seconds(const Audio& audio) {
    return static_cast<double>(audio.streams[movie_stream].heard.load()) / audio_rate;
}
