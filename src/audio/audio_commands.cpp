// The commands from the main thread to the mix (audio_state.hpp): sent through the ring, run by
// the mix at the top of every ask on its own state (the voices, the ghosts, the listener, the
// reverb, the streams' gains). Retail queues the same work (sound_command_push) and runs
// it once a frame (sound_commands_run).
#include "audio/audio_state.hpp"

#include <algorithm>
#include <cmath>

namespace {

// a stream cut at once still fades this long, so it doesn't click
constexpr float declick_seconds = 0.004f;

// a voice cut off (restarted or stopped) fades out as a ghost
void ghost_voice(Audio& audio, const Cue& cue, Voice& voice) {
    if (!voice.gains_known || !cue.has_wave || voice.paused != 0) {
        return;
    }
    // the quietest ghost slot (or a free one) takes it
    Ghost* slot = &audio.mix.ghosts[0];
    for (Ghost& ghost : audio.mix.ghosts) {
        if (!ghost.sample) {
            slot = &ghost;
            break;
        }
        if (ghost.fade < slot->fade) {
            slot = &ghost;
        }
    }
    slot->sample = &cue.sample;
    slot->position = voice.position;
    slot->step = cue.sample.rate_ratio * std::exp2(voice.pitch / 4096.0);
    slot->looping = voice.looping;
    slot->gain_left = voice.gain_left;
    slot->gain_right = voice.gain_right;
    slot->send = voice.send;
    slot->fade = 1.0f;
}

void cut_voice(Audio& audio, const Cue& cue, Voice& voice) {
    if (voice.playing) {
        ghost_voice(audio, cue, voice);
    }
    voice.playing = false;
    voice.paused = 0;
}

// the mix's cue for a command, null when its bank isn't there (the main thread checked the rest)
Cue* mix_cue(Audio& audio, const Command& c) {
    if (c.bank < 0 || c.bank >= sound_bank_slots || !audio.mix.banks[c.bank]) {
        return nullptr;
    }
    Bank& bank = *audio.mix.banks[c.bank];
    if (c.cue < 0 || c.cue >= static_cast<int>(bank.cues.size())) {
        return nullptr;
    }
    return &bank.cues[static_cast<std::size_t>(c.cue)];
}

Voice* mix_voice_of(Audio& audio, const Command& c, Cue*& cue) {
    cue = mix_cue(audio, c);
    if (!cue || c.voice < 0 || c.voice >= static_cast<int>(cue->voices.size())) {
        return nullptr;
    }
    return &cue->voices[static_cast<std::size_t>(c.voice)];
}

void stream_fade_out(Stream& stream, float fade_seconds) {
    const float seconds = std::max(fade_seconds, declick_seconds);
    stream.fade_step = std::max(stream.gain, 1e-4f) / (seconds * audio_rate);
    stream.target = 0.0f;
    stream.stop_at_silence = true;
}

// what the main thread said, done to the mix's own state
void run_command(Audio& audio, const Command& c) {
    Mixer& mix = audio.mix;
    Cue* cue = nullptr;
    switch (c.kind) {
    case CommandKind::bank: {
        Bank* old = mix.banks[c.bank];
        if (old) {
            // ghosts still reading its waves go with it
            for (Ghost& ghost : mix.ghosts) {
                for (const Cue& gone : old->cues) {
                    if (ghost.sample == &gone.sample) {
                        ghost.sample = nullptr;
                    }
                }
            }
            (void)audio.released.push(old); // full: freed at the end instead
        }
        mix.banks[c.bank] = c.bank_ptr;
        break;
    }
    case CommandKind::start:
        if (Voice* voice = mix_voice_of(audio, c, cue)) {
            // from the front: SetCurrentPosition 0, then Play
            if (voice->playing) {
                ghost_voice(audio, *cue, *voice);
            }
            voice->playing = cue->has_wave;
            voice->looping = c.i0 != 0;
            voice->paused = 0;
            voice->position = 0.0;
            voice->gains_known = false;
            voice->serial = c.serial;
            std::copy(c.f, c.f + 3, voice->place);
        }
        break;
    case CommandKind::stop_voice:
        if (Voice* voice = mix_voice_of(audio, c, cue)) {
            cut_voice(audio, *cue, *voice);
        }
        break;
    case CommandKind::stop_cue:
        if ((cue = mix_cue(audio, c)) != nullptr) {
            for (Voice& voice : cue->voices) {
                cut_voice(audio, *cue, voice);
            }
        }
        break;
    case CommandKind::stop_all:
        // se_voices_stop_all: StopEx through the release envelope, which the game never sets (so
        // it ends at once); the ghosts' few milliseconds stand in for it
        for (Bank* bank : mix.banks) {
            for (std::size_t i = 0; bank && i < bank->cues.size(); ++i) {
                for (Voice& voice : bank->cues[i].voices) {
                    cut_voice(audio, bank->cues[i], voice);
                }
            }
        }
        break;
    case CommandKind::pitch:
        if (Voice* voice = mix_voice_of(audio, c, cue)) {
            voice->pitch = c.i0;
        }
        break;
    case CommandKind::place:
        if ((cue = mix_cue(audio, c)) != nullptr) {
            for (std::size_t i = 0; i < cue->voices.size(); ++i) {
                if (c.voice < 0 || static_cast<int>(i) == c.voice) {
                    std::copy(c.f, c.f + 3, cue->voices[i].place);
                }
            }
        }
        break;
    case CommandKind::pause:
        // func_000E18C0, per voice: a paused one resumes on 1 or 2 where it stopped; a playing
        // one pauses on 0 or 2 (remembering whether it looped); one that's done is just stopped
        for (Bank* bank : mix.banks) {
            for (std::size_t i = 0; bank && i < bank->cues.size(); ++i) {
                for (Voice& voice : bank->cues[i].voices) {
                    if (voice.paused != 0) {
                        if (c.i0 == 1 || c.i0 == 2) {
                            voice.paused = 0;
                            voice.playing = true;
                        }
                    } else if ((c.i0 == 0 || c.i0 == 2) && voice.playing) {
                        voice.paused = voice.looping ? 2 : 1;
                    }
                }
            }
        }
        break;
    case CommandKind::sound_level:
        mix.sound_level = c.i0;
        break;
    case CommandKind::pc_volumes:
        mix.pc_master = c.f[0];
        mix.pc_music = c.f[1];
        mix.pc_effects = c.f[2];
        break;
    case CommandKind::listener:
        mix.listener = c.listener;
        break;
    case CommandKind::reverb:
        set_reverb_preset(mix.reverb, c.preset);
        break;
    case CommandKind::stream_start: {
        // what the ring held before is another start's: dropped; then the reader may fill it
        Stream& stream = audio.streams[c.bank];
        stream.ring.taken.store(stream.ring.written.load(std::memory_order_acquire),
                                std::memory_order_release);
        stream.gen = c.serial;
        stream.heard.store(0, std::memory_order_relaxed);
        stream.target = c.f[0];
        stream.gain = c.f[1] > 0.0f ? 0.0f : c.f[0];
        stream.fade_step = c.f[1] > 0.0f ? c.f[0] / (c.f[1] * audio_rate) : 1.0f;
        stream.stop_at_silence = false;
        stream.paused = false;
        stream.on = true;
        stream.mix_gen.store(c.serial, std::memory_order_release);
        break;
    }
    case CommandKind::stream_fade:
        if (audio.streams[c.bank].on) {
            stream_fade_out(audio.streams[c.bank], c.f[0]);
        }
        break;
    case CommandKind::stream_cut: {
        Stream& stream = audio.streams[c.bank];
        stream.on = false;
        std::uint32_t mine = stream.gen;
        stream.live.compare_exchange_strong(mine, 0, std::memory_order_acq_rel);
        break;
    }
    case CommandKind::stream_gain:
        audio.streams[c.bank].target = audio.streams[c.bank].gain = c.f[0];
        audio.streams[c.bank].fade_step = 1.0f;
        break;
    case CommandKind::stream_pause: {
        Stream& stream = audio.streams[c.bank];
        stream.paused = c.i0 == 0 ? true : (c.i0 == 1 ? false : !stream.paused);
        break;
    }
    }
}

} // namespace

void run_commands(Audio& audio) {
    Command command;
    while (audio.commands.pop(command)) {
        run_command(audio, command);
    }
}

void send_command(Audio& audio, const Command& command) {
    if (audio.commands.push(command)) {
        return;
    }
    // the ring full: no device is asking (none open, or one stalled), so the main thread runs
    // what waits itself; the mix, should it ask now, waits only for this
    {
        const std::lock_guard<std::mutex> hold(audio.mix_lock);
        run_commands(audio);
    }
    audio.tally.overflows += 1;
    (void)audio.commands.push(command);
}

void free_released_banks(Audio& audio) {
    Bank* bank = nullptr;
    while (audio.released.pop(bank)) {
        std::erase_if(audio.loaded,
                      [bank](const std::unique_ptr<Bank>& b) { return b.get() == bank; });
    }
}
