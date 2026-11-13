#include "perf_log.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr int part_count = static_cast<int>(PerfPart::count);
constexpr int pass_count = static_cast<int>(GpuPass::count);

struct PerfLog {
    double period = 10.0; // seconds between lines; 0 none
    bool started = false;
    std::uint64_t window_start = 0;
    std::uint64_t last_frame_start = 0;
    // this frame's parts, and the window's sums
    std::uint64_t frame[part_count] = {};
    std::uint64_t sums[part_count] = {};
    int frames = 0;
    std::vector<float> intervals; // ms between frame starts
    std::vector<float> work;      // ms of a frame not blocked on the GPU or the swapchain
    double work_sum = 0;          // their sum, for the time no part names
    std::vector<float> steps;     // us a world step
    std::vector<float> gpu;       // ms a frame on the GPU
    double gpu_sums[pass_count] = {};
    int gpu_frames = 0;
    int pipelines = 0;
    std::uint64_t pipeline_ns = 0;
    int sets = 0;
    int uploads = 0;
    std::uint64_t upload_bytes = 0;
    std::string swapchain = "swapchain ?";
};

PerfLog& perf_log() {
    static PerfLog log = [] {
        PerfLog out;
        if (const char* every = std::getenv("PHANTOMCRASH_PERF")) {
            out.period = std::max(0.0, std::atof(every));
        }
        out.intervals.reserve(4096);
        out.work.reserve(4096);
        out.steps.reserve(4096);
        out.gpu.reserve(4096);
        return out;
    }();
    return log;
}

// the p-th percentile of the samples (sorted in place)
float percentile(std::vector<float>& samples, float p) {
    if (samples.empty()) {
        return 0.0f;
    }
    const std::size_t at = std::min(
        samples.size() - 1, static_cast<std::size_t>(p * static_cast<float>(samples.size())));
    std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(at),
                     samples.end());
    return samples[at];
}

void keep(std::vector<float>& samples, float value) {
    if (samples.size() < 16384) {
        samples.push_back(value);
    }
}

double ms_a_frame(const PerfLog& log, PerfPart part) {
    return log.frames ? static_cast<double>(log.sums[static_cast<int>(part)]) * 1e-6 / log.frames
                      : 0.0;
}

void say(PerfLog& log, std::uint64_t now) {
    const double seconds = static_cast<double>(now - log.window_start) * 1e-9;
    const int step_count = static_cast<int>(log.steps.size());
    const float frame50 = percentile(log.intervals, 0.5f),
                frame99 = percentile(log.intervals, 0.99f);
    const float work50 = percentile(log.work, 0.5f), work99 = percentile(log.work, 0.99f);
    const float step50 = percentile(log.steps, 0.5f), step99 = percentile(log.steps, 0.99f);
    // the rest of a frame's work: the input, the sound's following, the loop itself
    double named = 0;
    for (PerfPart part :
         {PerfPart::step, PerfPart::menus, PerfPart::hud, PerfPart::ui, PerfPart::scene,
          PerfPart::record, PerfPart::present, PerfPart::uploads}) {
        named += ms_a_frame(log, part);
    }
    const double other = log.frames ? std::max(0.0, log.work_sum / log.frames - named) : 0.0;
    char gpu[256] = "gpu none";
    if (log.gpu_frames > 0) {
        const double n = log.gpu_frames;
        std::snprintf(gpu, sizeof gpu,
                      "gpu p50 %.2f p99 %.2f ms (scene %.2f composite %.2f 2d %.2f gamma %.2f "
                      "blit %.2f)",
                      percentile(log.gpu, 0.5f), percentile(log.gpu, 0.99f), log.gpu_sums[0] / n,
                      log.gpu_sums[1] / n, log.gpu_sums[2] / n, log.gpu_sums[3] / n,
                      log.gpu_sums[4] / n);
    }
    SDL_Log("perf: %d frames in %.1fs (%.0f/s), frame p50 %.2f p99 %.2f ms, cpu p50 %.2f p99 "
            "%.2f ms; %d steps, world p50 %.0f p99 %.0f us; a frame ms: steps %.2f menus %.2f "
            "hud %.2f ui %.2f scene %.2f record %.2f present %.2f other %.2f, blocked fence %.2f "
            "acquire "
            "%.2f uploads %.2f; %s; made %d pipelines (%.1f ms) %d sets, %d uploads %.1f KB; %s",
            log.frames, seconds, log.frames / std::max(seconds, 1e-9), frame50, frame99, work50,
            work99, step_count, step50, step99, ms_a_frame(log, PerfPart::step),
            ms_a_frame(log, PerfPart::menus), ms_a_frame(log, PerfPart::hud),
            ms_a_frame(log, PerfPart::ui), ms_a_frame(log, PerfPart::scene),
            ms_a_frame(log, PerfPart::record), ms_a_frame(log, PerfPart::present), other,
            ms_a_frame(log, PerfPart::fence), ms_a_frame(log, PerfPart::acquire),
            ms_a_frame(log, PerfPart::uploads), gpu, log.pipelines,
            static_cast<double>(log.pipeline_ns) * 1e-6, log.sets, log.uploads,
            static_cast<double>(log.upload_bytes) / 1024.0, log.swapchain.c_str());
    // the next window
    std::fill(std::begin(log.sums), std::end(log.sums), 0);
    std::fill(std::begin(log.gpu_sums), std::end(log.gpu_sums), 0.0);
    log.frames = log.gpu_frames = log.pipelines = log.sets = log.uploads = 0;
    log.pipeline_ns = log.upload_bytes = 0;
    log.intervals.clear();
    log.work.clear();
    log.work_sum = 0;
    log.steps.clear();
    log.gpu.clear();
    log.window_start = now;
}

} // namespace

std::uint64_t perf_now() {
    return SDL_GetTicksNS();
}

void perf_add(PerfPart part, std::uint64_t ns) {
    perf_log().frame[static_cast<int>(part)] += ns;
}

void perf_load(const char* what, std::uint64_t start) {
    SDL_Log("load: %s in %.1f ms", what, static_cast<double>(perf_now() - start) * 1e-6);
}

void perf_world_step(std::uint64_t ns) {
    keep(perf_log().steps, static_cast<float>(ns) * 1e-3f);
}

void perf_gpu(const float (&ms)[static_cast<int>(GpuPass::count)]) {
    PerfLog& log = perf_log();
    float total = 0.0f;
    for (int i = 0; i < pass_count; ++i) {
        log.gpu_sums[i] += ms[i];
        total += ms[i];
    }
    keep(log.gpu, total);
    ++log.gpu_frames;
}

void perf_pipeline_made(std::uint64_t ns) {
    ++perf_log().pipelines;
    perf_log().pipeline_ns += ns;
}

void perf_set_made() {
    ++perf_log().sets;
}

void perf_upload(std::uint64_t bytes) {
    ++perf_log().uploads;
    perf_log().upload_bytes += bytes;
}

void perf_swapchain(int frames_in_flight, const char* present_mode, int images, int width,
                    int height, int target_width, int target_height) {
    char text[160];
    std::snprintf(text, sizeof text, "%d in flight, %s, %d images, %dx%d, targets %dx%d",
                  frames_in_flight, present_mode, images, width, height, target_width,
                  target_height);
    perf_log().swapchain = text;
    SDL_Log("render: swapchain %s", text);
}

void perf_finish() {
    PerfLog& log = perf_log();
    if (log.period > 0.0 && log.frames >= 30) {
        say(log, perf_now());
    }
}

void perf_frame_end(std::uint64_t frame_start) {
    PerfLog& log = perf_log();
    const std::uint64_t now = perf_now();
    if (!log.started) {
        log.started = true;
        log.window_start = now;
        SDL_Log("load: start to the first frame in %.1f ms", static_cast<double>(now) * 1e-6);
    } else {
        keep(log.intervals, static_cast<float>(frame_start - log.last_frame_start) * 1e-6f);
    }
    log.last_frame_start = frame_start;
    // the frame's own work: its time less what it spent blocked
    const std::uint64_t blocked = log.frame[static_cast<int>(PerfPart::fence)] +
                                  log.frame[static_cast<int>(PerfPart::acquire)] +
                                  log.frame[static_cast<int>(PerfPart::capture)];
    const std::uint64_t busy = now - frame_start;
    keep(log.work, static_cast<float>(busy - std::min(busy, blocked)) * 1e-6f);
    log.work_sum += static_cast<double>(busy - std::min(busy, blocked)) * 1e-6;
    for (int i = 0; i < part_count; ++i) {
        log.sums[i] += log.frame[i];
        log.frame[i] = 0;
    }
    ++log.frames;
    if (log.period > 0.0 && static_cast<double>(now - log.window_start) * 1e-9 >= log.period) {
        say(log, now);
    }
}
