#include "stage/stage_audio.hpp"

#include "audio/sound_cues.hpp"
#include "stage/stage_animate.hpp"

#include <cmath>

namespace {

// frame_cadence in the port: the ambience counts world steps, and retail writes its waits as
// frame_cadence * seconds, so the seconds are the same (docs/sixty.md, auto)
constexpr int cadence = 60;
constexpr int craft_cue = 0x2F; // supply_craft_engine_loop, bank 1

// rand_float_crt: the CRT's rand (0x343FD, 0x269EC3, 15 bits) over 32768 x range
float crt_float(StageAudio& sound, float range) {
    sound.crt = sound.crt * 0x343FDu + 0x269EC3u;
    return static_cast<float>((sound.crt >> 16) & 0x7FFF) / 32768.0f * range;
}

void reverb_named(Audio& audio, const char* name) {
    ReverbPreset preset;
    if (find_reverb(audio, name, preset)) {
        set_reverb(audio, preset);
    }
}

void point_of(const StageData& data, int index, float out[3]) {
    for (int k = 0; k < 3; ++k) {
        out[k] = data.ambience_points[index][k];
    }
}

float distance(const float a[3], const float b[3]) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

// three distant sounds over the fighter, 60 up, each again after
// 3, 4 and 5 s and up to half a second more
// decomp: city_ambience_update 0x738DC
void city_update(StageAudio& sound, Audio& audio, const float actor[3]) {
    const float at[3] = {actor[0], 60.0f, actor[2]};
    for (int i = 0; i < 3; ++i) {
        AmbienceVoice& voice = sound.voices[i];
        set_sound_position(audio, voice.bank, voice.cue, -1, at);
        if (voice.timer < 0) {
            play_sound(audio, voice.bank, voice.cue, sound_positioned, at);
            voice.timer = static_cast<int>(crt_float(sound, cadence * 0.5f) +
                                           static_cast<float>(cadence * (3 + i)));
        }
    }
}

// the underground rooms by the fighter's zone, with their reverb
// decomp: shibuya_ambience_update 0x73BEF
void shibuya_update(StageAudio& sound, Audio& audio, const StageData& data, int selector,
                    const float actor[3]) {
    const int area = stage_zone_at(selector, actor);
    AmbienceVoice& overhead = sound.voices[0];
    AmbienceVoice& point = sound.voices[1];
    const auto rooms = [&](int reverb, const char* preset, float lift, const float spot[3]) {
        if (sound.reverb != reverb) {
            sound.reverb = reverb;
            reverb_named(audio, preset);
        }
        const float at[3] = {actor[0], actor[1] + lift, actor[2]};
        if (!overhead.playing) {
            overhead.playing = true;
            play_sound(audio, bank_battle, se_shibuya_room_overhead_loop,
                       sound_loop | sound_positioned, at);
        } else {
            set_sound_position(audio, bank_battle, se_shibuya_room_overhead_loop, -1, at);
        }
        if (!point.playing) {
            point.voice =
                play_sound(audio, point.bank, point.cue, sound_loop | sound_positioned, spot);
            point.playing = true;
        } else {
            set_sound_position(audio, point.bank, point.cue, point.voice, spot);
        }
    };
    if (area == 3 || area == 0) {
        float spot[3];
        point_of(data, 1, spot);
        rooms(1, "room", 40.0f, spot);
        return;
    }
    if (area == 4 || area == 2) {
        // the nearer of points 2 and 3 (retail passes the offset from the fighter to it as a
        // position, docs/bugs.md; the port places it at the point)
        float a[3], b[3];
        point_of(data, 2, a);
        point_of(data, 3, b);
        rooms(2, "room-2", 100.0f, distance(a, actor) > distance(b, actor) ? b : a);
        return;
    }
    if (sound.reverb != 0) {
        sound.reverb = 0;
        reverb_named(audio, "city");
    }
    if (overhead.playing) {
        overhead.playing = false;
        stop_sound(audio, bank_battle, se_shibuya_room_overhead_loop);
    }
    if (point.playing) {
        stop_sound(audio, point.bank, point.cue);
        point.playing = false;
    }
}

// the sea's two 2D sounds, every 10 and 8 s and up to 3 s more
// decomp: aqua_ambience_update 0x740DD
void aqua_update(StageAudio& sound, Audio& audio) {
    if (sound.voices[0].timer < 0) {
        play_sound(audio, bank_battle, se_aqualine_ambience_a, 0);
        sound.voices[0].timer =
            static_cast<int>(crt_float(sound, cadence * 3.0f) + static_cast<float>(cadence * 10));
    }
    if (sound.voices[1].timer < 0) {
        // D_0022E930: (1, 27) or (1, 28), a coin toss
        const int cue = crt_float(sound, 2.0f) > 1.0f ? 27 : 28;
        play_sound(audio, bank_battle, cue, 0);
        sound.voices[1].timer =
            static_cast<int>(crt_float(sound, cadence * 3.0f) + static_cast<float>(cadence * 8));
    }
}

void fx_to(FxVec3 v, float out[3]) {
    out[0] = fx_float(v.x);
    out[1] = fx_float(v.y);
    out[2] = fx_float(v.z);
}

} // namespace

void stage_audio_start(StageAudio& sound, Audio& audio, const StageData& data, int selector,
                       const float actor[3], const StageView& view, const StageWorld& world) {
    sound = StageAudio{};
    sound.selector = selector;
    // the banks game_state_task loads at boot that the stage plays from
    load_sound_bank(audio, bank_battle, "battle");
    load_sound_bank(audio, bank_battle_old, "battle_old"); // the AI's guns and laser
    load_sound_bank(audio, bank_battle_player, "battle_player");
    // stage_ambience_voices_clear, then the stage's enter
    switch (selector / 2) {
    case 0: {
        reverb_named(audio, "city");
        const int cues[3] = {se_shinjuku_ambience_a, se_shinjuku_ambience_b,
                             se_shinjuku_ambience_c};
        for (int i = 0; i < 3; ++i) {
            sound.voices[i].bank = bank_battle;
            sound.voices[i].cue = cues[i];
            sound.voices[i].timer = static_cast<int>(crt_float(sound, cadence * 0.5f) +
                                                     static_cast<float>(cadence * (3 + i)));
        }
        break;
    }
    case 1:
        reverb_named(audio, "city");
        sound.voices[1].bank = bank_battle;
        sound.voices[1].cue = se_shibuya_room_point_loop;
        break;
    case 2: {
        reverb_named(audio, "aqualine");
        float at[3];
        point_of(data, 0, at);
        play_sound(audio, bank_battle, se_aqualine_ambience_loop, sound_loop | sound_positioned,
                   at);
        break;
    }
    default:
        reverb_named(audio, "city"); // stage_ambience_enter_67
        break;
    }
    (void)actor;
    // stage_route_choose: the engine loops at the sky craft and, outside the ending, the craft
    sound.sky_voice =
        play_sound(audio, bank_battle, craft_cue, sound_loop | sound_positioned, view.sky.pos);
    if (!world.setup.ending) {
        float at[3];
        fx_to(world.craft.pos, at);
        sound.craft_voice =
            play_sound(audio, bank_battle, craft_cue, sound_loop | sound_positioned, at);
    }
    sound.craft_state = world.craft.state;
}

void stage_audio_step(StageAudio& sound, Audio& audio, const StageData& data,
                      const StageWorld& world, const StageView& view, const float actor[3]) {
    // the step's sounds
    for (const StageEvent& event : stage_events(world)) {
        float at[3];
        fx_to(event.at, at);
        if (event.kind == StageEventKind::Sound) {
            play_sound(audio, event.a, event.b, event.flags,
                       event.flags & sound_positioned ? at : nullptr);
        } else if (event.kind == StageEventKind::SoundStop) {
            if (event.a == bank_battle && event.b == craft_cue) {
                // the wreck's end: retail stops every voice of the cue, the sky craft's too, and
                // no later craft starts its loop again (docs/bugs.md); the port stops the craft's
                stop_sound_voice(audio, bank_battle, craft_cue, sound.craft_voice);
                sound.craft_voice = -1;
            } else {
                stop_sound(audio, event.a, event.b);
            }
        }
    }
    // the loops follow the craft (while it is drawn) and the sky craft
    const SupplyCraft& craft = world.craft;
    if (craft.state != craft_parked && craft.state != craft_paused &&
        craft.state != craft_off) {
        float at[3];
        fx_to(craft.pos, at);
        if (sound.craft_voice < 0 && !world.setup.ending) {
            sound.craft_voice =
                play_sound(audio, bank_battle, craft_cue, sound_loop | sound_positioned, at);
        }
        set_sound_position(audio, bank_battle, craft_cue, sound.craft_voice, at);
    }
    sound.craft_state = craft.state;
    if (!world.setup.ending) {
        set_sound_position(audio, bank_battle, craft_cue, sound.sky_voice, view.sky.pos);
    }

    // stage_ambience_update, then every voice's timer down one
    switch (sound.selector / 2) {
    case 0:
        city_update(sound, audio, actor);
        break;
    case 1:
        shibuya_update(sound, audio, data, sound.selector, actor);
        break;
    case 2:
        if (!world.setup.ending) {
            aqua_update(sound, audio);
        }
        break;
    default:
        break;
    }
    for (AmbienceVoice& voice : sound.voices) {
        voice.timer -= 1;
    }
}

void stage_audio_stop(StageAudio& sound, Audio& audio) {
    set_reverb(audio, reverb_off);
    switch (sound.selector / 2) {
    case 0:
        for (int i = 0; i < 3; ++i) {
            stop_sound(audio, sound.voices[i].bank, sound.voices[i].cue);
        }
        break;
    case 1:
        stop_sound(audio, bank_battle, se_shibuya_room_point_loop);
        stop_sound(audio, bank_battle, se_shibuya_room_overhead_loop);
        break;
    case 2:
        stop_sound(audio, bank_battle, se_aqualine_ambience_a);
        stop_sound(audio, bank_battle, se_aqualine_ambience_b);
        stop_sound(audio, bank_battle, se_aqualine_ambience_loop);
        break;
    default:
        break;
    }
    stop_sound(audio, bank_battle, craft_cue);
    sound = StageAudio{};
}

void stage_audio_listener(Audio& audio, const float eye[3], float yaw, float pitch) {
    const float forward[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                              std::cos(yaw) * std::cos(pitch)};
    const float up[3] = {-std::sin(yaw) * std::sin(pitch), std::cos(pitch),
                         -std::cos(yaw) * std::sin(pitch)};
    set_listener(audio, eye, forward, up);
}
