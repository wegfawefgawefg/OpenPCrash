#pragma once
// The first-run setup's work, on a thread of its own: open the player's disc (an image or a game
// folder, tools/extract/disc_image.hpp), check it is Phantom Crash and that the drive has room,
// extract it into data/ and cook cooked/ from it (docs/assets.md). The screen (setup_screen.hpp)
// reads its progress and can ask it to stop. Nothing leaves the machine.
#include "extract.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

enum class SetupStage { idle, opening, extracting, cooking, done, failed, stopped };

// what went wrong, in words a player reads
struct SetupFailure {
    std::string title;  // "This isn't Phantom Crash"
    std::string detail; // what to do about it
};

struct SetupJob {
    std::string source; // the dropped or chosen path
    std::string data_dir;
    std::string cooked_dir;
    std::atomic<SetupStage> stage{SetupStage::idle};
    ExtractProgress progress;
    std::mutex mutex; // guards failure
    SetupFailure failure;
    std::thread thread;
};

// the setup's room on disk: data/ and cooked/ from the retail disc (2.7 GB and 0.6 GB, 2026-11-12),
// with some to spare. PHANTOMCRASH_DEV_SETUP_NEED_GB overrides it (port-only), to see the message
bool setup_has_room(const std::string& data_dir, std::string& detail);
// starts the work on source; the job must be idle or finished
void setup_start(SetupJob& job, const std::string& source);
// 0..1000 over the whole setup
int setup_permille(const SetupJob& job);
// what it's doing now, for the line under the bar
std::string setup_activity(const SetupJob& job);
// asks it to stop and waits for the thread
void setup_stop(SetupJob& job);
SetupFailure setup_failure(SetupJob& job);
// data/ is there and whole (the manifest is written last)
bool setup_data_ready(const std::string& data_dir);
