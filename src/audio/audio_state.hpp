#pragma once
// What the sound engine holds (docs/audio.md, "Structure"). Two owners, and no lock the device's
// thread could be kept waiting on by a thread the machine has put off the CPU:
// - the main thread (the effect and music calls) owns everything outside `mix`, the voices' views
//   and the streams' main fields. It answers its own questions (the voice in turn, whether one
//   still plays) from its views and tells the mix what to do through `commands`, a ring of Command
//   with one putter (as retail queues its sound work, sound_command_push);
// - the mix (render_audio, on the device's thread) owns `mix`, each cue's `voices` and the streams'
//   mix fields. At the top of every ask it runs the commands waiting, then mixes. It answers back
//   through atomics (a voice's start that ended, a stream done, frames heard) and `released` (the
//   banks it let go of, freed by the main thread).
// mix_lock is held by whoever runs the commands: the mix at every ask, and the main thread only
// when the ring is full (no device asking: none open, or one stalled), so the device's thread never
// waits on the main thread. Music and the movie's sound are read by the reader thread under
// files_lock, converted to the mix's 48 kHz float there, and handed to the mix through each
// stream's FrameRing; the mix takes no lock for them.
#include "audio/audio.hpp"
#include "audio/reverb.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// a ring with one thread putting and one taking (the taker may change hands under a lock)
template <typename T, std::size_t N> struct Spsc {
    static_assert((N & (N - 1)) == 0, "a power of two");
    std::array<T, N> items{};
    std::atomic<std::size_t> head{0}; // the next put, the putter's
    std::atomic<std::size_t> tail{0}; // the next take, the taker's
    bool push(const T& item) {
        const std::size_t h = head.load(std::memory_order_relaxed);
        if (h - tail.load(std::memory_order_acquire) >= N) {
            return false;
        }
        items[h & (N - 1)] = item;
        head.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& item) {
        const std::size_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) {
            return false;
        }
        item = items[t & (N - 1)];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }
};

// interleaved stereo float frames from the reader to the mix: one putter, one taker
struct FrameRing {
    static constexpr std::uint64_t size = 1u << 17; // frames, 2.7 s at 48 kHz
    std::vector<float> data = std::vector<float>(size * 2);
    std::atomic<std::uint64_t> written{0}; // frames put, ever (the reader's)
    std::atomic<std::uint64_t> taken{0};   // frames taken, ever (the mix's)
};

// a cue's wave, PCM16 as the extractor wrote it
struct Sample {
    std::vector<std::int16_t> pcm; // interleaved
    int channels = 1;
    int frames = 0;
    // source frames a frame at pitch 0: the WAV's rate over the original's (1 unless resampled)
    double rate_ratio = 1.0;
};

// one DirectSound buffer of a cue (the original's SoundBuf), the mix's
struct Voice {
    bool playing = false;
    bool looping = false;
    int paused = 0;           // SoundBuf.state: 1 paused, 2 paused while looping
    double position = 0.0;    // source frames in
    int pitch = 0;            // 4096 a octave from 48 kHz; 0 from the load (func_000DDC50)
    float place[3] = {};      // where a 3D voice is, game units
    std::uint32_t serial = 0; // the start playing (VoiceView::serial)
    // ramped across a block
    float gain_left = 0.0f, gain_right = 0.0f, send = 0.0f;
    bool gains_known = false;
    float shade = 1.0f;     // behind the listener: low-pass coefficient (1 none)
    float shade_low = 0.0f; // its state
};

// the main thread's view of a voice: what it started and stopped, and where it put it
struct VoiceView {
    std::uint32_t serial = 0; // starts sent, the last one's number
    bool on = false;          // started with a wave and not stopped since (Cue::ended says whether
                              // the mix has played it out)
    bool looping = false;
    int paused = 0; // as Voice::paused
    int pitch = 0;
    float place[3] = {};
    std::uint64_t started_ns = 0; // for the trace's guess at how far in a voice cut was
};

struct Cue {
    std::string name;
    bool is3d = false;
    bool has_wave = false; // a cue whose wave the loader didn't find is silent, as the original's
    int next = 0;          // the voice in turn (CueVoice.next), the main thread's
    Sample sample;
    std::vector<Voice> voices;    // CueVoice.count of them, the mix's
    std::vector<VoiceView> views; // the same, the main thread's
    // per voice: the serial of the start the mix played to its end (the mix writes, main reads)
    std::unique_ptr<std::atomic<std::uint32_t>[]> ended;
};

struct Bank {
    bool used = false;
    bool muted = false; // the main thread's (refusals)
    std::string name;
    float volume = 1.0f; // SoundPad.volume
    std::vector<Cue> cues;
};

// a voice cut off (restarted or stopped) fading out over a few milliseconds, so it doesn't click
struct Ghost {
    const Sample* sample = nullptr;
    double position = 0.0;
    double step = 1.0;
    bool looping = false;
    float gain_left = 0.0f, gain_right = 0.0f, send = 0.0f;
    float fade = 0.0f; // 1 down to 0
};
inline constexpr int ghost_count = 32;

// one streamed WAV: music (two, for a crossfade) or the movie's audio. A start has a number (gen);
// the reader fills the ring only for the start the mix has taken (mix_gen == want), and the mix
// empties the ring when it takes one, so no frame of one start is ever heard as another's
struct Stream {
    // the main thread's and the reader's, under files_lock
    std::FILE* file = nullptr;
    std::int64_t data_start = 0;
    std::int64_t data_end = 0;
    std::int64_t read_at = 0; // file offset of the next read
    int block_align = 4;
    bool loop = false;
    bool flushed = false;               // the file's end put through convert
    std::uint32_t want = 0;             // the start the file is for
    SDL_AudioStream* convert = nullptr; // the reader's: the file's PCM to the mix's format
    // between the threads
    FrameRing ring;
    std::atomic<std::uint32_t> mix_gen{0}; // the start the mix has taken
    std::atomic<bool> reading{false};      // more of the file still to come into the ring
    std::atomic<std::uint32_t> live{0};    // the start heard now, 0 none: main sets, the mix ends
    std::atomic<std::uint64_t> heard{0};   // frames handed to the mix since its start
    // the mix's
    std::uint32_t gen = 0;  // the start it plays
    bool on = false;        // heard
    float gain = 0.0f;      // now, linear
    float target = 0.0f;    // where gain goes
    float fade_step = 1.0f; // gain per frame toward it (1: at once)
    bool stop_at_silence = false;
    bool paused = false;
    // the main thread's
    int track = -1;
    std::uint64_t faded = 0; // when it was last faded out (play_music takes the oldest fade)
};
inline constexpr int stream_count = 3;
inline constexpr int movie_stream = 2;

struct Listener {
    float position[3] = {0.0f, 0.01f, 0.0f}; // as func_000DE550 sets it at start
    float forward[3] = {0.0f, 0.0f, 1.0f};
    float up[3] = {0.0f, 1.0f, 0.0f};
    float right[3] = {1.0f, 0.0f, 0.0f};
};

// what the main thread tells the mix (audio_mix.cpp run_command)
enum class CommandKind : std::uint8_t {
    bank,         // banks[bank] = bank_ptr (null: none), the old one sent back
    start,        // a voice from its start: serial, loop (i0), place (f[0..2])
    stop_voice,   // one voice cut
    stop_cue,     // every voice of a cue cut
    stop_all,     // every voice cut
    pitch,        // a voice's pitch (i0)
    place,        // a voice's place (f[0..2]); voice -1 every voice of the cue
    pause,        // pause_sounds' mode (i0)
    sound_level,  // se_volume_set's level (i0)
    pc_volumes,   // master, music, effects (f[0..2])
    listener,     // the listener
    reverb,       // the reverb's preset
    stream_start, // stream `bank` heard from start gen (serial) at gain f[0], fade f[1] seconds
    stream_fade,  // stream `bank` faded out over f[0] seconds, then stopped
    stream_cut,   // stream `bank` stopped at once
    stream_gain,  // stream `bank` at gain f[0] at once
    stream_pause, // stream `bank`: i0 0 pause, 1 resume, 2 toggle
};

struct Command {
    CommandKind kind = CommandKind::stop_all;
    int bank = 0, cue = 0, voice = 0;
    int i0 = 0;
    std::uint32_t serial = 0;
    float f[3] = {};
    Bank* bank_ptr = nullptr;
    Listener listener;
    ReverbPreset preset;
};
inline constexpr std::size_t command_slots = 8192;

// a playback device SDL said came or went, as its event watch heard it (any thread)
struct DeviceEvent {
    bool added = false;
    SDL_AudioDeviceID id = 0;
};

// the output device (audio/audio_device.cpp, audio_follow.cpp). The device's thread only counts
// (the atomics); the main thread reads them, follows the device and says what it sees.
struct AudioOut {
    SDL_AudioStream* stream = nullptr;
    SDL_AudioDeviceID device = 0; // the stream's logical device
    bool wanted = false;
    // the device's thread's tallies (times SDL_GetTicksNS, spans ns)
    std::atomic<std::uint64_t> pulls{0};      // asks since opened
    std::atomic<std::uint64_t> frames{0};     // frames handed over, ever
    std::atomic<std::uint64_t> first_pull{0}; // the first ask since opened, 0 none
    std::atomic<std::uint64_t> last_pull{0};  // and the last
    std::atomic<std::uint64_t> usual_gap{0};  // the gap the device asks at (eased)
    std::atomic<std::uint64_t> worst_gap{0};  // the longest between two asks, since taken
    std::atomic<std::uint64_t> worst_pull{0}; // the longest one ask took, since taken
    std::atomic<std::uint64_t> slow_pull{0};  // the longest over 80 ms, since taken
    std::atomic<std::uint32_t> late{0};       // asks over twice the usual gap, since taken
    std::atomic<std::uint32_t> bad_blocks{0}; // with a NaN or an infinity, sent as silence
    std::atomic<bool> in_pull{false};         // an ask being answered now (the mix being made)
    std::atomic<bool> raise_priority{false};  // the next ask raises its thread's priority
    std::atomic<int> priority{0};             // 1 raised, -1 couldn't be, 0 not tried
    std::atomic<std::uint64_t> priority_took{0};
    // SDL's word of devices coming and going, from its event watch
    std::mutex events_lock;
    std::vector<DeviceEvent> events;
    // the main thread's
    std::string on;             // the device's name it's on, as last looked (SDL moves it)
    std::string lost;           // the device ours was on when it went, till sound goes again
    double lost_at = -1.0;      // when; SDL is given a moment to move a default stream
    SDL_AudioDeviceID back = 0; // the lost one come back (a new device of its name)
    std::vector<std::pair<SDL_AudioDeviceID, std::string>> names; // playback devices seen
    std::string default_name; // the system's default, as last looked
    double opened_at = 0.0;   // seconds
    double retry_at = 0.0;    // the next try while there's none
    double checked_at = 0.0;  // last frame's time (a long frame isn't judged by)
    double looked_at = 0.0;   // the default's name last looked at
    double health_at = 0.0;   // the last health line
    double quiet_from = -1.0; // the asks stopped (said), -1 not
    int misses = 0;           // opens in a row that failed or never asked
    int opens = 0;
    int off_default = 0;     // looks in a row on a device that isn't the default
    bool chosen = false;     // opened on a device we named, not the system's default
    bool heard = false;      // asked for sound since this one was opened
    bool slow_said = false;  // a stall that's our own mix taking long, said
    bool state_said = false; // the line a second after an open, said
};

// what the effect calls did, for the health line and PHANTOMCRASH_SOUND_TRACE (main thread)
struct SoundTally {
    std::uint64_t starts = 0;    // voices started
    std::uint64_t cuts = 0;      // of them, a voice cut that was still playing (the round robin)
    std::uint64_t refused = 0;   // starts refused (muted, flag 4, no cue, 2D/3D)
    std::uint64_t stops = 0;     // voices stopped that were playing
    std::uint64_t overflows = 0; // commands run by the main thread for a ring full
};

inline constexpr int mix_block = 256;        // frames mixed at once (gains ramp across one)
inline constexpr int music_table_rows = 202; // music_catalogue_register's 0xCA names

// the mix's own, touched only by whoever holds mix_lock
struct Mixer {
    Bank* banks[sound_bank_slots] = {};
    Ghost ghosts[ghost_count];
    Listener listener;
    Reverb reverb;
    int sound_level = 225; // se_volume_set's level (D_00AC6D7C)
    float pc_master = 1.0f, pc_music = 1.0f, pc_effects = 1.0f;
    // scratch
    float send[mix_block] = {};
    float wet[mix_block * 2] = {};
    float pulled[mix_block * 2] = {};
};

struct Audio {
    std::string data_dir;
    AudioOptions options;
    // the millibel table at 0x410318 (data/audio/volume.sexp), read by both, written at start
    short millibels[256] = {};
    // the main thread's
    Bank* banks[sound_bank_slots] = {};        // as the main thread has them (the mix follows)
    std::vector<std::unique_ptr<Bank>> loaded; // every bank not yet freed, the main thread's
    int music_level = 255;                     // stream_volume_set's level (D_00AC6D64)
    float music_scale = 1.0f;
    // the PC's own volumes over the original's two levels (set_pc_volumes, docs/settings.md)
    float pc_master = 1.0f, pc_music = 1.0f, pc_effects = 1.0f;
    Listener listener; // as last set (the trace's distances)
    // VS: every 3D sound at one point (vs_sound_pos), none moved after (set_sound_fixed_point)
    bool fixed_point = false;
    float fixed_place[3] = {};
    std::vector<std::string> music_files; // by track, "" for a name not on the disc
    std::vector<std::pair<std::string, ReverbPreset>> reverb_presets; // data/audio/reverb.sexp
    int music_now = -1;           // which stream is the current music
    std::uint32_t stream_gen = 0; // the last stream start's number
    std::uint64_t fades = 0;      // stream fades so far (Stream::faded)
    // PHANTOMCRASH_SOUND_TRACE=1: every start, cut, refusal and stop said with its step
    bool trace = false;
    long long trace_step = -1;
    SoundTally tally;
    // between the threads
    Stream streams[stream_count];
    Spsc<Command, command_slots> commands; // main -> mix
    Spsc<Bank*, 16> released;              // mix -> main: banks to free
    std::mutex mix_lock;                   // held while running the commands and mixing
    std::atomic<int> voices_playing{0};    // the mix's count at its last block
    std::mutex files_lock;
    SDL_Thread* reader = nullptr;
    std::atomic<bool> quit{false};
    AudioOut out;
    std::atomic<std::uint32_t> peak_bits{0};
    // the mix's
    Mixer mix;
};

// helpers shared by the audio files
inline float millibels_gain(int mb) {
    return mb <= -10000 ? 0.0f : static_cast<float>(SDL_pow(10.0, mb / 2000.0));
}
// the gain a 0..255 level gives through the millibel table
inline float level_gain(const Audio& audio, int level) {
    return millibels_gain(audio.millibels[level < 0 ? 0 : (level > 255 ? 255 : level)]);
}
// a cue of the main thread's banks, or null when bank or cue is out of range or not loaded
Cue* find_cue(Audio& audio, int bank, int cue);

// audio_banks.cpp: the tables from data/audio
void load_volume_table(Audio& audio);
void load_music_table(Audio& audio);
void load_reverb_presets(Audio& audio);
bool read_bank(const std::string& data_dir, const std::string& name, Bank& bank);
// audio_streams.cpp
void make_streams(Audio& audio);
void free_streams(Audio& audio);
// audio_commands.cpp: a command to the mix (run by the main thread itself when the ring is full);
// every command waiting run, under mix_lock (the mix's ask); the banks the mix let go of, freed
// (each main-thread update and before a load)
void send_command(Audio& audio, const Command& command);
void run_commands(Audio& audio);
void free_released_banks(Audio& audio);
// the distance gain a 3D voice at `place` gets from a listener (1 inside 2 units)
float place_gain(const Listener& listener, const float place[3]);
// audio_device.cpp: a stream on the device (the default, or one named), pulling; false if none
// could be opened
bool open_output(Audio& audio, SDL_AudioDeviceID id);
void close_output(Audio& audio);
double audio_seconds();
std::string audio_device_name(SDL_AudioDeviceID id);
// audio_follow.cpp: each frame, the device followed (update_audio's work)
void follow_output(Audio& audio);
