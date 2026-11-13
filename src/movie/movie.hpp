#pragma once
// The movie player (docs/movies.md): the original's one Sofdec player (movie_player_open ..
// movie_player_close, decomp src/od/u_0ae8c0.c) on pl_mpeg for the picture and the sound engine
// for the sound. Movies are numbered as movie_files: 0 opening, 1 phantomcrash, 2 ending,
// 3 staff, 4 kudryavka, 5 epilogue, 6 opening02, 7 dolby, 8 phantagram, 9 the_end, 10 cloud.
//
// The picture follows the sound: the movie's clock is how much of its WAV the engine has played
// (movie_audio_seconds), so a slow frame drops pictures rather than stretching the movie. After
// the sound ends (staff's picture runs 3.5 s past it) the clock goes on in wall time.
// Presentation calls this from the main thread; the world never does.
#include "movie/movie_decode.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct Audio;

inline constexpr int movie_count = 11;
enum MovieId : int {
    movie_opening = 0,
    movie_phantomcrash = 1,
    movie_ending = 2,
    movie_staff = 3,
    movie_kudryavka = 4,
    movie_epilogue = 5,
    movie_opening02 = 6,
    movie_dolby = 7,
    movie_phantagram = 8,
    movie_the_end = 9,
    movie_cloud = 10,
};

// a row of data/audio/movies.sexp (what the extractor wrote for movie_files)
struct MovieFile {
    std::string name;         // "opening.sfd"
    std::string video;        // data_dir/movies/opening.sfd
    std::string sound;        // data_dir/audio/adx/opening.wav, empty when there is none
    double sound_seconds = 0; // its length
};

// What the renderer shows: the current picture as RGBA8, rows top to bottom, 640x480 for every
// movie. `serial` changes whenever the pixels do; the renderer uploads when it differs from what
// it uploaded last, and draws it as movie_draw_fullscreen does: one opaque quad over the whole
// 640x480 screen (docs/movies.md, "Presenting").
struct MovieImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // width * height * 4
    int frame = -1;                 // the movie frame it holds, -1 before the first
    std::uint64_t serial = 0;
};

struct MoviePlayer {
    std::vector<MovieFile> files; // by movie number
    bool opened = false;          // movie_player_open .. close; a second open is refused
    bool error = false;           // the movie couldn't be read: reported as over at once
    int movie = -1;               // playing, -1 none
    MovieDecoder decoder;         // holds a pointer to itself: never copy a player that's open
    bool sound = false;           // its sound plays (the clock follows it)
    bool sound_over = false;      // its sound has been heard to the end
    double sound_stalled = 0;     // seconds the sound clock hasn't moved near its end
    double clock = 0;             // seconds into the movie
    bool picture_over = false;    // the last frame has had its time
    int attract_next = 1;         // D_001D6F6C: which of opening / phantomcrash attract plays next
    MovieImage image;
};

// data_dir/audio/movies.sexp into player.files; false when it can't be read
bool load_movie_files(MoviePlayer& player, const std::string& data_dir);

// movie_player_open: false while already open, as the original refuses a second open
bool open_movie_player(MoviePlayer& player);
// movie_player_close: stops the picture and the sound
void close_movie_player(MoviePlayer& player, Audio& audio);
// movie_play: starts movie `movie` from its beginning, ending whatever played (the credits and
// the opening switch movies this way inside one open). A movie that can't be read sets the error,
// so movie_done reports it over and its screen moves on, as the original's error callback does.
void play_movie(MoviePlayer& player, Audio& audio, int movie);
// movie_player_service: the clock moved on (from the sound, or `seconds` of wall time once it has
// ended or when there is none) and the picture decoded up to it; call once per update
void service_movie(MoviePlayer& player, const Audio& audio, double seconds);
// movie_is_done: the picture and the sound have both ended, or the movie couldn't be read
bool movie_done(const MoviePlayer& player);
