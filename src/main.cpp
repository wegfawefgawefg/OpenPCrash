// Phantom Crash on PC: init, the run, shutdown. The game boots into the original's front end
// (src/screens, the application states), whose loop is src/front_end_run.cpp: fixed 60 Hz steps
// with a catch-up cap, a fight drawn with an interpolation alpha (AGENTS.md, "Simulation").
// --viewer runs the stage viewer, a battle on one stage with player 1's fighter and the chase
// camera, instead.
#include "audio/audio.hpp"
#include "build_info.hpp"
#include "cli.hpp"
#include "cook/data_hash.hpp"
#include "cook/pack_library.hpp"
#include "front_end_run.hpp"
#include "input/bindings.hpp"
#include "input/devices.hpp"
#include "input/virtual_pads.hpp"
#include "render/renderer.hpp"
#include "report/reports.hpp"
#include "run_log.hpp"
#include "screens/pc/pc_settings.hpp"
#include "setup/setup_job.hpp"
#include "setup/setup_screen.hpp"
#include "state.hpp"
#include "talk/talk_player.hpp"
#include "user_folder.hpp"
#include "viewer_run.hpp"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <gubsy/app.hpp>
#include <gubsy/runtime.hpp>

int main(int argc, char** argv) {
    // init state
    State state;
    if (!parse_cli(argc, argv, state.options)) {
        return 2;
    }
    // the conversation debug player has its own loop (src/talk/talk_player.hpp)
    if (!state.options.talk.empty()) {
        return run_talk_player(state.options);
    }

    // init the run log (src/run_log.hpp): this run's SDL_Log lines in a file in the per-user folder
    SDL_SetAppMetadata("OpenPCrash", build_info().version, "org.openpcrash.OpenPCrash");
    run_log_start(argc, argv);
    SDL_Log("%s", build_label().c_str());
    // the bug reports, with the player's yes (src/report): none from a capture
    reports_start(argc, argv, state.options.frames > 0);

    // init window (and PHANTOMCRASH_PADS's virtual pads, src/input/virtual_pads.hpp)
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    virtual_pads_attach();
    // no Vulkan (Windows without a Vulkan driver): said plainly, before the setup or the window
    if (!vulkan_loader_check()) {
        report_send("renderer", "no Vulkan loader");
        reports_finish();
        run_log_stop();
        SDL_Quit();
        return 1;
    }
    // first run: no data/ yet, so the setup screen reads the player's own disc into it
    // (src/setup); a quit there ends the run
    if (!setup_data_ready(state.options.data_dir) && run_setup_screen(state.options) != 0) {
        reports_finish();
        run_log_stop();
        SDL_Quit();
        return 0;
    }
    SDL_Window* window = SDL_CreateWindow("OpenPCrash", state.options.width,
                                          state.options.height, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "window: %s\n", SDL_GetError());
        report_send("startup", std::string("window: ") + SDL_GetError());
        reports_finish();
        SDL_Quit();
        return 1;
    }

    // init renderer, open the packs (cooking data/ on first run, docs/assets.md)
    Renderer renderer;
    PackLibrary packs;
    std::string error;
    const bool drawing = renderer_init(renderer, window, error);
    report_set_gpu(renderer.device);
    if (!drawing || !packs_start(packs, state.options.data_dir, state.options.cooked_dir, error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        report_send(drawing ? "startup" : "renderer", "start: " + error);
        reports_finish();
        renderer_shutdown(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    data_hash_print(packs); // the gameplay data this run plays on (cook/data_hash.hpp)

    // init audio (src/audio, docs/audio.md)
    AudioPtr audio = init_audio(state.options.data_dir);

    // init input: gubsy's devices and bindings to each player's pad record (docs/input.md); its
    // files in the user's folder (PHANTOMCRASH_GUBSY_ROOT overrides it, for headless runs)
    GubsyRuntime gubsy;
    GubsyAppConfig gubsy_config;
    gubsy_config.window_title = "OpenPCrash";
    if (const char* root = std::getenv("PHANTOMCRASH_GUBSY_ROOT")) {
        gubsy_config.data_root = root;
    } else if (char* prefs = SDL_GetPrefPath(user_folder_org, user_folder_app)) {
        gubsy_config.data_root = std::string(prefs) + "gubsy";
        SDL_free(prefs);
    }
    if (!init_gubsy_runtime(gubsy, gubsy_config)) {
        std::fprintf(stderr, "start: gubsy didn't start\n");
    }
    register_input(gubsy);
    if (state.options.no_dialogue >= 0) {
        story_talk_settings_write(gubsy, state.options.no_dialogue == 1);
    }
    InputDevices devices;
    pad_calibrations_load(devices.calibrations, gubsy); // STICK CALIBRATION's, by kind of pad
    seats_alone(state.seats);

    // init the PC settings (docs/settings.md): read from gubsy's files, applied to the window,
    // the renderer and the sound
    PcHost pc;
    pc.gubsy = &gubsy;
    pc.window = window;
    pc.renderer = &renderer;
    pc.audio = audio.get();
    pc.calibrations = &devices.calibrations;
    pc.base_w = state.options.width;
    pc.base_h = state.options.height;
    pc_settings_load(pc);
    pc_settings_apply(pc, true);

    // run the game: the front end, or the stage viewer
    const int status =
        state.options.viewer && state.options.vs <= 0 // --vs's --stage is the round's area
            ? run_viewer(state, window, renderer, packs, audio.get(), gubsy, devices, pc)
            : run_front_end(state, window, renderer, packs, audio.get(), gubsy, devices, pc);

    // shutdown
    cleanup_gubsy_runtime(gubsy);
    audio.reset();
    renderer_shutdown(renderer);
    SDL_DestroyWindow(window);
    reports_finish();
    run_log_stop();
    SDL_Quit();
    return status;
}
