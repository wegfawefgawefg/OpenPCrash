#pragma once
// The conversation debug player: phantomcrash --talk NAME plays one event script by name (talk_5b,
// a talk state such as 0x5b, bark_2c4b60, bio_..., maker_...) the way its state would, in a window
// of its own, or headless with --capture out.png --frames N (no window at all). Accept is Z, Enter
// or Space (A or START on a pad); the skip chord is Tab held (both triggers and both stick clicks).
// A debug entry beside the game's loop until the state machine plays conversations itself.
#include "cli.hpp"

int run_talk_player(const Options& options);
