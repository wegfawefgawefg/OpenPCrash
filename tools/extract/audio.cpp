#include "audio.hpp"

#include "addresses.hpp"
#include "audio_names.hpp"
#include "disc.hpp"
#include "extract.hpp"
#include "sexp_out.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

// game_state_task (decomp src/od/u_06c3e0.c) loads the four banks once at boot with
// se_bank_load_resource(asset, bank, ..., 1.0f): sys is bank 0, battle 1, battle_old 2,
// battle_player 3, every bank at volume 1
struct BankFile {
    int asset;
    int bank;
};
const BankFile bank_files[] = {{0x783, 0}, {0x780, 1}, {0x781, 2}, {0x782, 3}};
constexpr int dsp_image_asset = 1924; // dsstdfx.bin

std::uint32_t le32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    std::uint32_t value = 0;
    if (at + 4 <= bytes.size()) {
        std::memcpy(&value, bytes.data() + at, 4);
    }
    return value;
}

std::uint16_t le16(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return static_cast<std::uint16_t>(at + 2 <= bytes.size() ? bytes[at] | bytes[at + 1] << 8 : 0);
}

// "D:\Media\sound\battle_old.bin" -> "battle_old"
std::string path_stem(const std::string& path) {
    const std::size_t slash = path.find_last_of('\\');
    std::string stem = path.substr(slash == std::string::npos ? 0 : slash + 1);
    const std::size_t dot = stem.rfind('.');
    return dot == std::string::npos ? stem : stem.substr(0, dot);
}

bool write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    return static_cast<bool>(out);
}

struct BankCounts {
    int cues = 0;
    int failed = 0;
};

// One SE bank (docs/port/data.md section 18): "SE BIN  ", u32 cue count, then 16 bytes a cue
// {u8 is3d, u8 0, u8 voice count, u8 0, buffer-state offset, spatial offset, wave offset}; each
// wave a RIFF of Xbox IMA ADPCM. The loader (se_bank_load 0xDDF00) makes `voices` DirectSound
// buffers over the same wave, so one WAV a cue covers every voice.
bool extract_bank(const Disc& disc, const ExtractOptions& options, const BankFile& file,
                  Manifest& manifest, BankCounts& counts) {
    const std::vector<std::uint8_t> bytes = read_asset(disc, file.asset);
    const std::string name = path_stem(disc.assets[file.asset].path);
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "SE BIN  ", 8) != 0) {
        std::fprintf(stderr, "audio: asset %d is not an SE bank\n", file.asset);
        ++counts.failed;
        return true;
    }
    const std::uint32_t count = le32(bytes, 8);
    const std::string dir = "audio/se/" + name;
    std::error_code made;
    std::filesystem::create_directories(options.out_dir + "/" + dir, made);

    std::string table = ";; " + name + ".bin, SE bank " + std::to_string(file.bank) +
                        ": written by pc_extract (tools/extract/audio.cpp), see "
                        "docs/audio-assets.md\n";
    table += "(se-bank (name " + sx_string(name) + ") (bank " + sx_int(file.bank) + ") (asset " +
             sx_int(file.asset) + ") (source " + sx_string(asset_source(disc, file.asset)) +
             ") (volume 1) (cues " + sx_int(count) + ")";
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t entry = 12 + static_cast<std::size_t>(i) * 16;
        const int is3d = bytes[entry];
        const int voices = bytes[entry + 2];
        const std::uint32_t wave = le32(bytes, entry + 12);
        // a wave ends where the next starts (offsets rise), the last at the end of the bank
        std::uint32_t end = static_cast<std::uint32_t>(bytes.size());
        if (i + 1 < count && le32(bytes, entry + 16 + 12) > wave) {
            end = le32(bytes, entry + 16 + 12);
        }
        if (wave + 12 > end || std::memcmp(&bytes[wave], "RIFF", 4) != 0) {
            std::fprintf(stderr, "audio: %s cue %u has no RIFF\n", name.c_str(), i);
            ++counts.failed;
            continue;
        }
        const std::vector<RiffChunk> chunks = riff_chunks(&bytes[wave], end - wave);
        const RiffChunk* fmt = find_chunk(chunks, "fmt ");
        const RiffChunk* data = find_chunk(chunks, "data");
        const RiffChunk* fact = find_chunk(chunks, "fact");
        if (!fmt || !data || wave + data->offset + data->size > end) {
            std::fprintf(stderr, "audio: %s cue %u has no fmt or data\n", name.c_str(), i);
            ++counts.failed;
            continue;
        }
        const std::size_t f = wave + fmt->offset;
        const int format = le16(bytes, f);
        const int channels = le16(bytes, f + 2);
        const int rate = static_cast<int>(le32(bytes, f + 4));
        const int block_align = le16(bytes, f + 12);
        if (format != 0x69 || channels < 1 || channels > 2 || block_align != 36 * channels) {
            std::fprintf(stderr, "audio: %s cue %u: format 0x%x, %d channels, not Xbox IMA\n",
                         name.c_str(), i, format, channels);
            ++counts.failed;
            continue;
        }
        // a fact chunk smaller than the blocks hold trims the last block
        const std::size_t capacity = data->size / (36u * channels) * 64;
        const std::size_t fact_frames = fact ? le32(bytes, wave + fact->offset) : capacity;
        const std::size_t frames = fact_frames < capacity ? fact_frames : capacity;
        const std::vector<std::int16_t> pcm =
            decode_xbox_ima(&bytes[wave + data->offset], data->size, channels, frames);

        const SeCueName* named = se_cue_name(file.bank, static_cast<int>(i));
        const char* cue_name = named ? named->name : nullptr;
        char stem[96];
        std::snprintf(stem, sizeof stem, "%02u%s%s.wav", i, cue_name ? "_" : "",
                      cue_name ? cue_name : "");
        const std::string wav = dir + "/" + stem;
        if (!write_wav(options.out_dir + "/" + wav, rate, channels, pcm)) {
            std::fprintf(stderr, "audio: can't write %s\n", wav.c_str());
            return false;
        }
        ++counts.cues;

        // the RIFF size field: most cues store the whole span, some the standard span - 8
        // (the game never reads it, docs/port/data.md section 18)
        const std::uint32_t riff_size = le32(bytes, wave + 4);
        const char* riff_kind = riff_size == end - wave       ? "span"
                                : riff_size + 8 == end - wave ? "standard"
                                                              : "other";
        table += "\n  (cue (index " + sx_int(i) + ")";
        if (cue_name) {
            table += " (name " + sx_string(cue_name) + ")";
        }
        table += " (file " + sx_string(wav) + ") (rate " + sx_int(rate) + ") (channels " +
                 sx_int(channels) + ") (frames " + sx_int(static_cast<long long>(frames)) +
                 ") (seconds " + sx_decimal(static_cast<double>(frames) / rate, 3) + ")\n" +
                 "    (is3d " + sx_int(is3d) + ") (voices " + sx_int(voices) +
                 ") (format xbox-ima-adpcm) (disc-bytes " + sx_int(data->size) + ") (riff-size " +
                 riff_kind + ")";
        if (fact) {
            table += " (fact " + sx_int(static_cast<long long>(fact_frames)) + ")";
        }
        if (named && named->looped) {
            table += " (looped-by-caller)";
        }
        table += " " + sx_levels(measure_levels(pcm)) + ")";
    }
    table += ")\n";
    if (!write_text(options.out_dir + "/audio/se/" + name + ".sexp", table)) {
        return false;
    }
    manifest.entries.push_back(
        "(se-bank (asset " + sx_int(file.asset) + ") (source " +
        sx_string(asset_source(disc, file.asset)) + ") (bank " + sx_int(file.bank) + ") (table " +
        sx_string("audio/se/" + name + ".sexp") + ") (dir " + sx_string(dir) + "))");
    return true;
}

// DirectSound's I3DL2 listener (DSI3DL2LISTENER), the decomp's ReverbPreset
struct Reverb {
    int room, room_hf;
    float room_rolloff, decay_time, decay_hf_ratio;
    int reflections;
    float reflections_delay;
    int reverb;
    float reverb_delay, diffusion, density, hf_reference;
};

Reverb read_reverb(const Xbe& xbe, std::uint32_t va) {
    return {xbe_i32(xbe, va),      xbe_i32(xbe, va + 4),  xbe_f32(xbe, va + 8),
            xbe_f32(xbe, va + 12), xbe_f32(xbe, va + 16), xbe_i32(xbe, va + 20),
            xbe_f32(xbe, va + 24), xbe_i32(xbe, va + 28), xbe_f32(xbe, va + 32),
            xbe_f32(xbe, va + 36), xbe_f32(xbe, va + 40), xbe_f32(xbe, va + 44)};
}

std::string reverb_sexp(const char* name, const std::string& source, const Reverb& r) {
    return "(reverb-preset (name " + std::string(name) + ") (source " + sx_string(source) +
           ")\n  (room " + sx_int(r.room) + ") (room-hf " + sx_int(r.room_hf) + ") (room-rolloff " +
           sx_float(r.room_rolloff) + ") (decay-time " + sx_float(r.decay_time) +
           ") (decay-hf-ratio " + sx_float(r.decay_hf_ratio) + ")\n  (reflections " +
           sx_int(r.reflections) + ") (reflections-delay " + sx_float(r.reflections_delay) +
           ") (reverb " + sx_int(r.reverb) + ") (reverb-delay " + sx_float(r.reverb_delay) +
           ")\n  (diffusion " + sx_float(r.diffusion) + ") (density " + sx_float(r.density) +
           ") (hf-reference " + sx_float(r.hf_reference) + "))\n";
}

// The stage reverb (decomp findings "Stage ambience and reverb", src/od/u_072650.c): presets the
// XBE holds as tables, two the code builds field by field, and which one each stage selector
// sets, read from the enter functions table stage_ambience_enters 0x22E840.
bool extract_reverb(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    // built in aqua_ambience_enter 0x74020: the I3DL2 Mountains values
    const Reverb aqualine = {-1000, -2500, 0.0f, 1.49f, 0.21f,  -2780,
                             0.3f,  -2014, 0.1f, 27.0f, 100.0f, 5000.0f};
    // built in every stage exit (city_ambience_exit 0x73AD0 and the rest): reverb off
    const Reverb off = {-10000, -10000, 0.0f, 1.0f, 1.0f, -10000,
                        0.0f,   -10000, 0.0f, 0.0f, 0.0f, 5000.0f};
    std::string text = ";; I3DL2 listener reverb per stage: written by pc_extract "
                       "(tools/extract/audio.cpp), see docs/audio-assets.md\n";
    text += reverb_sexp("city",
                        "reverb_city 0x22E8A0 (city_ambience_enter 0x736BF and "
                        "stage_ambience_enter_67 0x742A8 build the same values in code)",
                        read_reverb(disc.xbe, xbe_at::reverb_city));
    text += reverb_sexp("room", "reverb_room 0x22E8D0", read_reverb(disc.xbe, xbe_at::reverb_room));
    text += reverb_sexp("room-2", "reverb_room_2 0x22E900",
                        read_reverb(disc.xbe, xbe_at::reverb_room_2));
    text += reverb_sexp("aqualine", "built in code, aqua_ambience_enter 0x74020", aqualine);
    text +=
        reverb_sexp("off", "built in code by every stage exit (city_ambience_exit 0x73AD0)", off);

    // selector = stage * 2 + night; the enter function decides the preset
    struct Enter {
        std::uint32_t function;
        const char* stage;
        const char* preset;
    };
    const Enter enters[] = {
        {0x736BF, "city_ambience_enter", "city"},
        {0x73B8E, "shibuya_ambience_enter", "city"},
        {0x74020, "aqua_ambience_enter", "aqualine"},
        {0x742A8, "stage_ambience_enter_67", "city"},
    };
    for (int selector = 0; selector < 8; ++selector) {
        const std::uint32_t function =
            xbe_u32(disc.xbe, xbe_at::stage_ambience_enters + selector * 4);
        const Enter* found = nullptr;
        for (const Enter& enter : enters) {
            found = enter.function == function ? &enter : found;
        }
        if (!found) {
            std::fprintf(stderr, "audio: stage selector %d enters at 0x%x, not a known ambience\n",
                         selector, function);
            return false;
        }
        text += "(stage-reverb (selector " + sx_int(selector) + ") (enter " + found->stage +
                ") (preset " + found->preset + ") (exit off)";
        if (function == 0x73B8E) {
            // shibuya_ambience_update: by shibuya_zone_at of the first human fighter
            text += "\n  (area 0 room) (area 3 room) (area 2 room-2) (area 4 room-2) (area 1 city)";
        }
        text += ")\n";
    }
    if (!write_text(options.out_dir + "/audio/reverb.sexp", text)) {
        return false;
    }
    manifest.entries.push_back("(reverb (table " + sx_hex(xbe_at::reverb_city) +
                               ") (file \"audio/reverb.sexp\"))");
    return true;
}

// The volume curve every level goes through (decomp docs/port/audio.md, "Volumes and the mix"):
// volume_millibels 0xDD2D0 reads the 256 shorts at 0x410318, -9600 at level 0 up to 0 at 255.
// The sound engine reads it back as (volume-millibels v0 .. v255).
bool extract_volume(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    std::string text = ";; volume levels 0..255 to millibels: the short[256] at 0x410318 "
                       "(volume_millibels 0xDD2D0), written by pc_extract "
                       "(tools/extract/audio.cpp), see docs/audio-assets.md\n(volume-millibels";
    for (int level = 0; level < xbe_at::volume_level_count; ++level) {
        text += level % 16 == 0 ? "\n " : "";
        text += " " + sx_int(xbe_i16(disc.xbe, xbe_at::volume_millibels + level * 2));
    }
    text += ")\n";
    if (!write_text(options.out_dir + "/audio/volume.sexp", text)) {
        return false;
    }
    manifest.entries.push_back("(volume (table " + sx_hex(xbe_at::volume_millibels) +
                               ") (file \"audio/volume.sexp\"))");
    return true;
}

} // namespace

bool extract_audio(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    std::error_code made;
    std::filesystem::create_directories(options.out_dir + "/audio/se", made);

    // the four SE banks
    BankCounts counts;
    for (const BankFile& file : bank_files) {
        if (asset_wanted(options, file.asset) &&
            !extract_bank(disc, options, file, manifest, counts)) {
            return false;
        }
    }
    std::printf("audio: %d SE cues decoded, %d unread\n", counts.cues, counts.failed);

    // the DSP image is the APU's reverb program: nothing to convert, reverb.sexp replaces it
    if (asset_wanted(options, dsp_image_asset)) {
        manifest.entries.push_back(
            "(dsp-image (asset " + sx_int(dsp_image_asset) + ") (source " +
            sx_string(asset_source(disc, dsp_image_asset)) +
            ") (not-converted \"Xbox APU DSP microcode: I3DL2 reverb and crosstalk\") "
            "(replaced-by \"audio/reverb.sexp\"))");
    }
    if (options.only_assets.empty() &&
        !(extract_reverb(disc, options, manifest) && extract_volume(disc, options, manifest))) {
        return false;
    }

    // the streams: they are not in the asset registry, so --assets skips them
    if (!options.only_assets.empty()) {
        return true;
    }
    return extract_music(disc, options, manifest) && extract_movies(disc, options, manifest);
}
