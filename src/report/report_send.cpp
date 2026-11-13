// The worker: reports are made and sent one at a time off the main thread, and one that can't go
// (offline, the server down) waits in reports/outbox/ for a later start, tried three times at most,
// as do last time's crash files. Nothing here can stop the game: every failure is a log line.
#include "report/report_fight.hpp"
#include "report/report_internal.hpp"
#include "report/reports.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr int outbox_most = 10; // reports kept for later
constexpr int tries_most = 3;   // starts that try one
constexpr int crashes_most = 3; // last time's crash files sent, the newest

struct Worker {
    std::mutex lock;
    std::condition_variable wake, finished;
    std::deque<ReportJob> queue;
    bool stopping = false, done = false, running = false;
    std::chrono::steady_clock::time_point deadline;
    std::thread thread;
};

Worker& worker() {
    static Worker* made = new Worker;
    return *made;
}

std::string outbox() {
    return reports_folder() + "outbox/";
}

// the files in a folder whose names start so and end so, oldest first
std::vector<fs::path> files_in(const std::string& folder, const std::string& prefix,
                               const std::string& extension) {
    std::vector<fs::path> files;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(folder, error)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file(error) && name.rfind(prefix, 0) == 0 &&
            entry.path().extension() == extension) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        std::error_code e;
        return fs::last_write_time(a, e) < fs::last_write_time(b, e);
    });
    return files;
}

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// the last bytes of a log, from its first whole line, a line each, scrubbed
nlohmann::json log_tail(const std::string& path) {
    nlohmann::json lines = nlohmann::json::array();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        lines.push_back("(no run log)");
        return lines;
    }
    const std::streamoff size = file.tellg();
    const std::streamoff from =
        std::max<std::streamoff>(0, size - static_cast<std::streamoff>(report_log_bytes));
    file.seekg(from);
    std::string text(static_cast<std::size_t>(size - from), '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    std::istringstream in(text);
    std::string line;
    bool cut = from > 0; // the first line is a part
    while (std::getline(in, line)) {
        if (!cut) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(scrub(line.substr(0, 400)));
        }
        cut = false;
    }
    if (lines.size() > 2000) { // the server's most
        lines.erase(lines.begin(), lines.end() - 2000);
    }
    return lines;
}

// the job as the server takes it
nlohmann::json report_of(const ReportJob& job) {
    nlohmann::json report;
    report["version"] = 1;
    report["kind"] = job.kind;
    report["id"] = random_id();
    const nlohmann::json& c = job.context;
    report["session"] = c.value("session", std::string("0000000000000000"));
    report["message"] = scrub(job.message).substr(0, 500);
    if (!job.note.empty()) {
        report["note"] = job.note.substr(0, 1000);
    }
    report["uptime"] = std::max<long long>(0, job.uptime);
    for (const char* part : {"build", "system", "game", "settings"}) {
        report[part] = c.contains(part) ? c[part] : nlohmann::json::object();
    }
    nlohmann::json stack = nlohmann::json::array();
    for (const std::string& line : job.stack) {
        if (stack.size() < 64) {
            stack.push_back(scrub(line).substr(0, 200));
        }
    }
    report["stack"] = stack;
    report["log"] = log_tail(job.log_path);
    if (!job.fight.empty()) {
        const std::string packed = report_fight_pack(job.fight);
        if (!packed.empty() && packed.size() <= report_replay_limit) {
            report["replay"] = base64(packed);
            report["replay_bytes"] = static_cast<long long>(job.fight.size());
        } else {
            report["message"] = report["message"].get<std::string>() +
                                " (replay left out: " + std::to_string(packed.size() / 1024) +
                                " KiB)";
        }
    }
    if (!job.shot_png.empty()) {
        const std::string shot = report_shot_jpeg(job.shot_png);
        std::error_code error;
        fs::remove(job.shot_png, error);
        if (!shot.empty()) {
            report["shot"] = shot;
        }
    }
    return report;
}

// the JSON as sent: the log cut from the front until the rest fits
std::string body_of(nlohmann::json& report) {
    const auto dump = [&] {
        return report.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    };
    nlohmann::json big = nlohmann::json::object();
    for (const char* part : {"shot", "replay"}) {
        if (report.contains(part)) {
            big[part] = std::move(report[part]);
            report.erase(part);
        }
    }
    std::string body = dump();
    while (body.size() > report_json_limit && !report["log"].empty()) {
        auto& log = report["log"];
        log.erase(log.begin(),
                  log.begin() + static_cast<long>(std::max<std::size_t>(1, log.size() / 4)));
        body = dump();
    }
    for (auto& [part, value] : big.items()) {
        report[part] = std::move(value);
    }
    return big.empty() ? body : dump();
}

// sent, or nothing to be done about it: either way not kept
bool settled(int status) {
    return status != 0 && status != 408 && status != 429 && status < 500;
}

int post(const std::string& body, const std::string& kind) {
    std::string error;
    const int status = post_report(report_url(), body, 20.0, error);
    if (status >= 200 && status < 300) {
        SDL_Log("report: %s sent (%zu KiB)", kind.c_str(), body.size() / 1024);
    } else {
        SDL_Log("report: %s not sent: %s", kind.c_str(),
                status ? ("HTTP " + std::to_string(status)).c_str() : error.c_str());
    }
    return status;
}

// a report kept for a later start: outbox/<id>.<tries>.json
void keep_for_later(const std::string& body, const std::string& id, int tries) {
    if (reports_folder().empty() || tries >= tries_most) {
        SDL_Log("report: tried %d times; dropped", tries);
        return;
    }
    if (static_cast<int>(files_in(outbox(), "", ".json").size()) >= outbox_most) {
        SDL_Log("report: the outbox is full; this one is dropped");
        return;
    }
    std::ofstream(outbox() + id + "." + std::to_string(tries) + ".json", std::ios::binary) << body;
}

void send_or_keep(const ReportJob& job) {
    nlohmann::json report = report_of(job);
    const std::string body = body_of(report);
    if (!settled(post(body, job.kind))) {
        keep_for_later(body, report["id"].get<std::string>(), 1);
    }
}

// another copy of the game running now with that process id
bool still_running(unsigned long pid) {
#ifdef _WIN32
    if (pid == GetCurrentProcessId()) {
        return false;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    return pid > 0 && pid != static_cast<unsigned long>(getpid()) &&
           (kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
#endif
}

// last time's crash files and markers: a report each, the files removed
void collect(const std::string& own_session) {
    const std::string folder = reports_folder();
    std::error_code error;
    // a session that left its marker and no crash file ended where no handler saw
    for (const fs::path& marker : files_in(folder, "running-", ".txt")) {
        std::ifstream file(marker);
        std::string line, session, log;
        unsigned long pid = 0;
        while (std::getline(file, line)) {
            if (line.rfind("pid ", 0) == 0) {
                pid = std::strtoul(line.c_str() + 4, nullptr, 10);
            } else if (line.rfind("session ", 0) == 0) {
                session = line.substr(8);
            } else if (line.rfind("log ", 0) == 0) {
                log = line.substr(4);
            }
        }
        file.close();
        if (session.empty() || session == own_session || still_running(pid)) {
            continue;
        }
        fs::remove(marker, error);
        if (fs::exists(folder + "crash-" + session + ".txt", error)) {
            continue; // the crash file says it all
        }
        ReportJob job;
        job.kind = "crash";
        job.message = "unclean exit: the last session ended with no crash file and no clean exit "
                      "(ended on the spot, killed, or a crash no handler saw)";
        job.context = report_context();
        job.context["session"] = session;
        job.log_path = log;
        job.fight = read_file(folder + "crash-" + session + ".fight");
        fs::remove(folder + "crash-" + session + ".fight", error);
        send_or_keep(job);
    }
    std::vector<fs::path> crashes = files_in(folder, "crash-", ".txt");
    for (std::size_t i = 0; i < crashes.size(); ++i) {
        const std::string name = crashes[i].stem().string(); // crash-<session>
        if (name == "crash-" + own_session) {
            continue;
        }
        const std::string fight_path = folder + name + ".fight";
        CrashFile crash;
        if (i + crashes_most >= crashes.size() && crash_file_read(crashes[i].string(), crash)) {
            ReportJob job;
            job.kind = "crash";
            job.message = crash.reason;
            job.stack = crash.stack;
            job.context = nlohmann::json::parse(crash.context, nullptr, false);
            if (!job.context.is_object()) {
                job.context = report_context();
            }
            job.log_path = crash.log_path;
            job.fight = read_file(fight_path);
            SDL_Log("report: last time's crash (%s) goes now", scrub(crash.reason).c_str());
            send_or_keep(job);
        }
        fs::remove(crashes[i], error);
        fs::remove(fight_path, error);
    }
}

// what waits in the outbox, tried again (each counts a try)
void send_outbox() {
    std::error_code error;
    const auto month = fs::file_time_type::clock::now() - std::chrono::hours(24 * 30);
    for (const fs::path& path : files_in(outbox(), "", ".json")) {
        const std::string stem = path.stem().string(); // <id>.<tries>
        const std::size_t dot = stem.rfind('.');
        const int tries = dot == std::string::npos ? tries_most : std::atoi(stem.c_str() + dot + 1);
        const bool old = fs::last_write_time(path, error) < month;
        const std::string body = read_file(path.string());
        fs::remove(path, error);
        if (old || body.empty()) {
            continue;
        }
        if (!settled(post(body, "kept report"))) {
            keep_for_later(body, stem.substr(0, dot), tries + 1);
            return; // still offline: the rest wait too
        }
    }
}

void run(std::string own_session) {
    Worker& w = worker();
    crash_guard_thread();
    // last time's and the outbox: not from a capture (left for a start that sends)
    if (!reports_quiet()) {
        collect(own_session);
        send_outbox();
    }
    for (;;) {
        std::unique_lock<std::mutex> lock(w.lock);
        w.wake.wait(lock, [&] { return !w.queue.empty() || w.stopping; });
        if (w.queue.empty()) {
            break;
        }
        ReportJob job = std::move(w.queue.front());
        w.queue.pop_front();
        const bool late = w.stopping && std::chrono::steady_clock::now() > w.deadline;
        lock.unlock();
        if (late) {
            // no time to send: kept for the next start
            nlohmann::json report = report_of(job);
            keep_for_later(body_of(report), report["id"].get<std::string>(), 0);
            continue;
        }
        if (reports_consented()) {
            send_or_keep(job);
        }
    }
    std::lock_guard<std::mutex> lock(w.lock);
    w.done = true;
    w.finished.notify_all();
}

} // namespace

void report_worker_start() {
    Worker& w = worker();
    if (w.running) {
        return;
    }
    w.running = true;
    nlohmann::json context = report_context();
    w.thread = std::thread(run, context.value("session", std::string()));
}

void report_queue(ReportJob job) {
    Worker& w = worker();
    std::lock_guard<std::mutex> lock(w.lock);
    if (w.running && !w.done && w.queue.size() < 8) {
        w.queue.push_back(std::move(job));
    }
    w.wake.notify_one();
}

void report_worker_stop(double seconds) {
    Worker& w = worker();
    if (!w.running) {
        return;
    }
    std::unique_lock<std::mutex> lock(w.lock);
    w.stopping = true;
    w.deadline = std::chrono::steady_clock::now() +
                 std::chrono::milliseconds(static_cast<int>(seconds * 1000));
    w.wake.notify_one();
    const bool done =
        w.finished.wait_for(lock, std::chrono::milliseconds(static_cast<int>(seconds * 1000) + 500),
                            [&] { return w.done; });
    lock.unlock();
    // one still sending when the time's up is left to the end of the process
    if (done) {
        w.thread.join();
    } else {
        w.thread.detach();
    }
    w.running = false;
}

void report_outbox_clear() {
    std::error_code error;
    for (const fs::path& path : files_in(outbox(), "", ".json")) {
        fs::remove(path, error);
    }
}
