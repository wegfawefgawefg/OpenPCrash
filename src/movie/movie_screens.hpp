#pragma once
// The screens that play movies (docs/movies.md, "Screens"), as the original's tasks run them:
// the Phantagram and Dolby logos, the attract movie, the opening, the ending, the credits and the
// secret movies. Each is a short machine stepped once per 60 Hz update (every movie state runs at
// frame_cadence 60), with the original's countdowns in updates, its skip rules and the result its
// state turns into the next state. Fades, drawing and the state change stay with the caller.
#include "movie/movie.hpp"

#include <string>
#include <vector>

struct Audio;

enum class MovieScreenKind : int {
    phantagram_logo, // state 0x9E, movie 8, then 0x12 logo
    dolby_logo,      // state 0x2D, movie 7, then 2 attract movie
    attract,         // state 2, phantomcrash and opening by turns, then 0x28 title return
    opening,         // state 0x19, opening then opening02, then 0x16 name entry
    ending,          // state 0xB, ending with subtitles, then 0x25 credits
    credits,         // state 0x25, staff, epilogue, the_end looped; then 0x18 or 0xC
    secret,          // state 0xC, kudryavka then cloud, then 0x18 night
};

// the pads as a movie screen reads them, filled by presentation each update (fades zero the pads
// in the original, screen_fade_frame; pass false while a fade runs)
struct MovieScreenInput {
    bool accept = false;     // input_accept_pressed(player 0): a fresh A or START
    bool any = false;        // attract_input_any: a fresh press of any button on any pad (0xFFF0)
                             // or either pad-change flag (func_000DFD60/80)
    bool fade_black = false; // screen_fade_is_black(): the credits wait for it
};

// what one update asks of the caller
struct MovieScreenStep {
    bool draw = false;       // movie_draw_fullscreen: the picture over the whole screen
    int subtitle = -1;       // the ending's subtitle row to draw centred at (320, 416), -1 none
    bool fade_reset = false; // screen_fade_reset: shown at once, no fade in
    float fade_out = -1;     // screen_fade_out_start(ticks, fade_out_mode); 0 cuts to black
    int fade_out_mode = 0;
    float fade_in = -1; // screen_fade_in_start(ticks)
    bool done = false;  // task_header_set(result): the screen is over
    int result = 0;
    int next_state = -1; // where the state's update goes on that result
};

// ending_subtitles from data/tables/ending_subtitles.sexp; text as written there
// (full-width UTF-8)
struct EndingSubtitle {
    int start = 0; // movie frames at 30 a second (updates / 2)
    int end = 0;
    std::string text;
};

struct MovieScreen {
    MovieScreenKind kind = MovieScreenKind::phantagram_logo;
    int step = 0;
    int frames = 0;      // the screen's countdown, in updates
    int count = 0;       // opening02's update count, the ending's subtitle ticks
    int subtitle = 0;    // the ending's next subtitle row (the list only steps forward)
    bool opened = false; // this screen opened the player (its destroy closes it only then)
    int result = 0;      // what it left with
};

bool load_ending_subtitles(std::vector<EndingSubtitle>& rows, const std::string& data_dir);

void start_movie_screen(MovieScreen& screen, MovieScreenKind kind);
// one update; `seconds` is the wall time since the last (1/60 at a steady 60 Hz), for the clock
// once a movie's sound has ended
MovieScreenStep step_movie_screen(MovieScreen& screen, MoviePlayer& player, Audio& audio,
                                  const MovieScreenInput& input,
                                  const std::vector<EndingSubtitle>& subtitles, double seconds);
// the screen's destroy: closes the player if it opened it
void end_movie_screen(MovieScreen& screen, MoviePlayer& player, Audio& audio);
