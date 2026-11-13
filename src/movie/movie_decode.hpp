#pragma once
// One Sofdec movie's picture (docs/movies.md): the .sfd is an MPEG-1 program stream, video in
// stream 0xE0 and CRI ADX sound in 0xC0. pl_mpeg's demuxer splits it and its MPEG-1 decoder turns
// the video into YCbCr 4:2:0 frames; the sound is not read here (the extractor decoded it to WAV
// and the sound engine plays that).
#include <cstdint>

struct plm_buffer_t;
struct plm_demux_t;
struct plm_video_t;

// the decoded frame's planes: Y at full size, Cb and Cr at half width and height (MPEG-1 4:2:0,
// BT.601 studio range); each stride is the plane's width rounded up to whole macroblocks
struct MoviePlanes {
    int width = 0;
    int height = 0;
    const std::uint8_t* y = nullptr;
    const std::uint8_t* cb = nullptr;
    const std::uint8_t* cr = nullptr;
    int y_stride = 0;
    int c_stride = 0;
};

struct MovieDecoder {
    plm_buffer_t* file = nullptr;  // the .sfd, read as needed
    plm_demux_t* demux = nullptr;  // its packs and packets
    plm_buffer_t* video = nullptr; // the 0xE0 payload, filled from the demuxer on demand
    plm_video_t* decoder = nullptr;
    void* frame = nullptr; // pl_mpeg's last decoded frame (a plm_frame_t), null before the first
    int width = 0;
    int height = 0;
    double frame_rate = 0.0; // from the sequence header (29.97 for every movie on the disc)
    int frames = 0;          // decoded so far
    bool ended = false;      // no frame left
};

// open the file and read up to its first sequence header; false (and nothing held) if it isn't
// an MPEG program stream with an MPEG-1 video stream
bool open_movie_decoder(MovieDecoder& movie, const char* path);
void close_movie_decoder(MovieDecoder& movie);
// decode the next frame in display order; false at the end
bool decode_movie_frame(MovieDecoder& movie);
// the last decoded frame, good until the next decode
MoviePlanes movie_planes(const MovieDecoder& movie);
// the last decoded frame as RGBA8, `stride` bytes a row, alpha 255
void movie_frame_to_rgba(const MovieDecoder& movie, std::uint8_t* rgba, int stride);
