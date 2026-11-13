#pragma once
// The frame timings in the run log (docs/performance.md): every 10 s a `perf:` line, as the audio's
// health line, with the frame time's p50 and p99 (whole, and without the time blocked on the GPU,
// the swapchain and the frame limit), the world steps' p50 and p99, the CPU parts of a frame (the
// menus, the HUD's 2D, the 2D handed to the renderer, the 3D's draw list, the Vulkan record), the
// time blocked on the last frame's fence and on the swapchain, and the GPU's time per pass from
// Vulkan timestamp queries. The swapchain's line (frames in flight, present mode, images, size)
// comes when it is made. A player's run log carries them, so a slow machine's report says where.
// Cheap: a clock read per part, and one sort of the window's frame times per line.
// PHANTOMCRASH_PERF=N says it every N seconds instead (1 for a short capture), 0 never.
#include <cstdint>

// a frame's CPU parts, summed over the frame
enum class PerfPart : std::uint8_t {
    step,    // a battle step whole: the world, the presentation's steps, the sounds
    world,   // step_world alone
    menus,   // the front end's updates (their logic and their 2D)
    hud,     // the fight's HUD 2D (battle_run_hud)
    ui,      // ui_present: the 2D list handed to the renderer
    scene,   // the 3D's draw list (the stage, the machines, the effects)
    fence,   // blocked on the last frame's fence (the GPU)
    uploads, // the pending uploads (loads) run and waited for
    acquire, // blocked on the swapchain for an image
    record,  // the frame's commands written (the sprites' vertices among them)
    present, // submit and present
    capture, // a --capture's readback and PNG (headless runs)
    count
};

// the GPU's passes, timed by timestamps between them
enum class GpuPass : std::uint8_t {
    scene,     // the scene pass: pass 0's 2D, passes 1..10, the backdrop copies, the depth probe
    composite, // pass 11: the big screens' copy, the blur and dof or the tinted copy, the cockpit
    overlay,   // the 2D of passes 12..15, pass 13's meshes, the post pass
    gamma,     // the capture texture's copy and the gamma ramp
    blit,      // the output to the swapchain image
    count
};

std::uint64_t perf_now(); // nanoseconds
void perf_add(PerfPart part, std::uint64_t ns);
// times its scope into a part
struct PerfScope {
    PerfPart part;
    std::uint64_t start;
    explicit PerfScope(PerfPart p) : part(p), start(perf_now()) {
    }
    ~PerfScope() {
        perf_add(part, perf_now() - start);
    }
    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;
};
// a load's time, said in the run log ("load: WHAT in X ms"); PerfLoad says its scope's
void perf_load(const char* what, std::uint64_t start);
struct PerfLoad {
    const char* what;
    std::uint64_t start;
    explicit PerfLoad(const char* w) : what(w), start(perf_now()) {
    }
    ~PerfLoad() {
        perf_load(what, start);
    }
    PerfLoad(const PerfLoad&) = delete;
    PerfLoad& operator=(const PerfLoad&) = delete;
};
// one world step's time (its p50 and p99 go in the line)
void perf_world_step(std::uint64_t ns);
// a frame's GPU time per pass, milliseconds
void perf_gpu(const float (&ms)[static_cast<int>(GpuPass::count)]);
// things made inside frames: pipelines (with their time), texture descriptor sets, uploads
void perf_pipeline_made(std::uint64_t ns);
void perf_set_made();
void perf_upload(std::uint64_t bytes);
// the swapchain as made, said in the log at once and kept for the perf line
void perf_swapchain(int frames_in_flight, const char* present_mode, int images, int width,
                    int height, int target_width, int target_height);
// a frame ends (pc_frame_end, before the frame limit's wait): its time from frame_start, and the
// perf line when it is due
void perf_frame_end(std::uint64_t frame_start);
// the run ends: the window so far said, when it has a few frames
void perf_finish();
