// Linux and macOS: signal handlers on a stack of their own (a stack overflow has none left),
// backtrace() for the stack and dladdr() for the module each address is in, after Mans In My
// House's. The handler writes the crash file and the fight's record, then lets the signal end the
// process as before. It allocates nothing and takes no locks.
#include "report/report_crash.hpp"
#include "report/report_fight.hpp"
#include "report/report_internal.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

namespace {

constexpr int crash_signals[]{SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP};
constexpr int sample_signal = SIGUSR2; // the watchdog asking the main thread for its stack
pthread_t main_thread;
void* samples[crash::max_frames];
std::atomic<int> sample_count{-1};
thread_local bool writing = false;

// each thread's alternate signal stack, given back as the thread ends
struct ThreadGuard {
    void* stack = nullptr;
    ~ThreadGuard() {
        if (stack) {
            stack_t off{};
            off.ss_flags = SS_DISABLE;
            sigaltstack(&off, nullptr);
            std::free(stack);
        }
    }
};
thread_local ThreadGuard thread_guard;

void put(int fd, const void* data, std::size_t size) {
    const char* text = static_cast<const char*>(data);
    while (size > 0) {
        const ssize_t sent = write(fd, text, size);
        if (sent <= 0) {
            return;
        }
        text += sent;
        size -= static_cast<std::size_t>(sent);
    }
}

void put(int fd, const char* text) {
    put(fd, text, crash::text_size(text));
}

void put_hex(int fd, std::uintptr_t value) {
    char text[24];
    crash::format_hex(text, value);
    put(fd, text);
}

const char* base_name(const char* path) {
    const char* name = path;
    for (const char* at = path; *at; ++at) {
        if (*at == '/') {
            name = at + 1;
        }
    }
    return name;
}

// "0x7f12a4c0 phantomcrash+0x1f3a"
void put_frame(int fd, void* address) {
    put_hex(fd, reinterpret_cast<std::uintptr_t>(address));
    Dl_info info{};
    if (dladdr(address, &info) && info.dli_fname && info.dli_fbase) {
        put(fd, " ");
        put(fd, base_name(info.dli_fname));
        put(fd, "+");
        put_hex(fd, reinterpret_cast<std::uintptr_t>(address) -
                        reinterpret_cast<std::uintptr_t>(info.dli_fbase));
    }
    put(fd, "\n");
}

const char* signal_name(int sig) {
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV (bad memory access)";
    case SIGBUS:
        return "SIGBUS (bad memory access)";
    case SIGFPE:
        return "SIGFPE (arithmetic error)";
    case SIGILL:
        return "SIGILL (illegal instruction)";
    case SIGABRT:
        return "SIGABRT (aborted)";
    case SIGTRAP:
        return "SIGTRAP (trap)";
    default:
        return "signal";
    }
}

std::uintptr_t fault_pc(void* context) {
    const auto* uc = static_cast<const ucontext_t*>(context);
    if (!uc) {
        return 0;
    }
#if defined(__APPLE__) && defined(__x86_64__)
    return uc->uc_mcontext->__ss.__rip;
#elif defined(__APPLE__) && defined(__aarch64__)
    return uc->uc_mcontext->__ss.__pc;
#elif defined(__linux__) && defined(__x86_64__)
    return static_cast<std::uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
#elif defined(__linux__) && defined(__aarch64__)
    return uc->uc_mcontext.pc;
#else
    return 0;
#endif
}

void on_crash(int sig, siginfo_t* info, void* context) {
    if (crash::crashing.exchange(true)) {
        // another thread is writing the crash file: it ends the process when done
        if (!writing) {
            for (int i = 0; i < 100; ++i) {
                const timespec tenth{0, 100 * 1000 * 1000};
                nanosleep(&tenth, nullptr);
            }
        }
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }
    writing = true;
    const int fd = open(crash::text_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        put(fd, "openpcrash-crash 1\nreason ");
        put(fd, signal_name(sig));
        if (sig == SIGSEGV || sig == SIGBUS) {
            put(fd, " at ");
            put_hex(fd, reinterpret_cast<std::uintptr_t>(info ? info->si_addr : nullptr));
        }
        if (crash::last_words[0]) {
            put(fd, ": ");
            put(fd, crash::last_words);
        }
        put(fd, "\nlog ");
        put(fd, crash::log_path);
        put(fd, "\ncontext ");
        std::size_t size = 0;
        const char* context_json = crash::context_text(size);
        put(fd, context_json, size);
        put(fd, "\nstack\n");
        if (const std::uintptr_t pc = fault_pc(context)) {
            put(fd, "pc ");
            put_frame(fd, reinterpret_cast<void*>(pc));
        }
        void* frames[crash::max_frames];
        const int count = backtrace(frames, crash::max_frames);
        for (int i = 0; i < count; ++i) {
            put_frame(fd, frames[i]);
        }
        close(fd);
    }
    // the fight's record beside it
    const int fight = open(crash::fight_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fight >= 0) {
        int to = fight;
        report_fight_write(&to, [](void* at, const void* data, std::size_t size) {
            put(*static_cast<int*>(at), data, size);
        });
        const off_t written = lseek(fight, 0, SEEK_END);
        close(fight);
        if (written <= 0) {
            unlink(crash::fight_path);
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

void on_sample(int) {
    sample_count.store(backtrace(samples, crash::max_frames));
}

} // namespace

void crash_handlers_install(const std::string& text_path, const std::string& fight_path) {
    crash::set_paths(text_path.c_str(), fight_path.c_str());
    main_thread = pthread_self();
    // backtrace() loads its unwinder the first time: not in a handler
    void* warm[4];
    backtrace(warm, 4);
    static char alternate[256 * 1024];
    stack_t stack{};
    stack.ss_sp = alternate;
    stack.ss_size = sizeof alternate;
    sigaltstack(&stack, nullptr);
    struct sigaction action {};
    action.sa_sigaction = on_crash;
    // not SA_RESETHAND: a second thread crashing meanwhile waits for the first
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (const int sig : crash_signals) {
        sigaction(sig, &action, nullptr);
    }
    struct sigaction sample {};
    sample.sa_handler = on_sample;
    sample.sa_flags = SA_RESTART;
    sigemptyset(&sample.sa_mask);
    sigaction(sample_signal, &sample, nullptr);
    crash::set_terminate_handler();
}

void crash_guard_thread() {
    if (thread_guard.stack) {
        return;
    }
    constexpr std::size_t size = 128 * 1024; // backtrace() and dladdr() need some
    void* stack = std::malloc(size);
    if (!stack) {
        return;
    }
    stack_t alternate{};
    alternate.ss_sp = stack;
    alternate.ss_size = size;
    if (sigaltstack(&alternate, nullptr) == 0) {
        thread_guard.stack = stack;
    } else {
        std::free(stack);
    }
}

std::vector<std::string> crash_main_stack() {
    sample_count.store(-1);
    if (pthread_kill(main_thread, sample_signal) != 0) {
        return {};
    }
    for (int i = 0; i < 50 && sample_count.load() < 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const int count = sample_count.load();
    std::vector<std::string> lines;
    // the first two are the handler and the signal's trampoline
    for (int i = 2; i < count; ++i) {
        Dl_info info{};
        char absolute[24], offset[24];
        crash::format_hex(absolute, reinterpret_cast<std::uintptr_t>(samples[i]));
        std::string line = absolute;
        if (dladdr(samples[i], &info) && info.dli_fname && info.dli_fbase) {
            crash::format_hex(offset, reinterpret_cast<std::uintptr_t>(samples[i]) -
                                          reinterpret_cast<std::uintptr_t>(info.dli_fbase));
            line += std::string(" ") + base_name(info.dli_fname) + "+" + offset;
        }
        lines.push_back(line);
    }
    return lines;
}
