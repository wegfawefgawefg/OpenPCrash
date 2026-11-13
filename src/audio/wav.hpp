#pragma once
// RIFF/WAVE files as the extractor writes them (PCM 8/16/24-bit or 32-bit float, mono or stereo).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct WavInfo {
    int format = 0; // 1 PCM, 3 float
    int channels = 0;
    int rate = 0;
    int bits = 0;
    int block_align = 0;
    std::int64_t data_start = 0; // file offset of the samples
    std::int64_t data_size = 0;  // bytes of them
};

// the header of an open file: walks the chunks for "fmt " and "data"
bool read_wav_header(std::FILE* file, WavInfo& info);
// a whole file as interleaved PCM16
bool read_wav(const std::string& path, WavInfo& info, std::vector<std::int16_t>& pcm);
