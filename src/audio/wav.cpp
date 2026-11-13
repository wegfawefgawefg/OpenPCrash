#include "audio/wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

std::uint16_t le16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}

// one sample of any supported format to PCM16
std::int16_t to_pcm16(const unsigned char* p, const WavInfo& info) {
    if (info.format == 3) {
        float x = 0.0f;
        std::memcpy(&x, p, sizeof x);
        x = std::clamp(x, -1.0f, 1.0f);
        return static_cast<std::int16_t>(std::lround(x * 32767.0f));
    }
    switch (info.bits) {
    case 8:
        return static_cast<std::int16_t>((p[0] - 128) << 8);
    case 16:
        return static_cast<std::int16_t>(le16(p));
    case 24:
        return static_cast<std::int16_t>(p[1] | p[2] << 8);
    default:
        return static_cast<std::int16_t>(p[2] | p[3] << 8);
    }
}

} // namespace

bool read_wav_header(std::FILE* file, WavInfo& info) {
    unsigned char head[12];
    if (std::fseek(file, 0, SEEK_SET) != 0 || std::fread(head, 1, 12, file) != 12 ||
        std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0) {
        return false;
    }
    std::fseek(file, 0, SEEK_END);
    const std::int64_t file_size = std::ftell(file);
    std::int64_t at = 12;
    bool fmt = false;
    // walk the chunks (padded to even sizes) till both turned up
    while (at + 8 <= file_size) {
        unsigned char chunk[8];
        std::fseek(file, static_cast<long>(at), SEEK_SET);
        if (std::fread(chunk, 1, 8, file) != 8) {
            return false;
        }
        const std::int64_t size = le32(chunk + 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            unsigned char f[16];
            if (std::fread(f, 1, 16, file) != 16) {
                return false;
            }
            info.format = le16(f);
            info.channels = le16(f + 2);
            info.rate = static_cast<int>(le32(f + 4));
            info.block_align = le16(f + 12);
            info.bits = le16(f + 14);
            if (info.format == 0xFFFE && size >= 40) {
                // WAVE_FORMAT_EXTENSIBLE: the subformat's first word says PCM or float
                unsigned char ext[10];
                if (std::fread(ext, 1, 10, file) == 10) {
                    info.format = le16(ext + 8);
                }
            }
            fmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            info.data_start = at + 8;
            // the chunk's size, or the file's end when the writer left it short or open
            info.data_size = std::min(size, file_size - info.data_start);
            if (size == 0 || size == 0xFFFFFFFF) {
                info.data_size = file_size - info.data_start;
            }
            if (fmt) {
                break;
            }
        }
        at += 8 + size + (size & 1);
    }
    const bool format_ok =
        info.format == 1 ? (info.bits == 8 || info.bits == 16 || info.bits == 24 || info.bits == 32)
                         : (info.format == 3 && info.bits == 32);
    return fmt && info.data_start > 0 && format_ok && info.channels >= 1 && info.channels <= 2 &&
           info.rate > 0 && info.block_align == info.channels * info.bits / 8;
}

bool read_wav(const std::string& path, WavInfo& info, std::vector<std::int16_t>& pcm) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }
    bool ok = read_wav_header(file, info);
    if (ok) {
        std::vector<unsigned char> bytes(static_cast<std::size_t>(info.data_size));
        std::fseek(file, static_cast<long>(info.data_start), SEEK_SET);
        const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), file);
        const int width = info.bits / 8;
        const std::size_t samples = got / static_cast<std::size_t>(info.block_align) *
                                    static_cast<std::size_t>(info.channels);
        pcm.resize(samples);
        for (std::size_t i = 0; i < samples; ++i) {
            pcm[i] = to_pcm16(bytes.data() + i * static_cast<std::size_t>(width), info);
        }
    }
    std::fclose(file);
    return ok;
}
