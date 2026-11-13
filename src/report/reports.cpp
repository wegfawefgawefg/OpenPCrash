// The reports themselves: the consent, the context every report carries, the limits on sending,
// and the jobs handed to the worker (report_send.cpp).
#include "report/reports.hpp"

#include "build_info.hpp"
#include "report/report_internal.hpp"
#include "run_log.hpp"
#include "user_folder.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <regex>
#include <set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#ifndef PHANTOMCRASH_REPORT_URL
#define PHANTOMCRASH_REPORT_URL ""
#endif

namespace {

constexpr int automatic_most = 6;        // automatic reports a session
constexpr double seconds_between = 10.0; // between any two

struct Reporter {
    std::mutex lock;
    std::string folder, session, url;
    std::atomic<int> consent{static_cast<int>(ReportConsent::unasked)};
    bool quiet = false, started = false, armed = false, finished = false;
    std::vector<std::pair<std::string, std::string>> settings;
    std::string gpu;
    int state = -1;
    bool fighting = false;
    std::string rules = "retail"; // the game's cheats and price rules (PC CONFIG's CHEATS)
    std::set<std::string> kinds_sent;
    int automatic_sent = 0;
    double last_sent = -1e9;
    std::string marker; // running-<session>.txt while the game runs
    Uint64 started_ns = 0;
};

// never destroyed: the worker, the watchdog and the crash handler may outlive main's statics
Reporter& reporter() {
    static Reporter* made = new Reporter;
    return *made;
}

std::string consent_path() {
    return reporter().folder + "consent.txt";
}

// "Ubuntu 24.04.1 LTS (Linux 6.8.0)", "Windows 10.0.22631", "macOS 14.5"
std::string os_name() {
#ifdef _WIN32
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto get = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))) {
            get(&v);
        }
    }
    return "Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) +
           "." + std::to_string(v.dwBuildNumber);
#elif defined(__APPLE__)
    char version[64] = {};
    std::size_t size = sizeof version - 1;
    sysctlbyname("kern.osproductversion", version, &size, nullptr, 0);
    return std::string("macOS ") + version;
#else
    std::string pretty = SDL_GetPlatform();
    std::ifstream release("/etc/os-release");
    std::string line;
    while (std::getline(release, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            pretty = line.substr(12);
            pretty.erase(std::remove(pretty.begin(), pretty.end(), '"'), pretty.end());
        }
    }
    utsname u{};
    if (uname(&u) == 0) {
        pretty += std::string(" (") + u.sysname + " " + u.release + ")";
    }
    return pretty;
#endif
}

void refresh_crash_context() {
    crash_context_set(
        report_context().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

// while the game runs, reports/running-<session>.txt says so; a clean exit removes it. One the
// next start finds with no crash file beside it ended where no handler saw (report_send.cpp)
void write_marker(Reporter& r) {
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    std::ofstream file(r.marker, std::ios::trunc);
    file << "openpcrash-session 1\npid " << pid << "\nsession " << r.session << "\nlog "
         << run_log_path() << "\n";
}

// a yes: the crash handler, the marker, the worker and the watchdog, once
void arm(Reporter& r) {
    if (r.armed || r.folder.empty() || !reports_available()) {
        return;
    }
    r.armed = true;
    std::error_code error;
    std::filesystem::create_directories(r.folder + "outbox", error);
    r.marker = r.folder + "running-" + r.session + ".txt";
    write_marker(r);
    refresh_crash_context();
    crash_handlers_install(r.folder + "crash-" + r.session + ".txt",
                           r.folder + "crash-" + r.session + ".fight");
    report_worker_start();
    if (!r.quiet) {
        watchdog_start();
    }
}

bool may_send(Reporter& r, const std::string& kind, bool manual, std::string& why) {
    const double now = report_seconds();
    if (now - r.last_sent < seconds_between) {
        why = "A report just went. Try again in a few seconds.";
        return false;
    }
    if (!manual && (r.kinds_sent.count(kind) != 0 || r.automatic_sent >= automatic_most)) {
        why = "already sent one";
        return false;
    }
    r.last_sent = now;
    if (!manual) {
        r.kinds_sent.insert(kind);
        ++r.automatic_sent;
    }
    return true;
}

ReportJob job_of(Reporter& r, const char* kind, const std::string& message) {
    ReportJob job;
    job.kind = kind;
    job.message = message.substr(0, 500);
    job.context = report_context();
    job.log_path = run_log_path();
    job.uptime = static_cast<long long>(report_seconds());
    (void)r;
    return job;
}

} // namespace

const std::string& reports_folder() {
    return reporter().folder;
}

std::string report_url() {
    return reporter().url;
}

bool reports_available() {
    return !reporter().url.empty();
}

bool reports_consented() {
    return reports_available() && reporter().consent.load() == static_cast<int>(ReportConsent::yes);
}

bool reports_quiet() {
    return reporter().quiet;
}

bool reports_question_due() {
    // PHANTOMCRASH_DEV_REPORT_ASK=1 (port-only): asked in a capture too, for its pictures
    const char* ask = std::getenv("PHANTOMCRASH_DEV_REPORT_ASK");
    const bool forced = ask && ask[0] == '1';
    return reports_available() && !reporter().folder.empty() && (forced || !reporter().quiet) &&
           report_consent() == ReportConsent::unasked;
}

double report_seconds() {
    const Uint64 started = reporter().started_ns;
    return started ? static_cast<double>(SDL_GetTicksNS() - started) * 1e-9 : 0.0;
}

std::string random_id() {
    static std::mutex* lock = new std::mutex;
    static std::mt19937_64* random = new std::mt19937_64(
        std::random_device{}() ^ (static_cast<std::uint64_t>(std::time(nullptr)) << 20));
    std::lock_guard<std::mutex> guard(*lock);
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>((*random)()));
    return text;
}

std::string scrub(const std::string& text) {
    static const std::regex* user_folder = new std::regex(
        R"(([/\\](?:home|Users|users|Documents and Settings|media|run/media)[/\\])[^/\\\s:]+)");
    std::string out = text;
    // the home folder itself first (it may be anywhere: /media/..., D:\...)
    static const std::string home = [] {
        const char* folder = SDL_GetUserFolder(SDL_FOLDER_HOME);
        std::string h = folder ? folder : "";
        while (h.size() > 1 && (h.back() == '/' || h.back() == '\\')) {
            h.pop_back();
        }
        return h.size() > 3 ? h : std::string();
    }();
    for (std::size_t at; !home.empty() && (at = out.find(home)) != std::string::npos;) {
        out.replace(at, home.size(), "~");
    }
    return std::regex_replace(out, *user_folder, "$1~");
}

nlohmann::json report_context() {
    Reporter& r = reporter();
    static const nlohmann::json system = [] {
        nlohmann::json s;
        s["os"] = scrub(os_name()).substr(0, 200);
        s["platform"] = SDL_GetPlatform();
        s["cores"] = SDL_GetNumLogicalCPUCores();
        s["ram_mb"] = SDL_GetSystemRAM();
        return s;
    }();
    const BuildInfo& build = build_info();
    nlohmann::json context;
    context["session"] = r.session;
    context["build"] = {{"version", build.version},
                        {"commit", build.commit},
                        {"date", build.date},
                        {"release", build.release},
                        {"label", build_label()}};
    context["system"] = system;
    std::lock_guard<std::mutex> lock(r.lock);
    context["system"]["gpu"] = r.gpu.substr(0, 300);
    context["game"] = {
        {"state", r.state}, {"fighting", r.fighting}, {"rules", r.rules.substr(0, 200)}};
    nlohmann::json settings = nlohmann::json::object();
    for (const auto& [key, value] : r.settings) {
        settings[key] = value.substr(0, 64);
    }
    context["settings"] = settings;
    return context;
}

void reports_start(int argc, char** argv, bool quiet) {
    Reporter& r = reporter();
    if (r.started) {
        return;
    }
    r.started = true;
    r.started_ns = SDL_GetTicksNS();
    // PHANTOMCRASH_REPORTS=1 (port-only): a capture sends automatic reports too, for checks
    const char* forced = std::getenv("PHANTOMCRASH_REPORTS");
    r.quiet = quiet && !(forced && forced[0] == '1');
    r.session = random_id();
    // the address: built in, or given for a check (PHANTOMCRASH_REPORT_URL)
    const char* url = std::getenv("PHANTOMCRASH_REPORT_URL");
    r.url = url && *url ? url : PHANTOMCRASH_REPORT_URL;
    if (char* prefs = SDL_GetPrefPath(user_folder_org, user_folder_app)) {
        r.folder = std::string(prefs) + "reports/";
        SDL_free(prefs);
    }
    (void)argc;
    (void)argv;
    if (r.url.empty()) {
        return; // nothing shows or sends
    }
    std::ifstream file(consent_path());
    std::string answer;
    file >> answer;
    r.consent = static_cast<int>(answer == "yes"  ? ReportConsent::yes
                                 : answer == "no" ? ReportConsent::no
                                                  : ReportConsent::unasked);
    SDL_Log("report: bug reports %s", answer == "yes"  ? "on"
                                      : answer == "no" ? "off"
                                                       : "not asked yet");
    if (r.consent == static_cast<int>(ReportConsent::yes)) {
        arm(r);
    }
}

void reports_finish() {
    Reporter& r = reporter();
    if (!r.started || r.finished) {
        return;
    }
    r.finished = true;
    if (!r.armed) {
        return;
    }
    watchdog_stop();
    report_worker_stop(2.0);
    // a clean exit: no unclean-exit report for this session
    std::error_code error;
    std::filesystem::remove(r.marker, error);
}

ReportConsent report_consent() {
    return static_cast<ReportConsent>(reporter().consent.load());
}

void report_consent_set(bool yes) {
    Reporter& r = reporter();
    if (r.folder.empty() || !reports_available()) {
        return;
    }
    r.consent = static_cast<int>(yes ? ReportConsent::yes : ReportConsent::no);
    std::error_code error;
    std::filesystem::create_directories(r.folder, error);
    std::ofstream(consent_path(), std::ios::trunc) << (yes ? "yes" : "no") << "\n";
    SDL_Log("report: bug reports %s", yes ? "on" : "off");
    if (yes) {
        arm(r);
    } else {
        report_outbox_clear();
    }
}

void report_set_settings(std::vector<std::pair<std::string, std::string>> settings) {
    Reporter& r = reporter();
    {
        std::lock_guard<std::mutex> lock(r.lock);
        r.settings = std::move(settings);
    }
    if (r.armed) {
        refresh_crash_context();
    }
}

void report_set_gpu(const std::string& device) {
    Reporter& r = reporter();
    {
        std::lock_guard<std::mutex> lock(r.lock);
        r.gpu = device;
    }
    if (r.armed) {
        refresh_crash_context();
    }
}

void report_note_game(int app_state, bool fighting, const std::string& rules) {
    Reporter& r = reporter();
    {
        std::lock_guard<std::mutex> lock(r.lock);
        if (r.state == app_state && r.fighting == fighting && r.rules == rules) {
            return;
        }
        r.state = app_state;
        r.fighting = fighting;
        r.rules = rules;
    }
    if (r.armed) {
        refresh_crash_context();
    }
}

bool report_send(const char* kind, const std::string& message, const std::string& fight,
                 const std::vector<std::string>& stack) {
    Reporter& r = reporter();
    if (!reports_consented() || r.quiet) {
        return false;
    }
    ReportJob job;
    {
        std::lock_guard<std::mutex> lock(r.lock);
        std::string why;
        if (!may_send(r, kind, false, why)) {
            return false;
        }
    }
    job = job_of(r, kind, message);
    job.fight = fight;
    job.stack = stack;
    SDL_Log("report: %s queued: %s", kind, scrub(message).c_str());
    report_queue(std::move(job));
    return true;
}

std::string report_send_manual(const std::string& note, const std::string& shot_png,
                               const std::string& fight) {
    Reporter& r = reporter();
    if (!reports_consented()) {
        return "Bug reports are off. Turn them on in PC CONFIG, GAME.";
    }
    {
        std::lock_guard<std::mutex> lock(r.lock);
        std::string why;
        if (!may_send(r, "manual", true, why)) {
            return why;
        }
    }
    ReportJob job = job_of(r, "manual", "");
    job.note = note.substr(0, 1000);
    if (!shot_png.empty()) {
        // a name of its own, so the panel's next picture can't take its place
        job.shot_png = r.folder + "shot-" + random_id() + ".png";
        std::error_code error;
        std::filesystem::rename(shot_png, job.shot_png, error);
        if (error) {
            job.shot_png.clear();
        }
    }
    job.fight = fight;
    SDL_Log("report: the player's own queued (%zu letters%s%s)", note.size(),
            shot_png.empty() ? "" : ", a picture", fight.empty() ? "" : ", the fight");
    report_queue(std::move(job));
    return {};
}

std::string report_shot_path() {
    return reporter().folder.empty() ? std::string() : reporter().folder + "shot.png";
}

void report_crash_on_purpose() {
    SDL_Log("report: crashing on purpose (PHANTOMCRASH_DEV_CRASH)");
    volatile int* nothing = nullptr;
    *nothing = 42;
}
