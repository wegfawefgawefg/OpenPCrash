#include "run_log.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

namespace {

struct RunLog {
    std::FILE* file = nullptr;
    std::string path;
    std::mutex lock; // SDL_Log may be called from any thread
    SDL_LogOutputFunction then = nullptr;
    void* then_data = nullptr;
    Uint64 started = 0;
};

// stdout's own lock (glibc's flockfile, the CRT's _lock_file)
void stdout_lock() {
#ifdef _WIN32
    _lock_file(stdout);
#else
    flockfile(stdout);
#endif
}
void stdout_unlock() {
#ifdef _WIN32
    _unlock_file(stdout);
#else
    funlockfile(stdout);
#endif
}

RunLog& run_log() {
    static RunLog log;
    return log;
}

const char* priority_name(SDL_LogPriority priority) {
    switch (priority) {
    case SDL_LOG_PRIORITY_VERBOSE:
        return "verbose";
    case SDL_LOG_PRIORITY_DEBUG:
        return "debug";
    case SDL_LOG_PRIORITY_WARN:
        return "warn";
    case SDL_LOG_PRIORITY_ERROR:
        return "error";
    case SDL_LOG_PRIORITY_CRITICAL:
        return "critical";
    default:
        return "info";
    }
}

// each line as SDL would say it (stderr), then into the file with the seconds since the start.
// The SDL line goes out holding stdout's lock, so it can't land inside a line the main thread's
// printf is writing; stdout is line buffered (run_log_start), so a line built from several printf
// calls stays in the buffer until its end and leaves in one write
void SDLCALL write_line(void* data, int category, SDL_LogPriority priority, const char* message) {
    RunLog& log = *static_cast<RunLog*>(data);
    if (log.then) {
        stdout_lock();
        log.then(log.then_data, category, priority, message);
        stdout_unlock();
    }
    const std::lock_guard<std::mutex> hold(log.lock);
    if (!log.file) {
        return;
    }
    const double seconds = static_cast<double>(SDL_GetTicksNS() - log.started) * 1.0e-9;
    std::fprintf(log.file, "%9.3f %s%s%s\n", seconds,
                 priority == SDL_LOG_PRIORITY_INFO ? "" : priority_name(priority),
                 priority == SDL_LOG_PRIORITY_INFO ? "" : ": ", message);
    std::fflush(log.file);
}

// the oldest run logs past `keep` (the new one counted) removed
void prune(const std::string& dir, int keep) {
    int count = 0;
    char** names = SDL_GlobDirectory(dir.c_str(), "run-*.log", 0, &count);
    if (!names) {
        return;
    }
    std::vector<std::string> runs(names, names + count);
    SDL_free(names);
    std::sort(runs.begin(), runs.end()); // the stamp sorts by time
    for (int i = 0; i + keep <= static_cast<int>(runs.size()); ++i) {
        SDL_RemovePath((dir + runs[static_cast<std::size_t>(i)]).c_str());
    }
}

} // namespace

void run_log_start(int argc, char** argv, int keep) {
    RunLog& log = run_log();
    if (log.file || log.started) {
        return;
    }
    // the log lines whole (docs/backlog.md, "Code"): stdout a line at a time (a trace redirected
    // to a file was block buffered, so the audio thread's SDL_Log lines split its lines at 4 KB
    // boundaries), and every SDL_Log line through write_line, the file or not
    std::setvbuf(stdout, nullptr, _IOLBF, 1 << 16);
    log.started = SDL_GetTicksNS();
    SDL_GetLogOutputFunction(&log.then, &log.then_data);
    SDL_SetLogOutputFunction(write_line, &log);
    const char* off = SDL_getenv("PHANTOMCRASH_RUN_LOG");
    if (off && off[0] == '0') {
        return;
    }
    char* prefs = SDL_GetPrefPath("PhantomCrash", "PhantomCrash");
    if (!prefs) {
        return;
    }
    const std::string dir = std::string(prefs) + "logs/";
    SDL_free(prefs);
    if (!SDL_CreateDirectory(dir.c_str())) {
        return;
    }
    prune(dir, keep);
    SDL_Time now = 0;
    SDL_DateTime t{};
    SDL_GetCurrentTime(&now);
    SDL_TimeToDateTime(now, &t, true);
    char name[64];
    std::snprintf(name, sizeof name, "run-%04d%02d%02d-%02d%02d%02d.log", t.year, t.month, t.day,
                  t.hour, t.minute, t.second);
    log.path = dir + name;
    log.file = std::fopen(log.path.c_str(), "a");
    if (!log.file) {
        log.path.clear();
        return;
    }
    std::string args;
    for (int i = 0; i < argc; ++i) {
        args += i ? " " : "";
        args += argv[i];
    }
    const int v = SDL_GetVersion();
    SDL_Log("run: %04d-%02d-%02d %02d:%02d:%02d, SDL %d.%d.%d (%s), %s", t.year, t.month, t.day,
            t.hour, t.minute, t.second, SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v),
            SDL_VERSIONNUM_MICRO(v), SDL_GetRevision(), args.c_str());
    SDL_Log("run: this log is %s", log.path.c_str());
}

const std::string& run_log_path() {
    return run_log().path;
}

void run_log_stop() {
    RunLog& log = run_log();
    if (log.file) {
        SDL_Log("run: the end");
    }
    if (log.started) {
        SDL_SetLogOutputFunction(log.then, log.then_data);
    }
    const std::lock_guard<std::mutex> hold(log.lock);
    if (log.file) {
        std::fclose(log.file);
        log.file = nullptr;
    }
}
