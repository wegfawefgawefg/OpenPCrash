// pl_mpeg over a Sofdec file (docs/movies.md, "Decoding"). Only the video packets go to the
// decoder; the ADX packets (0xC0, which pl_mpeg would take for MPEG audio), the Sofdec header
// (private stream 2, 0xBF) and the padding (0xBE) are passed over.
#include "movie/movie_decode.hpp"

// pl_mpeg.h uses size_t and FILE without including their headers
#include <cstddef>
#include <cstdio>
#include <pl_mpeg.h>

namespace {

constexpr int video_stream = 0xE0; // PLM_DEMUX_PACKET_VIDEO_1

// the video buffer ran dry: move the demuxer on to the next video packet
void read_video_packet(plm_buffer_t* buffer, void* user) {
    MovieDecoder& movie = *static_cast<MovieDecoder*>(user);
    while (plm_packet_t* packet = plm_demux_decode(movie.demux)) {
        if (packet->type == video_stream) {
            plm_buffer_write(buffer, packet->data, packet->length);
            return;
        }
    }
    if (plm_demux_has_ended(movie.demux)) {
        plm_buffer_signal_end(buffer);
    }
}

} // namespace

bool open_movie_decoder(MovieDecoder& movie, const char* path) {
    close_movie_decoder(movie);
    movie.file = plm_buffer_create_with_filename(path);
    if (!movie.file) {
        return false;
    }
    movie.demux = plm_demux_create(movie.file, 1);
    // Sofdec's system header says no video stream (video_bound 0) and one audio: whether there's
    // a picture is told by its sequence header below, not by the count
    if (!plm_demux_has_headers(movie.demux)) {
        close_movie_decoder(movie);
        return false;
    }
    movie.video = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
    plm_buffer_set_load_callback(movie.video, read_video_packet, &movie);
    movie.decoder = plm_video_create_with_buffer(movie.video, 1);
    // the sequence header comes with the first video packets
    if (!plm_video_has_header(movie.decoder)) {
        close_movie_decoder(movie);
        return false;
    }
    movie.width = plm_video_get_width(movie.decoder);
    movie.height = plm_video_get_height(movie.decoder);
    movie.frame_rate = plm_video_get_framerate(movie.decoder);
    return true;
}

void close_movie_decoder(MovieDecoder& movie) {
    // the decoder owns the video buffer and the demuxer the file buffer
    if (movie.decoder) {
        plm_video_destroy(movie.decoder);
    } else if (movie.video) {
        plm_buffer_destroy(movie.video);
    }
    if (movie.demux) {
        plm_demux_destroy(movie.demux);
    } else if (movie.file) {
        plm_buffer_destroy(movie.file);
    }
    movie = MovieDecoder{};
}

bool decode_movie_frame(MovieDecoder& movie) {
    if (!movie.decoder || movie.ended) {
        return false;
    }
    plm_frame_t* frame = plm_video_decode(movie.decoder);
    if (!frame) {
        movie.ended = true;
        return false;
    }
    movie.frame = frame;
    ++movie.frames;
    return true;
}

MoviePlanes movie_planes(const MovieDecoder& movie) {
    MoviePlanes planes;
    const plm_frame_t* frame = static_cast<const plm_frame_t*>(movie.frame);
    if (!frame) {
        return planes;
    }
    planes.width = static_cast<int>(frame->width);
    planes.height = static_cast<int>(frame->height);
    planes.y = frame->y.data;
    planes.cb = frame->cb.data;
    planes.cr = frame->cr.data;
    planes.y_stride = static_cast<int>(frame->y.width);
    planes.c_stride = static_cast<int>(frame->cb.width);
    return planes;
}

void movie_frame_to_rgba(const MovieDecoder& movie, std::uint8_t* rgba, int stride) {
    if (!movie.frame) {
        return;
    }
    // pl_mpeg writes R, G and B (BT.601, studio range expanded) and leaves the fourth byte
    plm_frame_t* frame = static_cast<plm_frame_t*>(movie.frame);
    plm_frame_to_rgba(frame, rgba, stride);
    for (unsigned row = 0; row < frame->height; ++row) {
        std::uint8_t* pixel = rgba + static_cast<std::size_t>(row) * stride;
        for (unsigned col = 0; col < frame->width; ++col) {
            pixel[col * 4 + 3] = 255;
        }
    }
}
