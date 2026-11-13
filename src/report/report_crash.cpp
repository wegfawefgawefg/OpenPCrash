// What every platform's crash handler shares: the paths and the context in fixed buffers,
// std::terminate's last words, and the crash file read back on the next start.
#include "report/report_crash.hpp"

#include "report/report_internal.hpp"
#include "run_log.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>

namespace crash {

char text_path[1024];
char fight_path[1024];
char log_path[1024];
char contexts[2][context_size];
std::atomic<int> context_at{0};
char last_words[512];
std::atomic<bool> crashing{false};

std::size_t text_size(const char* text) {
    std::size_t size = 0;
    while (text[size]) {
        ++size;
    }
    return size;
}

void format_hex(char* out, std::uintptr_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    out[0] = '0';
    out[1] = 'x';
    char reversed[2 * sizeof(value)];
    int count = 0;
    do {
        reversed[count++] = digits[value & 15];
        value >>= 4;
    } while (value);
    for (int i = 0; i < count; ++i) {
        out[2 + i] = reversed[count - 1 - i];
    }
    out[2 + count] = '\0';
}

const char* context_text(std::size_t& size) {
    const int at = context_at.load(std::memory_order_acquire);
    size = text_size(contexts[at]);
    return contexts[at];
}

namespace {

void on_terminate() {
    if (std::exception_ptr thrown = std::current_exception()) {
        try {
            std::rethrow_exception(thrown);
        } catch (const std::exception& error) {
            std::strncpy(last_words, error.what(), sizeof last_words - 1);
        } catch (...) {
            std::strncpy(last_words, "an exception that isn't a std::exception",
                         sizeof last_words - 1);
        }
    } else {
        std::strncpy(last_words, "std::terminate", sizeof last_words - 1);
    }
    std::abort();
}

} // namespace

void set_terminate_handler() {
    std::set_terminate(on_terminate);
}

void set_paths(const char* text, const char* fight) {
    std::strncpy(text_path, text, sizeof text_path - 1);
    std::strncpy(fight_path, fight, sizeof fight_path - 1);
    std::strncpy(log_path, run_log_path().c_str(), sizeof log_path - 1);
}

} // namespace crash

void crash_context_set(const std::string& json) {
    const int next = 1 - crash::context_at.load();
    const std::size_t size = std::min(json.size(), crash::context_size - 1);
    std::memcpy(crash::contexts[next], json.data(), size);
    crash::contexts[next][size] = '\0';
    crash::context_at.store(next, std::memory_order_release);
}

bool crash_file_read(const std::string& path, CrashFile& out) {
    std::ifstream file(path, std::ios::binary);
    std::string line;
    if (!file || !std::getline(file, line) || line != "openpcrash-crash 1") {
        return false;
    }
    bool stack = false;
    while (std::getline(file, line)) {
        if (stack) {
            if (out.stack.size() < 64) {
                out.stack.push_back(line.substr(0, 200));
            }
        } else if (line == "stack") {
            stack = true;
        } else if (line.rfind("reason ", 0) == 0) {
            out.reason = line.substr(7);
        } else if (line.rfind("log ", 0) == 0) {
            out.log_path = line.substr(4);
        } else if (line.rfind("context ", 0) == 0) {
            out.context = line.substr(8);
        }
    }
    return true;
}
