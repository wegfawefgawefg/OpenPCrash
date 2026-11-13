#pragma once
// Shared by the crash handlers (report_crash_posix.cpp, report_crash_windows.cpp) and
// report_crash.cpp: fixed buffers only, so a handler reads them without allocating or locking.
// The crash file is text: a first line, the reason, this run's log path, the context (one line of
// JSON), then the stack as "address module+offset" lines (named offline against the build's own
// executable, tools/reports.py). In a fight the record goes beside it as bytes (report_fight.hpp).
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace crash {

inline constexpr std::size_t context_size = 64 * 1024;
inline constexpr int max_frames = 64;

extern char text_path[1024];
extern char fight_path[1024];
extern char log_path[1024];
extern char contexts[2][context_size];
extern std::atomic<int> context_at;
extern char last_words[512]; // an uncaught exception's what(), or empty
extern std::atomic<bool> crashing;

std::size_t text_size(const char* text);
// "0x1f3a" into out (at least 19 bytes)
void format_hex(char* out, std::uintptr_t value);
const char* context_text(std::size_t& size);
// std::terminate's last words kept, then abort() as before
void set_terminate_handler();
// the paths and the log's, kept for the handler
void set_paths(const char* text, const char* fight);

} // namespace crash
