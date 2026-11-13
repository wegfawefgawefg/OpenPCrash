// The watchdog: a thread that looks twice a second at when the main loop last finished a frame. One
// that takes far longer than it may (15 seconds, or what report_busy allows for a load) is a hang
// report, once a session, with the main thread's stack as it is then and the fight's record. A
// machine put to sleep isn't a hang: the watchdog's own wait running long starts the count again.
#include "report/report_fight.hpp"
#include "report/report_internal.hpp"
#include "report/reports.hpp"

#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

constexpr double frame_most = 15.0; // seconds a frame may take

struct Watchdog {
    std::mutex lock;
    std::condition_variable wake;
    std::thread thread;
    bool running = false, stopping = false;
    std::atomic<std::uint64_t> last_frame{0}; // SDL ticks, ns; 0 not yet
    std::atomic<std::uint64_t> allowed_ns{0}; // the next frame's allowance past frame_most
};

Watchdog& watchdog() {
    static Watchdog* made = new Watchdog;
    return *made;
}

void run() {
    Watchdog& w = watchdog();
    crash_guard_thread();
    bool reported = false;
    std::uint64_t woke = SDL_GetTicksNS();
    std::unique_lock<std::mutex> lock(w.lock);
    while (!w.stopping) {
        w.wake.wait_for(lock, std::chrono::milliseconds(500));
        const std::uint64_t now = SDL_GetTicksNS();
        const bool slept = now - woke > 5'000'000'000ull; // the machine was asleep
        woke = now;
        const std::uint64_t last = w.last_frame.load();
        if (slept && last != 0) {
            w.last_frame = now;
            continue;
        }
        const std::uint64_t most =
            static_cast<std::uint64_t>(frame_most * 1e9) + w.allowed_ns.load();
        if (reported || last == 0 || now - last < most) {
            continue;
        }
        reported = true;
        lock.unlock();
        const double seconds = static_cast<double>(now - last) * 1e-9;
        SDL_Log("report: no frame for %.0f s: a hang", seconds);
        // the main thread's stack, and the fight as it stood (stuck, so it stands still)
        report_send("hang",
                    "no frame for " + std::to_string(static_cast<int>(seconds)) + " seconds",
                    report_fight_bytes(nullptr), crash_main_stack());
        lock.lock();
    }
}

} // namespace

void report_frame() {
    Watchdog& w = watchdog();
    w.last_frame = SDL_GetTicksNS();
    w.allowed_ns = 0;
}

void report_busy(double seconds) {
    watchdog().allowed_ns = static_cast<std::uint64_t>(seconds * 1e9);
}

void watchdog_start() {
    Watchdog& w = watchdog();
    if (w.running) {
        return;
    }
    w.running = true;
    w.thread = std::thread(run);
}

void watchdog_stop() {
    Watchdog& w = watchdog();
    if (!w.running) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(w.lock);
        w.stopping = true;
    }
    w.wake.notify_one();
    w.thread.join();
    w.running = false;
}
