// The streamed audio: the music WAVs (docs/port/data.md section 20) and the Sofdec movies with
// their ADX sound (section 21).
#include "addresses.hpp"
#include "audio.hpp"
#include "disc.hpp"
#include "extract.hpp"
#include "sexp_out.hpp"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace {

std::uint32_t le32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    std::uint32_t value = 0;
    if (at + 4 <= bytes.size()) {
        std::memcpy(&value, bytes.data() + at, 4);
    }
    return value;
}

std::string upper(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

// "12SENTOU.WAV" -> "12SENTOU"
std::string stem_of(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// the files of a disc folder by upper-case name, to their disc paths: a table name and the
// disc's file may differ in case (13BossStart.wav), which the Xbox's file system ignores
std::map<std::string, std::string> folder_by_name(const Disc& disc, const std::string& folder) {
    std::map<std::string, std::string> files;
    for (const std::string& name : disc_list(disc.source, folder)) {
        files[upper(name)] = folder + "/" + name;
    }
    return files;
}

// authoring markers ("cue " points and their LIST adtl labels): kept for reference, the game
// ignores them (func_000E1200 skips both chunks)
std::string wav_markers(const std::vector<std::uint8_t>& bytes,
                        const std::vector<RiffChunk>& chunks) {
    const RiffChunk* cue = find_chunk(chunks, "cue ");
    if (!cue) {
        return "";
    }
    std::map<std::uint32_t, std::string> labels;
    if (const RiffChunk* list = find_chunk(chunks, "LIST")) {
        std::size_t at = list->offset + 4; // after "adtl"
        const std::size_t end = list->offset + list->size;
        while (at + 12 <= end && at + 12 <= bytes.size()) {
            const std::uint32_t size = le32(bytes, at + 4);
            if (std::memcmp(&bytes[at], "labl", 4) == 0 && at + 8 + size <= bytes.size()) {
                const char* text = reinterpret_cast<const char*>(&bytes[at + 12]);
                labels[le32(bytes, at + 8)] = std::string(text, strnlen(text, size - 4));
            }
            at += 8 + size + (size & 1);
        }
    }
    std::string out = "\n  (markers";
    const std::uint32_t points = le32(bytes, cue->offset);
    for (std::uint32_t i = 0; i < points && cue->offset + 4 + (i + 1) * 24 <= bytes.size(); ++i) {
        const std::size_t at = cue->offset + 4 + i * 24;
        const std::uint32_t id = le32(bytes, at);
        out += " (marker (id " + sx_int(id) + ") (frame " + sx_int(le32(bytes, at + 20)) + ")";
        if (labels.count(id)) {
            out += " (label " + sx_string(labels[id]) + ")";
        }
        out += ")";
    }
    return out + ")";
}

} // namespace

bool extract_music(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    const std::map<std::string, std::string> on_disc = folder_by_name(disc, "Media/sound");
    std::error_code made;
    fs::create_directories(options.out_dir + "/audio/music", made);

    // who plays each row: the Sonic Amp catalogue's wave and the per-state table
    std::multimap<int, int> catalogue_rows, state_rows;
    for (int i = 0; i < xbe_at::music_catalogue_count; ++i) {
        catalogue_rows.insert(
            {xbe_i32(disc.xbe, xbe_at::music_sonic_amp_catalogue + i * xbe_at::music_record_stride),
             i});
    }
    for (int i = 0; i < xbe_at::state_music_count; ++i) {
        state_rows.insert({xbe_i32(disc.xbe, xbe_at::state_music + i * 4), i});
    }

    std::string text = ";; music_files 0x254398: the 202 names the sound init opens at boot, by "
                       "row (the track number every music table uses); written by pc_extract "
                       "(tools/extract/audio_streams.cpp), see docs/audio-assets.md\n";
    int found = 0, missing = 0;
    for (int row = 0; row < xbe_at::music_file_count; ++row) {
        const std::string name =
            xbe_cstring(disc.xbe, xbe_u32(disc.xbe, xbe_at::music_files + row * 4));
        text += "(track (index " + sx_int(row) + ") (name " + sx_string(name) + ")";
        for (auto [at, end] = catalogue_rows.equal_range(row); at != end; ++at) {
            text += " (catalogue " + sx_int(at->second) + ")";
        }
        for (auto [at, end] = state_rows.equal_range(row); at != end; ++at) {
            text += " (state " + sx_int(at->second) + ")";
        }
        const auto file = on_disc.find(upper(name));
        if (file == on_disc.end()) {
            // the init's open fails and the row stays silent
            text += " (missing))\n";
            ++missing;
            continue;
        }

        // the stream reader (func_000E1200): fmt and data found in the first 0x200 bytes, data
        // runs to the end of the file whatever its chunk says
        const std::vector<std::uint8_t> bytes = disc_file(disc.source, file->second);
        const std::vector<RiffChunk> chunks = riff_chunks(bytes.data(), bytes.size());
        const RiffChunk* fmt = find_chunk(chunks, "fmt ");
        const RiffChunk* data = find_chunk(chunks, "data");
        if (!fmt || !data || data->offset > 0x200 || std::memcmp(bytes.data(), "RIFF", 4) != 0) {
            std::fprintf(stderr, "audio: %s is not a WAV the game can stream\n", name.c_str());
            return false;
        }
        const int format = bytes[fmt->offset] | bytes[fmt->offset + 1] << 8;
        const int channels = bytes[fmt->offset + 2] | bytes[fmt->offset + 3] << 8;
        const int rate = static_cast<int>(le32(bytes, fmt->offset + 4));
        const int bits = bytes[fmt->offset + 14] | bytes[fmt->offset + 15] << 8;
        if (format != 1 || bits != 16 || channels < 1) {
            std::fprintf(stderr, "audio: %s is not PCM16\n", name.c_str());
            return false;
        }
        const std::size_t data_bytes = bytes.size() - data->offset;
        const std::size_t frames = data_bytes / (2u * channels);
        std::vector<std::int16_t> pcm(frames * channels);
        for (std::size_t i = 0; i < pcm.size(); ++i) {
            const std::size_t at = data->offset + 2 * i;
            pcm[i] = static_cast<std::int16_t>(bytes[at] | bytes[at + 1] << 8);
        }
        const std::string wav = "audio/music/" + stem_of(name) + ".wav";
        if (!write_wav(options.out_dir + "/" + wav, rate, channels, pcm)) {
            std::fprintf(stderr, "audio: can't write %s\n", wav.c_str());
            return false;
        }
        ++found;
        // with loop flag 1 the stream restarts at the data start: the whole file loops
        text += "\n  (file " + sx_string(wav) + ") (rate " + sx_int(rate) + ") (channels " +
                sx_int(channels) + ") (frames " + sx_int(static_cast<long long>(frames)) +
                ") (seconds " + sx_decimal(static_cast<double>(frames) / rate, 3) +
                ")\n  (loop (start 0) (end " + sx_int(static_cast<long long>(frames)) + ")) " +
                sx_levels(measure_levels(pcm));
        if (data_bytes != data->size || data_bytes % (2u * channels) != 0) {
            text += " (data-chunk-bytes " + sx_int(data->size) + ") (played-bytes " +
                    sx_int(static_cast<long long>(data_bytes)) + ")";
        }
        text += wav_markers(bytes, chunks) + ")\n";
        manifest.entries.push_back("(music (index " + sx_int(row) + ") (source " +
                                   sx_string(file->second) +
                                   ") (file " + sx_string(wav) + "))");
    }
    std::ofstream out(options.out_dir + "/audio/music.sexp", std::ios::binary);
    out << text;
    if (!out) {
        return false;
    }
    manifest.entries.push_back("(music-table (table " + sx_hex(xbe_at::music_files) +
                               ") (file \"audio/music.sexp\"))");
    std::printf("audio: %d music files of %d table rows (%d rows have no file)\n", found,
                xbe_at::music_file_count, missing);
    return true;
}

bool extract_movies(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    const std::map<std::string, std::string> on_disc = folder_by_name(disc, "Media/movie");
    std::error_code made;
    fs::create_directories(options.out_dir + "/movies", made);
    fs::create_directories(options.out_dir + "/audio/adx", made);

    std::string text = ";; movie_files 0x25943C: the Sofdec movies (MPEG-1 video, ADX sound) by "
                       "movie number; written by pc_extract (tools/extract/audio_streams.cpp), "
                       "see docs/audio-assets.md\n";
    int copied = 0, decoded = 0;
    for (int index = 0; index < xbe_at::movie_count; ++index) {
        // "d:\media\movie\opening.sfd" -> "opening.sfd"
        const std::string path =
            xbe_cstring(disc.xbe, xbe_u32(disc.xbe, xbe_at::movie_files + index * 4));
        const std::string name = path.substr(path.find_last_of('\\') + 1);
        const auto file = on_disc.find(upper(name));
        if (file == on_disc.end()) {
            std::fprintf(stderr, "audio: movie %s is not on the disc\n", name.c_str());
            text +=
                "(movie (index " + sx_int(index) + ") (name " + sx_string(name) + ") (missing))\n";
            continue;
        }

        // the movie as it is
        const std::string sfd = "movies/" + name;
        const std::vector<std::uint8_t> bytes = disc_file(disc.source, file->second);
        std::ofstream copy(options.out_dir + "/" + sfd, std::ios::binary);
        copy.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (bytes.empty() || !copy) {
            std::fprintf(stderr, "audio: can't copy %s\n", name.c_str());
            return false;
        }
        ++copied;
        text += "(movie (index " + sx_int(index) + ") (name " + sx_string(name) + ") (file " +
                sx_string(sfd) + ")";

        // its sound: stream 0xC0 of the program stream is one ADX file
        const std::vector<std::uint8_t> adx = demux_program_stream(bytes, 0xC0);
        AdxStream stream;
        std::string why;
        if (adx.empty() || !decode_adx(adx, stream, why)) {
            std::fprintf(stderr, "audio: %s: %s\n", name.c_str(),
                         adx.empty() ? "no ADX stream" : why.c_str());
            text += " (audio-unread " + sx_string(adx.empty() ? "no ADX stream" : why) + "))\n";
            continue;
        }
        const std::string wav = "audio/adx/" + stem_of(name) + ".wav";
        if (!write_wav(options.out_dir + "/" + wav, stream.rate, stream.channels, stream.samples)) {
            std::fprintf(stderr, "audio: can't write %s\n", wav.c_str());
            return false;
        }
        ++decoded;
        text +=
            "\n  (audio (file " + sx_string(wav) + ") (format adx) (rate " + sx_int(stream.rate) +
            ") (channels " + sx_int(stream.channels) + ") (frames " + sx_int(stream.total_frames) +
            ") (seconds " + sx_decimal(static_cast<double>(stream.total_frames) / stream.rate, 3) +
            ")\n    (adx-version " + sx_int(stream.version) + ") (cutoff " + sx_int(stream.cutoff) +
            ") (adx-bytes " + sx_int(static_cast<long long>(adx.size())) + ") ";
        if (stream.loops) {
            text += "(loop (start " + sx_int(stream.loop_start) + ") (end " +
                    sx_int(stream.loop_end) + ")) ";
        } else {
            text += "(loop none) ";
        }
        text += sx_levels(measure_levels(stream.samples)) + "))\n";
        manifest.entries.push_back("(movie (index " + sx_int(index) + ") (source " +
                                   sx_string(file->second) +
                                   ") (file " + sx_string(sfd) + ") (audio " + sx_string(wav) +
                                   "))");
    }
    std::ofstream out(options.out_dir + "/audio/movies.sexp", std::ios::binary);
    out << text;
    if (!out) {
        return false;
    }
    std::printf("audio: %d movies copied, %d ADX tracks decoded\n", copied, decoded);
    return true;
}
