#pragma once
// The stage's sound: the ambience per stage (stage_ambience_enter, _update, _exit 0x73620..0x7431D:
// the city's distant traffic, Shibuya's underground rooms and their reverb, the sea at the
// Aqualine), the supply craft's and the sky craft's engine loops moved with them, and the world's
// stage events played. Presentation: it reads the world after each step and never writes it.
#include "audio/audio.hpp"
#include "stage/stage_view.hpp"
#include "stage/stage_world.hpp"

#include <cstdint>

// AmbienceVoice (include/stage.h)
struct AmbienceVoice {
    int timer = 0; // steps to the next play
    bool playing = false;
    int bank = -1, cue = -1;
    int voice = -1;
};

struct StageAudio {
    int selector = -1;
    int reverb = 0; // Shibuya: 0 city, 1 room, 2 the second room
    AmbienceVoice voices[4];
    int craft_voice = -1; // the two bank 1 cue 0x2F loops
    int sky_voice = -1;
    int craft_state = 0;
    std::uint32_t crt = 1; // rand_float_crt's own generator (the CRT's LCG), off the world
};

// stage_ambience_enter for the selector, the banks it plays from loaded, and the craft loops
// started (stage_route_choose's sound_loop_3d). actor is where the human fighter stands (the
// free camera until fighters are in the world)
void stage_audio_start(StageAudio& sound, Audio& audio, const StageData& data, int selector,
                       const float actor[3], const StageView& view, const StageWorld& world);
// after each world step: the step's sounds, the loops moved, stage_ambience_update
void stage_audio_step(StageAudio& sound, Audio& audio, const StageData& data,
                      const StageWorld& world, const StageView& view, const float actor[3]);
// stage_ambience_exit: reverb off, the ambience stopped
void stage_audio_stop(StageAudio& sound, Audio& audio);
// sound_listener_update: the camera's eye, its forward and up (yaw 0 along +z, pitch up)
void stage_audio_listener(Audio& audio, const float eye[3], float yaw, float pitch);
