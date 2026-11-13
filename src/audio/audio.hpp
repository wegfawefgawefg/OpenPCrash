#pragma once
// The sound engine (docs/audio.md): GenkiLib's sound on SDL3. Sound effect banks addressed by
// (bank, cue) as the original's sound_start, each cue with its own few voices played round robin;
// 3D cues placed against a listener with DirectSound's distance model; one music stream of whole
// files, looped whole; an I3DL2-like reverb on the 3D cues; movie audio. Presentation calls this
// from the main thread; the world never does.
//
// Units are the original's: volume levels 0..255 through its millibel table, pitch 4096 a octave
// from 48 kHz, positions in game units (the port frame: right-handed, y up).
#include "audio/sound_cues.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

struct Audio; // audio/audio_state.hpp; one per game, a local in main

// sound_start's flags (decomp SoundStart.flags, sound_start)
enum SoundFlags : unsigned {
    sound_loop = 1, // play looping
    sound_positioned =
        2, // a 3D cue, placed at the position given (a 3D cue needs it, a 2D one refuses it)
    sound_once = 4, // don't restart the voice in turn while it still plays
};

inline constexpr int sound_bank_slots = 8; // GenkiLib's pads (se_bank_load)

// a cue found by name (play_sound_named), bank -1 when there's none
struct SoundRef {
    int bank = -1;
    int cue = -1;
};

// DirectSound's I3DL2 listener (the XDK's DSI3DL2LISTENER, decomp ReverbPreset in tables.h), as
// the stage ambience sets it (stage_ambience_enter). Levels in millibels, times in seconds.
struct ReverbPreset {
    int room = -10000;
    int room_hf = 0;
    float room_rolloff = 0.0f;
    float decay_time = 1.0f;
    float decay_hf_ratio = 0.5f;
    int reflections = -10000;
    float reflections_delay = 0.02f;
    int reverb = -10000;
    float reverb_delay = 0.04f;
    float diffusion = 100.0f; // percent
    float density = 100.0f;   // percent
    float hf_reference = 5000.0f;
};
// what stage_ambience_exit sets: every level at -10000 mB (the defaults above)
inline constexpr ReverbPreset reverb_off{};

struct AudioOptions {
    bool device = true; // open an output device (false: render_audio only, for offline)
    bool stream_thread =
        true; // read music on a thread of its own (false: pump_audio_streams by hand)
};

// the engine lives on the heap (it holds locks and a thread); shut down when it goes
struct AudioDeleter {
    void operator()(Audio* audio) const;
};
using AudioPtr = std::unique_ptr<Audio, AudioDeleter>;

// start: the millibel and music tables from data_dir/audio, the reader thread, and a device unless
// told not to (none found is tried again by update_audio, so this only fails without SDL audio)
AudioPtr init_audio(const std::string& data_dir, const AudioOptions& options = {});
// each frame on the main thread: follows the output device (lost, moved, back)
void update_audio(Audio& audio);

// sound effect banks: data_dir/audio/se/<name>.sexp into a slot, volume the pad's scale
// (every bank the game loads uses 1.0). A slot already holding a bank, or the same name in
// another slot, is left as it is, as se_bank_load does.
bool load_sound_bank(Audio& audio, int slot, const std::string& name, float volume = 1.0f);
void unload_sound_bank(Audio& audio, int slot);
// a bank refusing starts, pitch and position while muted (func_000DD290 / func_000DD2B0)
void mute_sound_bank(Audio& audio, int slot, bool muted);

// sound_start: the cue's next voice in turn restarted (from the beginning), returned, or -1 when
// refused (no bank, no cue, muted, 3D-ness and sound_positioned disagree, sound_once and the voice
// still plays). The turn moves on either way. position is null for the origin.
int play_sound(Audio& audio, int bank, int cue, unsigned flags = 0,
               const float* position = nullptr);
int play_sound_named(Audio& audio, std::string_view name, unsigned flags = 0,
                     const float* position = nullptr);
SoundRef find_sound(const Audio& audio, std::string_view name);
// func_000DD230: the cue is a 3D one (the sound test's test before sound_start)
bool sound_is_3d(Audio& audio, int bank, int cue);
// sound_stop (func_000DE1E0): every voice of the cue, at once
void stop_sound(Audio& audio, int bank, int cue);
// func_000DD8A0: one voice of the cue
void stop_sound_voice(Audio& audio, int bank, int cue, int voice);
// PHANTOMCRASH_SOUND_TRACE=1 says every start, cut, refusal and stop (SDL_Log, so the run log
// has them too) with the step the game gives here; -1 outside a fight
void set_sound_trace_step(Audio& audio, long long step);
// se_stop_all: every voice of every bank, through the release envelope
void stop_all_sounds(Audio& audio);
// func_000DE270: one voice's pitch, 4096 a octave from 48 kHz (0 plays at 48 kHz, -500 at
// 44.1 kHz). Kept by the voice for its later starts, as the DirectSound buffer keeps it.
void set_sound_pitch(Audio& audio, int bank, int cue, int voice, int pitch);
// sound_voice_position_set: one voice of a 3D cue; voice -1 moves them all (func_000DE380)
void set_sound_position(Audio& audio, int bank, int cue, int voice, const float position[3]);
// VS (sound_play_at with GameState.vs_mode): every 3D sound starts at this point and never moves
// (sound_3d_position_set_outside_vs); null puts sounds back where they are played
void set_sound_fixed_point(Audio& audio, const float* point);
// func_000DD840: 0 pauses every playing voice, 1 resumes the paused, 2 does either per voice
void pause_sounds(Audio& audio, int mode);
// se_volume_set: the effects level 0..255 (a new game sets 225, sound_volumes_default)
void set_sound_level(Audio& audio, int level);
// the PC's master, music and effects volumes, 0..1, over everything the original sets (the movie's
// sound counts as music); master 0 mutes
void set_pc_volumes(Audio& audio, float master, float music, float effects);

// sound_listener_update: the camera's eye, its forward and its up (port frame)
void set_listener(Audio& audio, const float position[3], const float forward[3], const float up[3]);
// ds_reverb_set (IDirectSound_SetI3DL2Listener): reverb_off to switch it off
void set_reverb(Audio& audio, const ReverbPreset& preset);
// a preset of data/audio/reverb.sexp by name (city, room, room-2, aqualine, off); false if none
bool find_reverb(const Audio& audio, std::string_view name, ReverbPreset& preset);

// music_play: track is the row of the 202-name music file table; loop 1 loops the whole file.
// The level is the music level times scale. The original cuts the playing track at once;
// fade_seconds > 0 crossfades instead (a port option, nothing in the original asks for it).
void play_music(Audio& audio, int track, bool loop, float scale = 1.0f, float fade_seconds = 0.0f);
// stream_stop_request; fade_seconds as above
void stop_music(Audio& audio, float fade_seconds = 0.0f);
// stream_volume_set: the music level 0..255 (255 at boot and in a new game) and a 0..1 scale
void set_music_level(Audio& audio, int level, float scale = 1.0f);
// func_000DD7E0: 0 pauses the music, 1 resumes it, 2 toggles
void pause_music(Audio& audio, int mode);
// stream_current: the track playing, -1 when none
int music_track(const Audio& audio);
// func_000DD210: a track is still playing (or draining its last block)
bool music_active(const Audio& audio);

// the movie's audio (ADX converted to WAV): played once at the music level, as movie_play does
bool play_movie_audio(Audio& audio, const std::string& path);
void stop_movie_audio(Audio& audio);
// seconds of it heard so far, the movie's clock
double movie_audio_seconds(const Audio& audio);

// the mix: frames of 48 kHz stereo float. The device's callback calls this; offline renders call
// it directly (with AudioOptions{false, false} and pump_audio_streams before each call).
inline constexpr int audio_rate = 48000;
void render_audio(Audio& audio, float* out, int frames);
// reads music and movie files ahead (the stream thread's work)
void pump_audio_streams(Audio& audio);
// the loudest output sample since last asked (render_audio's)
float audio_peak(Audio& audio);
