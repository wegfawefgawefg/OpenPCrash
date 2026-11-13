#pragma once
// Shared inside src/report: the report a job becomes, the worker, the HTTP under it, the crash
// handler and the watchdog (reports.hpp says what each kind is).
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// the static token the game sends with each report (not a secret: it only keeps junk out of the
// server, services/openpcrash-reports)
inline constexpr const char* report_token = "openpcrash-reports-1";
// a report without its picture and replay, as the server takes it
inline constexpr std::size_t report_json_limit = 240 * 1024;
// the replay packed; a bigger one goes without (said in the report)
inline constexpr std::size_t report_replay_limit = 1536 * 1024;
inline constexpr std::size_t report_log_bytes = 48 * 1024;

// what a report is made from, handed to the worker; it reads the log, packs the replay and turns
// the picture into a JPEG there, off the main thread
struct ReportJob {
    std::string kind, message, note;
    std::string shot_png; // a PNG file to send small and remove
    std::string fight;    // the fight's record as bytes (report_fight.hpp)
    std::vector<std::string> stack;
    nlohmann::json context; // build, system, game, settings (report_context)
    std::string log_path;   // whose tail goes with it: this run's log, or a crashed run's
    long long uptime = 0;
};

const std::string& reports_folder(); // <per-user folder>/reports/, "" when there is none
std::string report_url();
bool reports_consented(); // an address and a yes
bool reports_quiet();     // a capture or a scripted run: no automatic reports
std::string random_id();  // sixteen hex digits
// the home folder and user names in paths taken out of a line
std::string scrub(const std::string& text);
// build, system, game and settings now
nlohmann::json report_context();
double report_seconds(); // since reports_start

// report_send.cpp: the worker
void report_worker_start();
void report_queue(ReportJob job);
// what's queued sent for at most this long, the rest kept in the outbox
void report_worker_stop(double seconds);
void report_outbox_clear();

// report_http.cpp: POST it; the HTTP status, or 0 when it never got there (error says why)
int post_report(const std::string& url, const std::string& body, double timeout_seconds,
                std::string& error);

// report_shot.cpp: a PNG file as a JPEG at most 512 wide, base64; "" when it can't
std::string report_shot_jpeg(const std::string& png_path);
std::string base64(const std::string& bytes);

// report_crash_*.cpp: the handler writes crash-<session>.txt and, in a fight, .fight beside it
void crash_handlers_install(const std::string& text_path, const std::string& fight_path);
// the context the crash file starts with (one line of JSON), kept in fixed buffers
void crash_context_set(const std::string& json);
// every thread the game starts: room for the handler after a stack overflow there
void crash_guard_thread();
// the main thread's stack now, from another thread (the watchdog); empty when it can't
std::vector<std::string> crash_main_stack();
struct CrashFile {
    std::string reason, log_path, context;
    std::vector<std::string> stack;
};
bool crash_file_read(const std::string& path, CrashFile& out);

// report_watchdog.cpp
void watchdog_start();
void watchdog_stop();
