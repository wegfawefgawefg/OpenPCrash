// The sound effect calls, GenkiLib's sound_start and the setters around it (decomp
// src/o2/genkilib_0de830.c, src/hand/dsound_init_o2.c, genkilib_o2.c, voice_3d_o2.c,
// genkilib_sound_cmd.c), on the main thread. Each decides what it can from the main thread's views
// (the voice in turn, whether it still plays, a refusal) and sends the mix a command, which takes
// effect at the next mixed block (audio_state.hpp). The original queues some of these to its
// sound thread and calls DirectSound straight for the rest.
#include "audio/audio_state.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// a voice the main thread started that the mix hasn't played out, nor anyone stopped
bool live(const Cue& cue, int voice) {
    const VoiceView& view = cue.views[static_cast<std::size_t>(voice)];
    return view.on && cue.ended[static_cast<std::size_t>(voice)].load(std::memory_order_acquire) !=
                          view.serial;
}

// how much of a live voice is still heard, as the main thread can tell: its distance's gain (1 for
// a 2D one) times the part of its wave left, guessed from when it started at its pitch
float heard(const Audio& audio, const Cue& cue, int voice) {
    const VoiceView& view = cue.views[static_cast<std::size_t>(voice)];
    const float gain = cue.is3d ? place_gain(audio.listener, view.place) : 1.0f;
    if (cue.sample.frames <= 0) {
        return 0.0f;
    }
    const double seconds = static_cast<double>(SDL_GetTicksNS() - view.started_ns) * 1e-9;
    double position = seconds * audio_rate * cue.sample.rate_ratio * std::exp2(view.pitch / 4096.0);
    if (view.looping) {
        position = std::fmod(position, static_cast<double>(cue.sample.frames));
    }
    const double left = 1.0 - position / cue.sample.frames;
    return gain * static_cast<float>(left < 0.0 ? 0.0 : left);
}

// PHANTOMCRASH_SOUND_TRACE's line: what happened to which cue and voice, at the step the game
// said last (set_sound_trace_step), and a 3D voice's distance from the ears
void trace(const Audio& audio, const char* what, int bank, int cue, int voice, const char* why) {
    if (!audio.trace) {
        return;
    }
    const Bank* b = bank >= 0 && bank < sound_bank_slots ? audio.banks[bank] : nullptr;
    const Cue* c = b && cue >= 0 && cue < static_cast<int>(b->cues.size())
                       ? &b->cues[static_cast<std::size_t>(cue)]
                       : nullptr;
    char place[64] = "";
    if (c && c->is3d && voice >= 0 && voice < static_cast<int>(c->views.size())) {
        const float* v = c->views[static_cast<std::size_t>(voice)].place;
        const float* l = audio.listener.position;
        const float d[3] = {v[0] - l[0], v[1] - l[1], v[2] - l[2]};
        SDL_snprintf(place, sizeof place, " 3d %.1f units away",
                     std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
    }
    SDL_Log("sound step %lld %s %d/0x%X %s voice %d/%d%s%s%s", audio.trace_step, what, bank, cue,
            c && !c->name.empty() ? c->name.c_str() : "?", voice,
            c ? static_cast<int>(c->views.size()) : 0, place, why ? " " : "", why ? why : "");
}

// a voice the main thread no longer counts as playing (the mix is told apart)
void stop_view(Audio& audio, Cue& cue, int voice) {
    if (live(cue, voice)) {
        audio.tally.stops += 1;
    }
    VoiceView& view = cue.views[static_cast<std::size_t>(voice)];
    view.on = false;
    view.paused = 0;
}

Command voice_command(CommandKind kind, int bank, int cue, int voice) {
    Command c;
    c.kind = kind;
    c.bank = bank;
    c.cue = cue;
    c.voice = voice;
    return c;
}

bool is_origin(const float* p) {
    return p[0] == 0.0f && p[1] == 0.0f && p[2] == 0.0f;
}

} // namespace

bool load_sound_bank(Audio& audio, int slot, const std::string& name, float volume) {
    if (slot < 0 || slot >= sound_bank_slots) {
        return false;
    }
    // se_bank_load: a bank already loaded anywhere, or a slot in use, is left as it is
    for (const Bank* bank : audio.banks) {
        if (bank && bank->name == name) {
            return true;
        }
    }
    if (audio.banks[slot]) {
        return false;
    }
    // read here, then handed to the mix
    free_released_banks(audio);
    auto bank = std::make_unique<Bank>();
    if (!read_bank(audio.data_dir, name, *bank)) {
        return false;
    }
    bank->used = true;
    bank->volume = volume;
    audio.banks[slot] = bank.get();
    Command c = voice_command(CommandKind::bank, slot, 0, 0);
    c.bank_ptr = bank.get();
    audio.loaded.push_back(std::move(bank));
    send_command(audio, c);
    return true;
}

void unload_sound_bank(Audio& audio, int slot) {
    if (slot < 0 || slot >= sound_bank_slots || !audio.banks[slot]) {
        return;
    }
    // the mix lets go of it and sends it back to be freed (free_released_banks)
    audio.banks[slot] = nullptr;
    send_command(audio, voice_command(CommandKind::bank, slot, 0, 0));
}

void mute_sound_bank(Audio& audio, int slot, bool muted) {
    if (slot >= 0 && slot < sound_bank_slots && audio.banks[slot]) {
        audio.banks[slot]->muted = muted;
    }
}

int play_sound(Audio& audio, int bank, int cue, unsigned flags, const float* position) {
    Cue* c = find_cue(audio, bank, cue);
    if (!c || audio.banks[bank]->muted) {
        audio.tally.refused += 1;
        trace(audio, "refused", bank, cue, -1, c ? "(bank muted)" : "(no such bank or cue)");
        return -1;
    }
    // a 3D cue only with sound_positioned, a 2D one only without
    if (((flags & sound_positioned) != 0) != c->is3d) {
        audio.tally.refused += 1;
        trace(audio, "refused", bank, cue, -1, c->is3d ? "(3D cue, no place)" : "(2D cue, placed)");
        return -1;
    }
    // the voice in turn; the turn moves on whether it starts or not
    const int at = c->next;
    c->next = (c->next + 1) % static_cast<int>(c->views.size());
    if ((flags & sound_positioned) && audio.fixed_point) {
        position = audio.fixed_place;
    }
    VoiceView& view = c->views[static_cast<std::size_t>(at)];
    const bool cut = live(*c, at) && view.paused == 0;
    // how loud the voice cut still is, for the trace
    const float cut_heard = audio.trace && cut ? heard(audio, *c, at) : 0.0f;
    // the position goes to DirectSound only when it differs from the voice's spatial row, which
    // nothing but the load writes (zeros): so a null or origin position leaves the voice where it
    // last was (sound::differs)
    if ((flags & sound_positioned) && position && !is_origin(position)) {
        std::copy(position, position + 3, view.place);
    }
    if ((flags & sound_once) && cut) {
        audio.tally.refused += 1;
        trace(audio, "refused", bank, cue, at, "(flag 4: still playing)");
        return -1;
    }
    // started: counted, and sent with its place
    audio.tally.starts += 1;
    audio.tally.cuts += cut ? 1 : 0;
    view.serial += 1;
    view.on = c->has_wave;
    view.looping = (flags & sound_loop) != 0;
    view.paused = 0;
    view.started_ns = SDL_GetTicksNS();
    Command command = voice_command(CommandKind::start, bank, cue, at);
    command.serial = view.serial;
    command.i0 = view.looping ? 1 : 0;
    std::copy(view.place, view.place + 3, command.f);
    send_command(audio, command);
    char said[96] = "";
    if (cut) {
        SDL_snprintf(said, sizeof said, "(the voice in turn cut while still at about %.0f dB)",
                     cut_heard > 1e-6f ? 20.0f * std::log10(cut_heard) : -120.0f);
    }
    trace(audio, cut ? "start-cut" : "start", bank, cue, at,
          !c->has_wave ? "(no wave)"
          : cut        ? said
                       : nullptr);
    return at;
}

int play_sound_named(Audio& audio, std::string_view name, unsigned flags, const float* position) {
    const SoundRef ref = find_sound(audio, name);
    return ref.bank < 0 ? -1 : play_sound(audio, ref.bank, ref.cue, flags, position);
}

bool sound_is_3d(Audio& audio, int bank, int cue) {
    const Cue* c = find_cue(audio, bank, cue);
    return c && c->is3d;
}

void stop_sound(Audio& audio, int bank, int cue) {
    Cue* c = find_cue(audio, bank, cue);
    if (!c) {
        return;
    }
    for (int i = 0; i < static_cast<int>(c->views.size()); ++i) {
        if (live(*c, i)) {
            trace(audio, "stop", bank, cue, i, nullptr);
        }
        stop_view(audio, *c, i);
    }
    send_command(audio, voice_command(CommandKind::stop_cue, bank, cue, -1));
}

void set_sound_trace_step(Audio& audio, long long step) {
    audio.trace_step = step;
}

void stop_sound_voice(Audio& audio, int bank, int cue, int voice) {
    Cue* c = find_cue(audio, bank, cue);
    if (!c || voice < 0 || voice >= static_cast<int>(c->views.size())) {
        return;
    }
    if (live(*c, voice)) {
        trace(audio, "stop", bank, cue, voice, nullptr);
    }
    stop_view(audio, *c, voice);
    send_command(audio, voice_command(CommandKind::stop_voice, bank, cue, voice));
}

void stop_all_sounds(Audio& audio) {
    for (Bank* bank : audio.banks) {
        for (std::size_t c = 0; bank && c < bank->cues.size(); ++c) {
            for (int i = 0; i < static_cast<int>(bank->cues[c].views.size()); ++i) {
                stop_view(audio, bank->cues[c], i);
            }
        }
    }
    send_command(audio, voice_command(CommandKind::stop_all, 0, 0, 0));
}

void set_sound_pitch(Audio& audio, int bank, int cue, int voice, int pitch) {
    Cue* c = find_cue(audio, bank, cue);
    if (c && !audio.banks[bank]->muted && voice >= 0 && voice < static_cast<int>(c->views.size())) {
        const int kept = static_cast<short>(pitch);
        c->views[static_cast<std::size_t>(voice)].pitch = kept;
        Command command = voice_command(CommandKind::pitch, bank, cue, voice);
        command.i0 = kept;
        send_command(audio, command);
    }
}

void set_sound_fixed_point(Audio& audio, const float* point) {
    audio.fixed_point = point != nullptr;
    for (int i = 0; i < 3 && point; ++i) {
        audio.fixed_place[i] = point[i];
    }
}

void set_sound_position(Audio& audio, int bank, int cue, int voice, const float position[3]) {
    Cue* c = find_cue(audio, bank, cue);
    if (!c || audio.banks[bank]->muted || voice >= static_cast<int>(c->views.size()) ||
        audio.fixed_point) {
        return;
    }
    for (std::size_t i = 0; i < c->views.size(); ++i) {
        if (voice < 0 || static_cast<int>(i) == voice) {
            std::copy(position, position + 3, c->views[i].place);
        }
    }
    Command command = voice_command(CommandKind::place, bank, cue, voice);
    std::copy(position, position + 3, command.f);
    send_command(audio, command);
}

void pause_sounds(Audio& audio, int mode) {
    // func_000E18C0, per voice (the mix does the same to its own, audio_mix.cpp)
    for (Bank* bank : audio.banks) {
        for (std::size_t c = 0; bank && c < bank->cues.size(); ++c) {
            Cue& cue = bank->cues[c];
            for (int i = 0; i < static_cast<int>(cue.views.size()); ++i) {
                VoiceView& view = cue.views[static_cast<std::size_t>(i)];
                if (view.paused != 0) {
                    if (mode == 1 || mode == 2) {
                        view.paused = 0;
                    }
                } else if ((mode == 0 || mode == 2) && live(cue, i)) {
                    view.paused = view.looping ? 2 : 1;
                }
            }
        }
    }
    Command command = voice_command(CommandKind::pause, 0, 0, 0);
    command.i0 = mode;
    send_command(audio, command);
}

void set_sound_level(Audio& audio, int level) {
    Command command = voice_command(CommandKind::sound_level, 0, 0, 0);
    command.i0 = std::clamp(level, 0, 255);
    send_command(audio, command);
}

void set_listener(Audio& audio, const float position[3], const float forward[3],
                  const float up[3]) {
    Listener l;
    for (int i = 0; i < 3; ++i) {
        l.position[i] = position[i];
        l.forward[i] = forward[i];
        l.up[i] = up[i];
    }
    // right = forward x up in this right-handed frame; then the three made unit length
    // (func_000DDD70 builds the same basis for DirectSound's left-handed one)
    l.right[0] = forward[1] * up[2] - forward[2] * up[1];
    l.right[1] = forward[2] * up[0] - forward[0] * up[2];
    l.right[2] = forward[0] * up[1] - forward[1] * up[0];
    for (float* v : {l.forward, l.up, l.right}) {
        const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (length > 1e-6f) {
            v[0] /= length;
            v[1] /= length;
            v[2] /= length;
        }
    }
    audio.listener = l;
    Command command = voice_command(CommandKind::listener, 0, 0, 0);
    command.listener = l;
    send_command(audio, command);
}

void set_reverb(Audio& audio, const ReverbPreset& preset) {
    Command command = voice_command(CommandKind::reverb, 0, 0, 0);
    command.preset = preset;
    send_command(audio, command);
}
