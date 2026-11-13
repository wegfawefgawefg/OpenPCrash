#pragma once
// The first-run screen: when there is no data/ yet (no manifest), the game opens this instead of
// failing. It asks for the player's own Phantom Crash disc image or game folder (dropped on the
// window or picked with Browse), extracts and cooks it into options.data_dir and cooked_dir on a
// worker thread (setup_job.hpp) with a bar and a Cancel, and says plainly what's wrong if it can't.
// Later starts find the data and never see it. Its own window and SDL's 2D renderer, closed before
// the game's window opens.
//
// Port-only, for headless checks: PHANTOMCRASH_DEV_SETUP_DROP=PATH drops PATH on the window at
// frame 10; PHANTOMCRASH_DEV_SETUP_SHOTS=F,F,... writes setup_F.png beside --capture (or in the
// working folder) at those frames; PHANTOMCRASH_DEV_SETUP_CANCEL=F presses Cancel at frame F;
// PHANTOMCRASH_DEV_SETUP_QUIT=F quits at frame F.
#include "cli.hpp"

// 0 when the data is ready and the game goes on, 1 when the player quit
int run_setup_screen(const Options& options);
