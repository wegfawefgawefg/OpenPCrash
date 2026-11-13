#pragma once
// The audio codecs the disc uses, decoded to interleaved PCM16, and plain WAV out: Xbox IMA ADPCM
// (the SE banks, docs/port/data.md section 18), ADX (the movies' sound, from the public format
// description), an MPEG-1 program stream demuxer for the Sofdec movies, and a
// RIFF chunk walker. Plus levels, so a decode can be checked by numbers instead of by ear.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct RiffChunk {
    std::string id;         // "fmt ", "data", "cue "...
    std::size_t offset = 0; // of the chunk's payload, from the start of the RIFF
    std::uint32_t size = 0; // the payload's stated size
};

// every chunk after "RIFF size WAVE", up to end (the RIFF's own size field is not trusted)
std::vector<RiffChunk> riff_chunks(const std::uint8_t* riff, std::size_t end);
const RiffChunk* find_chunk(const std::vector<RiffChunk>& chunks, const char* id);

// Xbox IMA ADPCM: 36-byte blocks per channel of 64 frames (predictor, then 63 of the 64 nibbles);
// stereo interleaves 4 bytes per channel. Decodes whole blocks and stops at max_frames.
std::vector<std::int16_t> decode_xbox_ima(const std::uint8_t* data, std::size_t size, int channels,
                                          std::size_t max_frames);

struct AdxStream {
    int channels = 0;
    int rate = 0;
    int block_size = 0;
    int cutoff = 0;  // high-pass frequency the predictor coefficients come from
    int version = 0; // header version (3 or 4)
    int encoding = 0;
    std::uint32_t total_frames = 0;
    bool loops = false;
    std::uint32_t loop_start = 0; // frames
    std::uint32_t loop_end = 0;
    std::vector<std::int16_t> samples; // interleaved
};

// a whole ADX stream (header, "(c)CRI", 18-byte blocks, end marker); false with error on a bad
// header or an encrypted stream
bool decode_adx(const std::vector<std::uint8_t>& stream, AdxStream& out, std::string& error);

// the payloads of one stream id of an MPEG-1 program stream, in file order (Sofdec: 0xC0 is the
// ADX audio, 0xE0 the MPEG-1 video)
std::vector<std::uint8_t> demux_program_stream(const std::vector<std::uint8_t>& file,
                                               int stream_id);

bool write_wav(const std::string& path, int rate, int channels,
               const std::vector<std::int16_t>& samples);

struct Levels {
    double peak_db = -200.0; // dBFS
    double rms_db = -200.0;
    std::size_t clipped = 0; // samples at -32768 or 32767
};

Levels measure_levels(const std::vector<std::int16_t>& samples);
