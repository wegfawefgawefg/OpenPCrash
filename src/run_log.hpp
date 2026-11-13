#pragma once
// The run log: every SDL_Log line of a run also written to a file of its own under the game's pref
// path (SDL_GetPrefPath: ~/.local/share/PhantomCrash/PhantomCrash/logs/run-YYYYMMDD-HHMMSS.log on
// Linux), the newest few kept, like Mans In My House's run logs. A playtest's report then comes
// with the sound device's story (src/audio/audio_follow.cpp) and whatever else was said.
// PHANTOMCRASH_RUN_LOG=0 writes none. Either way stdout is line buffered and SDL_Log's lines go out
// holding stdout's lock, so they never split a --trace line.
#include <string>

// the log lines whole, then opens this run's file, says what started, and keeps the newest `keep`
// (the oldest removed)
void run_log_start(int argc, char** argv, int keep = 10);
// the file being written ("" when none)
const std::string& run_log_path();
void run_log_stop();
