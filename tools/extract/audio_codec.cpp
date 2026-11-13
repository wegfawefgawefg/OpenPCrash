#include "audio_codec.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0] | p[1] << 8 | p[2] << 16) |
           static_cast<std::uint32_t>(p[3]) << 24;
}

std::uint32_t be32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) << 24 |
           static_cast<std::uint32_t>(p[1] << 16 | p[2] << 8 | p[3]);
}

int be16(const std::uint8_t* p) {
    return p[0] << 8 | p[1];
}

int clamp16(int s) {
    return s < -32768 ? -32768 : s > 32767 ? 32767 : s;
}

// the IMA ADPCM tables every IMA decoder shares
const int ima_steps[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,   21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,   73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,  253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,  876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749, 3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int ima_index_steps[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

// one nibble: the multiplying expansion, (2 * magnitude + 1) * step / 8, as FFmpeg's IMA WAV and
// the archive's SE_BIN decoder (which matches vgmstream's decode_xbox_ima) do; the shift-and-add
// form some IMA decoders use rounds differently and drifts by up to ~90
int ima_expand(int nibble, int& predictor, int& index) {
    const int step = ima_steps[index];
    const int delta = ((2 * (nibble & 7) + 1) * step) >> 3;
    predictor = clamp16(nibble & 8 ? predictor - delta : predictor + delta);
    index += ima_index_steps[nibble];
    index = index < 0 ? 0 : index > 88 ? 88 : index;
    return predictor;
}

// a 4-bit two's complement value
int signed_nibble(int nibble) {
    return nibble & 8 ? nibble - 16 : nibble;
}

} // namespace

std::vector<RiffChunk> riff_chunks(const std::uint8_t* riff, std::size_t end) {
    std::vector<RiffChunk> chunks;
    std::size_t at = 12;
    while (at + 8 <= end) {
        RiffChunk chunk;
        chunk.id.assign(reinterpret_cast<const char*>(riff + at), 4);
        chunk.size = le32(riff + at + 4);
        chunk.offset = at + 8;
        chunks.push_back(chunk);
        // chunks are word aligned; a pad byte follows an odd size
        at += 8 + static_cast<std::size_t>(chunk.size) + (chunk.size & 1);
    }
    return chunks;
}

const RiffChunk* find_chunk(const std::vector<RiffChunk>& chunks, const char* id) {
    for (const RiffChunk& chunk : chunks) {
        if (chunk.id == id) {
            return &chunk;
        }
    }
    return nullptr;
}

std::vector<std::int16_t> decode_xbox_ima(const std::uint8_t* data, std::size_t size, int channels,
                                          std::size_t max_frames) {
    std::vector<std::int16_t> out;
    const std::size_t block_bytes = 36 * static_cast<std::size_t>(channels);
    const std::size_t blocks = size / block_bytes;
    out.reserve(blocks * 64 * channels);
    std::vector<std::int16_t> block(64 * channels);
    for (std::size_t b = 0; b < blocks && out.size() / channels < max_frames; ++b) {
        const std::uint8_t* at = data + b * block_bytes;
        for (int c = 0; c < channels; ++c) {
            // header: s16 predictor (frame 0), u8 step index, u8 zero
            const std::uint8_t* head = at + 4 * c;
            int predictor = static_cast<std::int16_t>(head[0] | head[1] << 8);
            int index = head[2] > 88 ? 88 : head[2];
            block[c] = static_cast<std::int16_t>(predictor);
            // 32 bytes of nibbles, low first, in 4-byte runs per channel; the 64th is padding
            for (int n = 0; n < 63; ++n) {
                const int run = n / 8;
                const int byte = (n % 8) / 2;
                const std::uint8_t value = at[4 * channels + (run * channels + c) * 4 + byte];
                const int nibble = n & 1 ? value >> 4 : value & 15;
                block[(n + 1) * channels + c] =
                    static_cast<std::int16_t>(ima_expand(nibble, predictor, index));
            }
        }
        out.insert(out.end(), block.begin(), block.end());
    }
    if (out.size() > max_frames * channels) {
        out.resize(max_frames * channels);
    }
    return out;
}

// ADX, as the public format descriptions have it (vgmstream's adx decoder, Wikipedia "ADX (file
// format)"): a big-endian header, then frames of 18 bytes per channel, each a 16-bit scale and 32
// 4-bit residuals, predicted from the two samples before with a fixed pair of coefficients that
// come from the header's high-pass frequency.
namespace {

struct AdxHeader {
    std::size_t data_offset = 0; // the copyright offset + 4: where the first frame starts
    int encoding = 0;            // 3: the standard fixed-coefficient ADPCM
    int frame_bytes = 0;         // 18
    int sample_bits = 0;         // 4
    int channels = 0;
    int sample_rate = 0;
    std::uint32_t sample_count = 0;
    int highpass = 0; // Hz
    int version = 0;
    int flags = 0; // nonzero: encrypted
};

AdxHeader read_adx_header(const std::uint8_t* h) {
    AdxHeader hd;
    hd.data_offset = static_cast<std::size_t>(be16(h + 2)) + 4;
    hd.encoding = h[4];
    hd.frame_bytes = h[5];
    hd.sample_bits = h[6];
    hd.channels = h[7];
    hd.sample_rate = static_cast<int>(be32(h + 8));
    hd.sample_count = be32(h + 12);
    hd.highpass = be16(h + 16);
    hd.version = h[18];
    hd.flags = h[19];
    return hd;
}

// the two prediction coefficients in 4.12 fixed point: with
//   a = sqrt(2) - cos(2 pi highpass / rate), b = sqrt(2) - 1, c = (a - sqrt((a + b)(a - b))) / b
// they are 2c and -c^2, truncated toward zero
void adx_coefficients(int highpass, int rate, int& first, int& second) {
    const double two_pi = 2.0 * 3.14159265358979;
    const double root2 = std::sqrt(2.0);
    const double a = root2 - std::cos(highpass * two_pi / rate);
    const double b = root2 - 1.0;
    const double c = (a - std::sqrt((a + b) * (a - b))) / b;
    first = static_cast<int>(c * 2.0 * 4096.0);
    second = static_cast<int>(-(c * c) * 4096.0);
}

// one channel's frame: 32 samples into out[0], out[step], ...; false on the end-of-stream frame
bool decode_adx_frame(const std::uint8_t* frame, int first, int second, int& prev1, int& prev2,
                      std::uint32_t samples_left, std::int16_t* out, int step) {
    const int header = be16(frame);
    if (header & 0x8000) {
        return false;
    }
    const int scale = (header & 0x1FFF) + 1;
    for (int i = 0; i < 32; ++i) {
        const std::uint8_t pair = frame[2 + i / 2];
        const int residual = signed_nibble(i % 2 == 0 ? pair >> 4 : pair & 0x0F);
        const int prediction = (first * prev1 + second * prev2) >> 12;
        const int sample = clamp16(prediction + residual * scale);
        prev2 = prev1;
        prev1 = sample;
        if (static_cast<std::uint32_t>(i) < samples_left) {
            out[static_cast<std::size_t>(i) * step] = static_cast<std::int16_t>(sample);
        }
    }
    return true;
}

} // namespace

bool decode_adx(const std::vector<std::uint8_t>& stream, AdxStream& out, std::string& error) {
    if (stream.size() < 0x18 || be16(stream.data()) != 0x8000) {
        error = "no ADX header";
        return false;
    }
    const AdxHeader hd = read_adx_header(stream.data());
    const bool standard = hd.encoding == 3 && hd.frame_bytes == 18 && hd.sample_bits == 4;
    if (!standard || hd.channels < 1 || hd.channels > 2 || hd.data_offset > stream.size() ||
        std::memcmp(stream.data() + hd.data_offset - 6, "(c)CRI", 6) != 0) {
        error = "unsupported ADX layout";
        return false;
    }
    if (hd.flags != 0) {
        error = "encrypted ADX (flags " + std::to_string(hd.flags) + ")";
        return false;
    }
    out.encoding = hd.encoding;
    out.block_size = hd.frame_bytes;
    out.channels = hd.channels;
    out.rate = hd.sample_rate;
    out.total_frames = hd.sample_count;
    out.cutoff = hd.highpass;
    out.version = hd.version;
    // the loop: a flag word, the start sample at +4 and the end sample at +12, in a block at 0x18
    // (version 3) or 0x24 (version 4) that has to end before the copyright string
    const std::size_t loop_block = hd.version == 4 ? 0x24 : 0x18;
    if ((hd.version == 3 || hd.version == 4) && loop_block + 0x14 <= hd.data_offset - 6) {
        const std::uint8_t* loop = stream.data() + loop_block;
        out.loops = be32(loop) != 0;
        out.loop_start = be32(loop + 4);
        out.loop_end = be32(loop + 12);
    }

    int first = 0, second = 0;
    adx_coefficients(hd.highpass, hd.sample_rate, first, second);
    // frames come one per channel in turn, each 32 samples of its channel
    std::vector<int> prev1(static_cast<std::size_t>(hd.channels), 0);
    std::vector<int> prev2(static_cast<std::size_t>(hd.channels), 0);
    out.samples.assign(static_cast<std::size_t>(hd.sample_count) * hd.channels, 0);
    std::size_t pos = hd.data_offset;
    for (std::uint32_t start = 0; start < hd.sample_count; start += 32) {
        for (int ch = 0; ch < hd.channels; ++ch) {
            if (pos + 18 > stream.size()) {
                error = "ADX stream ends early";
                return false;
            }
            std::int16_t* dest =
                out.samples.data() + static_cast<std::size_t>(start) * hd.channels + ch;
            if (!decode_adx_frame(&stream[pos], first, second, prev1[ch], prev2[ch],
                                  hd.sample_count - start, dest, hd.channels)) {
                error = "ADX end marker before the last frame";
                return false;
            }
            pos += 18;
        }
    }
    return true;
}

std::vector<std::uint8_t> demux_program_stream(const std::vector<std::uint8_t>& file,
                                               int stream_id) {
    std::vector<std::uint8_t> out;
    const std::size_t n = file.size();
    std::size_t p = 0;
    while (p + 6 <= n) {
        if (file[p] != 0 || file[p + 1] != 0 || file[p + 2] != 1) {
            ++p;
            continue;
        }
        const int id = file[p + 3];
        if (id == 0xBA) {
            // pack header: MPEG-1 12 bytes, MPEG-2 14 plus stuffing
            p += (file[p + 4] >> 6) == 1 ? 14 + (p + 13 < n ? file[p + 13] & 7 : 0) : 12;
            continue;
        }
        if (id == 0xB9 || id < 0xBB) {
            p += 4;
            continue;
        }
        const std::size_t body = p + 6;
        const std::size_t end = body + static_cast<std::size_t>(be16(&file[p + 4]));
        if (end > n) {
            break;
        }
        if (id == stream_id) {
            // MPEG-1 packet header: stuffing, STD buffer, then PTS / PTS+DTS / none
            std::size_t q = body;
            while (q < end && file[q] == 0xFF) {
                ++q;
            }
            if (q < end && (file[q] >> 6) == 1) {
                q += 2;
            }
            if (q < end) {
                q += (file[q] >> 4) == 2 ? 5 : (file[q] >> 4) == 3 ? 10 : 1;
            }
            if (q < end) {
                out.insert(out.end(), file.begin() + static_cast<long>(q),
                           file.begin() + static_cast<long>(end));
            }
        }
        p = end;
    }
    return out;
}

bool write_wav(const std::string& path, int rate, int channels,
               const std::vector<std::int16_t>& samples) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return false;
    }
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(samples.size() * 2);
    const std::uint32_t block = static_cast<std::uint32_t>(channels * 2);
    std::uint8_t head[44];
    auto put32 = [&](int at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            head[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
        }
    };
    auto put16 = [&](int at, std::uint32_t v) {
        head[at] = static_cast<std::uint8_t>(v);
        head[at + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    std::memcpy(head, "RIFF", 4);
    put32(4, 36 + data_bytes);
    std::memcpy(head + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);
    put16(22, static_cast<std::uint32_t>(channels));
    put32(24, static_cast<std::uint32_t>(rate));
    put32(28, static_cast<std::uint32_t>(rate) * block);
    put16(32, block);
    put16(34, 16);
    std::memcpy(head + 36, "data", 4);
    put32(40, data_bytes);
    bool ok = std::fwrite(head, 1, sizeof head, file) == sizeof head;
    // little-endian samples, written as such on any host
    std::vector<std::uint8_t> bytes(samples.size() * 2);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const std::uint16_t v = static_cast<std::uint16_t>(samples[i]);
        bytes[2 * i] = static_cast<std::uint8_t>(v);
        bytes[2 * i + 1] = static_cast<std::uint8_t>(v >> 8);
    }
    ok = ok && std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && ok;
}

Levels measure_levels(const std::vector<std::int16_t>& samples) {
    Levels levels;
    int peak = 0;
    double sum = 0.0;
    for (const std::int16_t s : samples) {
        const int magnitude = s < 0 ? -s : s;
        peak = magnitude > peak ? magnitude : peak;
        sum += static_cast<double>(s) * s;
        if (s == 32767 || s == -32768) {
            ++levels.clipped;
        }
    }
    if (peak > 0) {
        levels.peak_db = 20.0 * std::log10(peak / 32768.0);
        levels.rms_db = 10.0 * std::log10(sum / samples.size() / (32768.0 * 32768.0));
    }
    return levels;
}
