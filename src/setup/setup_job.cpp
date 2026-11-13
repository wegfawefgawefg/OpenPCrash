#include "setup/setup_job.hpp"

#include "cook/cook.hpp"
#include "disc.hpp"
#include "extract_run.hpp"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

// data/ and cooked/ take 3.3 GB from the retail disc; ask for a little more
constexpr double need_gb = 3.6;
// the extraction's share of the bar; the cook has the rest
constexpr int extract_share = 940;

double gigabytes(std::uintmax_t bytes) {
    return static_cast<double>(bytes) / 1e9;
}

// the free space where dir is, or would be: its nearest folder that exists
std::uintmax_t free_bytes(const std::string& dir) {
    std::error_code error;
    fs::path at = fs::absolute(dir, error);
    while (!at.empty() && !fs::exists(at, error)) {
        const fs::path up = at.parent_path();
        if (up == at) {
            break;
        }
        at = up;
    }
    const fs::space_info space = fs::space(at, error);
    return error ? static_cast<std::uintmax_t>(-1) : space.available;
}

double needed_gb() {
    const char* forced = std::getenv("PHANTOMCRASH_DEV_SETUP_NEED_GB");
    return forced ? std::atof(forced) : need_gb;
}

SetupFailure out_of_space(const std::string& data_dir) {
    char text[512];
    std::snprintf(text, sizeof text,
                  "Setup needs about %.1f GB free on the drive that holds %s, and there is only "
                  "%.1f GB. Free some space, then drag the disc in again.",
                  needed_gb(), data_dir.c_str(), gigabytes(free_bytes(data_dir)));
    return {"Out of space", text};
}

// why the disc couldn't be opened, for a player
SetupFailure disc_failure(DiscProblem problem, const std::string& error) {
    switch (problem) {
    case DiscProblem::not_phantom_crash:
        return {"This isn't Phantom Crash",
                "That's another Xbox game: " + error +
                    ". Drag a copy of your Phantom Crash disc instead."};
    case DiscProblem::other_release:
        return {"This copy of Phantom Crash can't be read yet",
                "It's Phantom Crash, but not the North American (USA) release, which is the "
                "only one OpenPCrash reads so far."};
    case DiscProblem::archive:
        return {"This file is still packed",
                "It's a .zip, .7z or .rar archive. Unpack it first, then drag the .iso inside it "
                "onto this window."};
    case DiscProblem::damaged:
        return {"The image is damaged",
                "Part of it is missing or can't be read; it may have been cut short while it was "
                "copied. Make a new copy of your disc and try again."};
    case DiscProblem::missing:
        return {"That can't be opened", error};
    case DiscProblem::not_xbox:
    case DiscProblem::none:
        break;
    }
    return {"This isn't Phantom Crash",
            "It isn't an Xbox disc image or a Phantom Crash game folder. Drag the disc image "
            "(.iso) you made from your disc, or the folder that holds default.xbe and Media."};
}

void fail(SetupJob& job, SetupFailure failure) {
    SDL_Log("setup: %s: %s", failure.title.c_str(), failure.detail.c_str());
    {
        std::lock_guard<std::mutex> lock(job.mutex);
        job.failure = std::move(failure);
    }
    job.stage = SetupStage::failed;
}

void work(SetupJob& job) {
    // open the disc and check it
    Disc disc;
    DiscProblem problem = DiscProblem::none;
    std::string error;
    if (!open_disc(disc, job.source, "", "", problem, error)) {
        SDL_Log("setup: %s", error.c_str());
        fail(job, disc_failure(problem, error));
        return;
    }
    SDL_Log("setup: %s, into %s", disc_source_label(disc.source).c_str(), job.data_dir.c_str());
    std::string room;
    if (!setup_has_room(job.data_dir, room)) {
        fail(job, out_of_space(job.data_dir));
        return;
    }

    // extract it; the manifest is written last, so a stopped run leaves none
    job.stage = SetupStage::extracting;
    std::error_code made;
    fs::create_directories(job.data_dir, made);
    fs::remove(fs::path(job.data_dir) / "manifest.sexp", made);
    ExtractOptions options;
    options.out_dir = job.data_dir;
    const bool extracted = run_extract(disc, options, {}, &job.progress, error);
    if (job.progress.cancel.load()) {
        fs::remove(fs::path(job.data_dir) / "manifest.sexp", made);
        job.stage = SetupStage::stopped;
        return;
    }
    if (!extracted || free_bytes(job.data_dir) < (64u << 20)) {
        fs::remove(fs::path(job.data_dir) / "manifest.sexp", made);
        fail(job, free_bytes(job.data_dir) < (256u << 20)
                      ? out_of_space(job.data_dir)
                      : SetupFailure{disc.source.image ? "The image is damaged"
                                                       : "The game folder is damaged",
                                     "Some of the game's files couldn't be read. Make a new "
                                     "copy of your disc and try again."});
        SDL_Log("setup: %s", error.c_str());
        return;
    }

    // cook the packs the game loads
    job.stage = SetupStage::cooking;
    CookOptions cook;
    cook.data_dir = job.data_dir;
    cook.cooked_dir = job.cooked_dir;
    cook.quiet = true;
    CookReport report;
    if (!cook_all(cook, report, error)) {
        const bool full = free_bytes(job.cooked_dir) < (256u << 20);
        if (full) {
            fs::remove(fs::path(job.data_dir) / "manifest.sexp", made);
        }
        fail(job, full ? out_of_space(job.cooked_dir)
                       : SetupFailure{"Setup couldn't finish", "Preparing the game's files "
                                                               "failed: " + error});
        return;
    }
    SDL_Log("setup: done, %d packs cooked in %.1f s", report.cooked, report.seconds);
    job.stage = SetupStage::done;
}

} // namespace

bool setup_has_room(const std::string& data_dir, std::string& detail) {
    const std::uintmax_t available = free_bytes(data_dir);
    char text[160];
    std::snprintf(text, sizeof text, "%.1f GB free, %.1f GB needed", gigabytes(available),
                  needed_gb());
    detail = text;
    return gigabytes(available) >= needed_gb();
}

void setup_start(SetupJob& job, const std::string& source) {
    if (job.thread.joinable()) {
        job.thread.join();
    }
    job.source = source;
    job.progress.permille = 0;
    job.progress.family = -1;
    job.progress.family_assets = 0;
    job.progress.assets_seen = 0;
    job.progress.cancel = false;
    job.stage = SetupStage::opening;
    job.thread = std::thread(work, std::ref(job));
}

int setup_permille(const SetupJob& job) {
    switch (job.stage.load()) {
    case SetupStage::extracting:
        return extract_permille(job.progress) * extract_share / 1000;
    case SetupStage::cooking:
        return extract_share + 20; // the cook says nothing until it's done; a few seconds
    case SetupStage::done:
        return 1000;
    default:
        return 0;
    }
}

std::string setup_activity(const SetupJob& job) {
    switch (job.stage.load()) {
    case SetupStage::opening:
        return "Opening the disc...";
    case SetupStage::extracting: {
        const int family = job.progress.family.load();
        return family >= 0 ? std::string("Reading the disc: ") + extract_family_label(family)
                           : std::string("Reading the disc...");
    }
    case SetupStage::cooking:
        return "Preparing the game's files...";
    case SetupStage::done:
        return "Done.";
    default:
        return "";
    }
}

void setup_stop(SetupJob& job) {
    job.progress.cancel = true;
    if (job.thread.joinable()) {
        job.thread.join();
    }
}

SetupFailure setup_failure(SetupJob& job) {
    std::lock_guard<std::mutex> lock(job.mutex);
    return job.failure;
}

bool setup_data_ready(const std::string& data_dir) {
    std::error_code error;
    return fs::is_regular_file(fs::path(data_dir) / "manifest.sexp", error);
}
