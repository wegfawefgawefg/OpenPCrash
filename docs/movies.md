# Movies

The movie player in `src/movie/`, built as the `phantomcrash_movie` library from `cmake/movie.cmake`. It is the
original's one Sofdec player (`movie_player_open` .. `movie_player_close`, decomp `src/od/u_0ae8c0.c`) with pl_mpeg
decoding the picture and the sound engine (`docs/audio.md`) playing the sound the extractor already decoded from ADX
(`docs/audio-assets.md`, "Movies and ADX"). The spec is the decomp's `docs/port/data.md` section 21, `audio.md`
"Movies", `screens.md` "Boot and attract", `progression.md` "The ending" and findings "Ending and post-credits".
Presentation calls it from the main thread; the world never touches it.

## Files

| File | What |
|---|---|
| `movie.hpp` | the player's API and `MovieImage`, what the renderer shows |
| `movie_decode.hpp/.cpp` | one `.sfd` through pl_mpeg: demux to the video stream, frames, RGBA |
| `movie_player.cpp` | the player: movie table, play, the clock, frame selection, done |
| `movie_screens.hpp/.cpp` | the seven screens that play movies, step for step as the original's tasks |
| `third_party/pl_mpeg/` | PL_MPEG (MIT, Dominic Szablewski) at c871f2b, one local change (below), its licence |
| `tools/movie_check/` | `pc_movie_check`: offline decode, PNG dumps, headless screen runs, a real-time run |

## The movies

`movie_files` 0x25943C, read from `data/audio/movies.sexp` (paths to the `.sfd` and its sound WAV). Decoded by
`pc_movie_check` on 2026-11-11; frame counts equal FFmpeg's `-count_frames` for all eleven.

| # | File | Picture | Frames | Picture s | Sound s | Sound |
|---|---|---|---|---|---|---|
| 0 | opening | 640x480 29.97 | 3328 | 111.044 | 111.044 | 44.1 kHz stereo |
| 1 | phantomcrash | 640x480 29.97 | 3190 | 106.440 | 106.440 | 48 kHz stereo |
| 2 | ending | 640x480 29.97 | 2191 | 73.106 | 73.106 | 48 kHz mono |
| 3 | staff | 640x480 29.97 | 10400 | 347.014 | 343.069 | 44.1 kHz stereo |
| 4 | kudryavka | 640x480 29.97 | 1530 | 51.051 | 51.051 | 48 kHz stereo |
| 5 | epilogue | 640x480 29.97 | 2683 | 89.523 | 89.523 | 48 kHz stereo, silent |
| 6 | opening02 | 640x480 29.97 | 1180 | 39.373 | 39.333 | 48 kHz stereo |
| 7 | dolby | 640x480 29.97 | 150 | 5.005 | 5.000 | 48 kHz stereo |
| 8 | phantagram | 640x480 29.97 | 200 | 6.673 | 6.673 | 48 kHz stereo |
| 9 | the_end | 640x480 29.97 | 120 | 4.004 | 4.000 | 48 kHz stereo, silent |
| 10 | cloud | 640x480 29.97 | 420 | 14.014 | 14.000 | 48 kHz stereo, silent |

Every picture is 640x480 at 30000/1001 frames a second (sequence header frame rate code 4; FFmpeg's "59.94" is the
container's time base, not the frame rate), square pixels, the same size on every frame. Picture and sound agree to
a frame everywhere except `staff.sfd`, whose picture runs 3.9 s past its sound (the credits' last card held).
Eyeballed: frames at 1 s, a quarter, half, three quarters and the last of each look right (logos, credits text,
`kudryavka.sfd`'s burnt-in subtitles, cloud's line); against FFmpeg's decode the same frames differ by at most 11 of
255 (the RGB conversion and IDCT rounding), the last frames by a mean of 0.1. Decoding takes 0.3 to 1.8 ms a frame on
one core of the dev desktop, so the main thread can do it inside a frame.

## Decoding

A Sofdec `.sfd` is an MPEG-1 program stream: pack headers (0xBA), one system header (0xBB), a private stream 2
packet (0xBF) holding the `SofdecStream` header and the file name, MPEG-1 video in stream 0xE0, the CRI ADX file in
stream 0xC0 (labelled as MPEG audio), padding (0xBE). Two things stop a stock MPEG-PS reader:
- the system header's `video_bound` is 0 (and `audio_bound` 1), so pl_mpeg's `plm_create` sees no video stream. The
  decoder uses pl_mpeg's demuxer and video decoder directly instead: the demuxer feeds only 0xE0 packets to the video
  buffer, and a picture is known by its sequence header, not the stream count.
- stream 0xC0 is ADX, not MPEG audio; pl_mpeg's MP2 decoder is never created. The sound comes from the WAV.

pl_mpeg drops the last frame of a stream that ends on B-frames (it only flushes the pending reference picture when
the last decoded picture was I or P). Seven of the eleven movies end that way and lost a frame. The vendored copy
drops that test, marked "PHANTOMCRASH local change" in `plm_video_decode`; with it every count matches FFmpeg and the
last frames match FFmpeg's.

## The player

```cpp
MoviePlayer player;                                  // one, in presentation; never copied while open
load_movie_files(player, data_dir);                  // data/audio/movies.sexp
open_movie_player(player);                           // movie_player_open: false while open
play_movie(player, audio, movie_opening);            // movie_play: picture from frame 0, sound starts
service_movie(player, audio, seconds);               // movie_player_service, once per update
movie_done(player);                                  // movie_is_done
player.image                                         // the picture to show (below)
close_movie_player(player, audio);                   // movie_player_close: stops picture and sound
```

- The clock is how much of the movie's WAV the sound engine has played (`movie_audio_seconds`), as Sofdec syncs the
  picture to the ADX sound. When the sound has ended (heard to within 20 ms of its length, or stuck for 0.25 s within
  half a second of it) or there is none, the clock goes on with the wall time passed to `service_movie`. So
  `staff.sfd`'s extra 3.9 s of picture plays in wall time, and a hitch drops pictures instead of stretching the
  movie. A sound stuck for 1 s anywhere else (no device pulls the mix: none on the machine, or lost until the engine
  reopens one) also hands the clock to wall time, so a movie never waits on a device; if the device comes back the
  sound is that much behind the picture for the rest of the movie.
- `service_movie` decodes up to frame `floor(clock * 29.97)` and converts only the last one to RGBA.
- Done when the last frame has had its 1/29.97 s and the sound has ended, or at once when the file couldn't be
  opened (the original's error callback sets a byte `movie_is_done` reports, so a broken movie ends its screen).
- `play_movie` while a movie plays starts the new one (the credits and the opening chain movies this way).
- The sound's volume is the sound engine's: `play_movie_audio` takes the music level through the millibel table at
  0.96 of its dB, as `movie_play` does.

## Presenting

The renderer shows `player.image` (`MovieImage`): RGBA8, 640x480, rows top to bottom, alpha 255. `serial` goes up
whenever the pixels change (at most 29.97 times a second); `frame` is the movie frame it holds. RGBA rather than
YCbCr planes because the renderer already uploads RGBA textures (`renderer_upload_texture`) and needs no new shader;
it costs 1.2 MB a frame. `movie_planes(player.decoder)` gives the Y, Cb and Cr planes (4:2:0, BT.601 studio range)
if a YCbCr upload and a shader conversion are wanted later.

What the renderer needs to add (not done here; the renderer is another agent's):

```cpp
// replace an uploaded texture's pixels, same size (a staging copy, or a host-visible image)
bool renderer_update_texture(Renderer& renderer, int texture, const std::uint8_t* rgba);
// movie_draw_fullscreen: an opaque quad over the 640x480 screen, texture 1:1, no depth, no blend;
// on wider windows the 4:3 screen is pillarboxed like every other 640x480 screen
```

Wiring for `main.cpp` (another agent owns it):

```cpp
MoviePlayer movies;                                   // after init_audio
load_movie_files(movies, state.options.data_dir);
std::vector<EndingSubtitle> subtitles;
load_ending_subtitles(subtitles, state.options.data_dir);
int movie_texture = -1;                               // created on the first image
std::uint64_t movie_serial = 0;

// in a movie state's update (60 Hz)
MovieScreenStep step = step_movie_screen(screen, movies, *audio, input, subtitles, 1.0 / 60.0);
// draw frame: when step.draw
if (movies.image.serial != movie_serial) {
    if (movie_texture < 0) movie_texture = renderer_upload_texture(renderer, 640, 480, movies.image.rgba);
    else renderer_update_texture(renderer, movie_texture, movies.image.rgba.data());
    movie_serial = movies.image.serial;
}
// leaving the state: end_movie_screen(screen, movies, *audio);
```

`cmake/movie.cmake` already links `phantomcrash_movie` into the game; `CMakeLists.txt` includes it after the sound
engine.

## Screens

`start_movie_screen(screen, kind)`, then `step_movie_screen` once per 60 Hz update. It plays and services the movies
itself and returns what the original's task asks of the rest: draw the picture (`draw`), the ending's subtitle row
(`subtitle`), fades (`fade_reset`, `fade_out` in ticks with its mode, `fade_in`), and when it is over `done`,
`result` and the `next_state` the state's update picks. `end_movie_screen` is the task's destroy (closes the player).
Every movie state runs at `frame_cadence` 60. "From 1 s in" is the original's countdown of `frame_cadence` that runs
while it is 0 or more: input is first read on the 62nd update after `movie_play` (1.03 s).

| Screen | State | Movies | Skip | Leaving | Next |
|---|---|---|---|---|---|
| Phantagram logo | 0x9E | phantagram | accept, from 1 s in | cut to black (`fade_out` 0) | 0x12 logo |
| Dolby logo | 0x2D | dolby | accept, from 1 s in | no fade call | 2 attract movie |
| Attract movie | 2 | phantomcrash, opening by turns | any button, from 1 s in | 16 updates with nothing drawn, then cut | 0x28 title return |
| Opening | 0x19 | opening, then opening02 | accept, from 1 s into each; skipping opening goes to opening02 | opening02 fades out over 97 ticks from update 2127 of it | 0x16 name entry |
| Ending | 0xB | ending | none | | 0x25 credits |
| Credits | 0x25 | staff, epilogue, the_end looped | none for staff and epilogue; accept once 28 s of the 30 s loop timer remain | below | 0x18 night or 0xC |
| Secret movies | 0xC | kudryavka, then cloud | none for kudryavka; cloud by accept after 120 updates | cut to black | 0x18 night |

- Accept is `input_accept_pressed(player_input_mask(0))`: a fresh A or START on player 0's pad. "Any button" is
  `attract_input_any`: a fresh press of any button (mask 0xFFF0) on any pad, or either of two pad-change flags
  (`func_000DFD60/80`, globals not named in the decomp). The caller fills `MovieScreenInput`, and passes false while a
  fade runs (`screen_fade_frame` zeroes the pads in the original).
- Attract: `D_001D6F6C` starts at 1, so the first attract after boot is phantomcrash, then opening, alternating
  (kept in `MoviePlayer::attract_next`; the state's enter also calls `input_masks_reset`, the caller's).
- Opening: switching to opening02 is an update with nothing drawn (`opening_movie_second_start` only plays). Its fade
  starts 2127 updates (35.45 s) into opening02 and lasts 97 ticks with mode -1; opening02 is 39.37 s. The state
  update calls `name_entry_target_pilot` before going to 0x16; event trigger row 0 turns that into Salsa's welcome.
- Ending: no skip. The subtitles are timed by updates, not by the movie: row k is shown while
  `start <= updates / 2 < end`, `updates` counted from the first update after `movie_play`; the list only steps
  forward. The text renderer (not done here) draws the row centred on x 320 at y 416 with `wide_text_draw_small`'s
  arguments in `ending_movie_subtitles_draw` (18x18 glyphs, colour 0xFFF0F0F0). The rows are
  `data/tables/ending_subtitles.sexp`.
- Credits: entering 0x25 sets the beaten bit (`game_beaten_set`, the caller's). staff, then epilogue, then a 1-tick
  fade out; once the caller reports the fade black (`input.fade_black`) the loop timer is set to 30 s and the_end
  plays, fading in over 30 ticks each time it restarts. 30 updates before the timer ends a 30-tick fade out starts;
  at the end the result is 1 (secret movies, 0xC). Accept with under 28 s left plays sys cue 5 and cuts to the night
  (0x18). Measured headless: untouched, the credits end after 466.75 s.
- The original services `staff.sfd` only on even task timer ticks (`credits_task_next2`). Sofdec's picture follows
  its sound clock either way; the port services every update.

The music is stopped by the screen before a movie state (its destroy and the state change, decomp `audio.md`
"Movies"), so nothing else plays during a movie; that stays with the caller.

## Not like the original

- The movie sound's level: `movie_play` also sets the ADX voice's mix bins, all eight entries (front left and right,
  back left and right, centre, LFE) at -500 mB, as the 2D effects have. The sound engine plays the movie stream at
  0 dB on both sides, so movies are 5 dB louder than retail relative to effects and music. The fix belongs in
  `play_movie_audio` (`src/audio/audio_streams.cpp`) adds the -500 mB to the movie stream's gain.
- Frame pacing follows the sound in 10 ms steps of the engine's reader and the device's block size; Sofdec's
  vsync-locked presentation isn't reproduced.

## Checking

`pc_movie_check --data DIR [--png DIR] [--movie N]` decodes the movies and prints the table above (frame counts, size,
rate, picture and sound lengths, decode time), with PNGs to `--png` (never into the repo). `--screens` runs every
screen at 60 updates a second with the sound engine rendering offline, untouched and with a button held, printing
the movies played, the subtitle rows, fades, when each ends and its next state. `--realtime N` plays movie N through an
SDL sound device in real time; run it with `SDL_AUDIO_DRIVER=dummy` (or `disk`), never on speakers. With
`--no-device` nothing pulls the mix.

Measured (2026-11-11): every screen ends where the table says. Untouched: Phantagram 6.68 s, Dolby 5.02 s, attract
106.73 s (phantomcrash) and 111.33 s (opening), opening 150.45 s, ending 73.12 s, credits 466.75 s (result 1),
secret movies 65.08 s. With a button held: the logos at 1.03 s, attract 1.32 s, opening 2.08 s (both movies skipped),
the ending can't be skipped, credits at 438.63 s (2 s into the_end, result 0), secret movies at 53.07 s. Dolby in real
time on the dummy driver: the clock follows the sound (60 ms ahead of the wall, the device's buffering) and the movie
is done at 4.94 s of wall time. the_end with `--no-device`: frame 0 held for 1 s, then wall time, done at 5.02 s.
