// The engine's tables from data/audio (docs/audio.md, "Data"): the millibel table, the music file
// table and the SE banks, each a .sexp the extractor writes.
#include "audio/audio_state.hpp"
#include "audio/wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <gsexp/sexp.hpp>
#include <sstream>

namespace {

bool read_text(const std::string& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::stringstream all;
    all << in.rdbuf();
    text = all.str();
    return true;
}

// an atom as an integer, decimal or 0x hex
bool atom_int(gsexp::Node node, long& value) {
    if (!node.valid() || node.is_list()) {
        return false;
    }
    const std::string text(node.text());
    char* end = nullptr;
    value = std::strtol(text.c_str(), &end, 0);
    return end && *end == '\0' && !text.empty();
}

int form_int(const gsexp::FormView& form, std::string_view head, int otherwise) {
    long value = 0;
    return atom_int(form.find_arg(head, 0), value) ? static_cast<int>(value) : otherwise;
}

std::string form_text(const gsexp::FormView& form, std::string_view head) {
    const gsexp::Node node = form.find_arg(head, 0);
    return node.valid() && !node.is_list() ? std::string(node.text()) : std::string();
}

// every form headed by `head`, at the top or inside another (a bank's cues are inside its
// se-bank form)
template <typename Each> void visit_forms(gsexp::Node node, std::string_view head, Each& each) {
    if (!node.is_list()) {
        return;
    }
    if (gsexp::is_atom(node.head(), head)) {
        each(gsexp::FormView(node));
        return;
    }
    for (gsexp::Node child : node.children()) {
        visit_forms(child, head, each);
    }
}

// every form of a file headed by `head`, or false when the file can't be read
template <typename Each> bool each_form(const std::string& path, std::string_view head, Each each) {
    std::string text;
    if (!read_text(path, text)) {
        return false;
    }
    const gsexp::ParseResult parsed = gsexp::parse(text);
    if (!parsed.ok) {
        SDL_Log("audio: %s doesn't parse", path.c_str());
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        visit_forms(parsed.root(i), head, each);
    }
    return true;
}

} // namespace

void load_volume_table(Audio& audio) {
    // volume_millibels 0x410318 (-9600 at 0, 0 at 255), as the extractor copies it out of the XBE
    const std::string path = audio.data_dir + "/audio/volume.sexp";
    int count = 0;
    each_form(path, "volume-millibels", [&](const gsexp::FormView& form) {
        long value = 0;
        while (count < 256 && atom_int(form.arg(static_cast<std::size_t>(count)), value)) {
            audio.millibels[count++] = static_cast<short>(value);
        }
    });
    if (count == 256) {
        return;
    }
    // without it, a fit of the table's curve: within 20 mB above level 10, louder below (the
    // table bends toward -4845 at 1); docs/audio.md, "Volume"
    SDL_Log("audio: no %s, using an approximate volume curve", path.c_str());
    audio.millibels[0] = -9600;
    for (int i = 1; i < 256; ++i) {
        const double mb = 2280.0 * std::log10(i / 255.0);
        audio.millibels[i] = static_cast<short>(std::lround(mb < -4845.0 ? -4845.0 : mb));
    }
}

void load_music_table(Audio& audio) {
    // the 202 names music_catalogue_register hands the sound init, by track; 89 are on the disc
    audio.music_files.assign(music_table_rows, std::string());
    const std::string path = audio.data_dir + "/audio/music.sexp";
    int found = 0;
    const bool read = each_form(path, "track", [&](const gsexp::FormView& form) {
        const int track = form_int(form, "index", -1);
        const std::string file = form_text(form, "file");
        if (track >= 0 && track < music_table_rows && !file.empty()) {
            audio.music_files[static_cast<std::size_t>(track)] = audio.data_dir + "/" + file;
            ++found;
        }
    });
    if (!read) {
        SDL_Log("audio: no %s, no music", path.c_str());
    } else {
        SDL_Log("audio: %d music tracks", found);
    }
}

bool read_bank(const std::string& data_dir, const std::string& name, Bank& bank) {
    const std::string path = data_dir + "/audio/se/" + name + ".sexp";
    bank.name = name;
    bank.cues.clear();
    int waves = 0;
    const bool read = each_form(path, "cue", [&](const gsexp::FormView& form) {
        const int index = form_int(form, "index", -1);
        if (index < 0 || index > 4096) {
            return;
        }
        if (bank.cues.size() <= static_cast<std::size_t>(index)) {
            bank.cues.resize(static_cast<std::size_t>(index) + 1);
        }
        Cue& cue = bank.cues[static_cast<std::size_t>(index)];
        cue.name = form_text(form, "name");
        cue.is3d = form_int(form, "is3d", 0) != 0;
        // SoundVoice's voice count, a byte; 0 would make the round robin divide by zero in the
        // original, the extractor never writes it
        const std::size_t voices = static_cast<std::size_t>(std::max(1, form_int(form, "voices", 1)));
        cue.voices.assign(voices, Voice{});
        cue.views.assign(voices, VoiceView{});
        cue.ended = std::make_unique<std::atomic<std::uint32_t>[]>(voices);
        const std::string file = form_text(form, "file");
        WavInfo info;
        if (!file.empty() && read_wav(data_dir + "/" + file, info, cue.sample.pcm)) {
            cue.sample.channels = info.channels;
            cue.sample.frames = static_cast<int>(cue.sample.pcm.size()) / info.channels;
            // the original's rate when the extractor resampled; the WAV's own otherwise
            const int original_rate = form_int(form, "rate", info.rate);
            cue.sample.rate_ratio = static_cast<double>(info.rate) / std::max(1, original_rate);
            cue.has_wave = cue.sample.frames > 0;
            waves += cue.has_wave ? 1 : 0;
        } else if (!file.empty()) {
            SDL_Log("audio: %s cue %d: can't read %s", name.c_str(), index, file.c_str());
        }
    });
    if (!read) {
        SDL_Log("audio: no bank %s", path.c_str());
        return false;
    }
    // a cue the file skipped stays: silent, one voice
    for (Cue& cue : bank.cues) {
        if (cue.voices.empty()) {
            cue.voices.resize(1);
        }
    }
    SDL_Log("audio: bank %s, %d cues, %d waves", name.c_str(), static_cast<int>(bank.cues.size()),
            waves);
    return true;
}

void load_reverb_presets(Audio& audio) {
    // the I3DL2 listener presets the stages set (stage_ambience_enter and its kin), by name
    const std::string path = audio.data_dir + "/audio/reverb.sexp";
    each_form(path, "reverb-preset", [&](const gsexp::FormView& form) {
        ReverbPreset p;
        auto number = [&](std::string_view head, float otherwise) {
            return form.get_float(head).value_or(otherwise);
        };
        p.room = form_int(form, "room", p.room);
        p.room_hf = form_int(form, "room-hf", p.room_hf);
        p.room_rolloff = number("room-rolloff", p.room_rolloff);
        p.decay_time = number("decay-time", p.decay_time);
        p.decay_hf_ratio = number("decay-hf-ratio", p.decay_hf_ratio);
        p.reflections = form_int(form, "reflections", p.reflections);
        p.reflections_delay = number("reflections-delay", p.reflections_delay);
        p.reverb = form_int(form, "reverb", p.reverb);
        p.reverb_delay = number("reverb-delay", p.reverb_delay);
        p.diffusion = number("diffusion", p.diffusion);
        p.density = number("density", p.density);
        p.hf_reference = number("hf-reference", p.hf_reference);
        audio.reverb_presets.push_back({form_text(form, "name"), p});
    });
    SDL_Log("audio: %d reverb presets", static_cast<int>(audio.reverb_presets.size()));
}

bool find_reverb(const Audio& audio, std::string_view name, ReverbPreset& preset) {
    for (const auto& [named, p] : audio.reverb_presets) {
        if (named == name) {
            preset = p;
            return true;
        }
    }
    return false;
}

Cue* find_cue(Audio& audio, int bank, int cue) {
    if (bank < 0 || bank >= sound_bank_slots || !audio.banks[bank]) {
        return nullptr;
    }
    Bank& b = *audio.banks[bank];
    if (cue < 0 || cue >= static_cast<int>(b.cues.size())) {
        return nullptr;
    }
    return &b.cues[static_cast<std::size_t>(cue)];
}

SoundRef find_sound(const Audio& audio, std::string_view name) {
    for (int b = 0; b < sound_bank_slots; ++b) {
        const Bank* bank = audio.banks[b];
        for (std::size_t c = 0; bank && c < bank->cues.size(); ++c) {
            if (!name.empty() && bank->cues[c].name == name) {
                return {b, static_cast<int>(c)};
            }
        }
    }
    return {};
}
