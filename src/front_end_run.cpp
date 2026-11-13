// The front end's loop (src/front_end_run.hpp) in plain paragraphs: calc frame time, pump input,
// run fixed steps (front_end_steps.cpp), draw frame, frame end. Every pass draws and presents. A
// fight draws with an interpolation alpha, up to the frame limit (FRAME LIMIT MAX: frame_ceiling)
// or the display with v-sync on; anything else (the menus, the movies, a paused fight) at the
// display's refresh: FIFO paces it with v-sync on, and with it off the frame's end waits out the
// rest of a refresh (pc_frame_seconds); FRAME LIMIT below that holds either.
#include "front_end_run.hpp"

#include "battle/battle_data.hpp"
#include "input/devices.hpp"
#include "input/menu_input.hpp"
#include "input/virtual_pads.hpp"
#include "render/renderer.hpp"
#include "report/reports.hpp"
#include "screens/pc/pc_settings.hpp"
#include "story/rumble.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

// init the front end: the headless runs' scripts, the world's generator, the 2D, the application
// states, then --vs's round or --state's entry; 0 when it started, else the run's exit status
int front_end_start(FrontEndRun& run) {
    State& state = run.state;
    FrontEnd& fe = run.fe;
    Ui& ui = run.ui;
    std::string& error = run.error;
    fe.pc = &run.pc;
    if (!state.options.battle_script.empty() &&
        !load_input_script(run.battle_script, state.options.battle_script, error)) {
        std::fprintf(stderr, "battle script: %s\n", error.c_str());
        return 2;
    }
    // --route FILE: a headless run's presses by state (input/dev_route.hpp)
    if (!state.options.route.empty() && !load_dev_route(run.route, state.options.route, error)) {
        std::fprintf(stderr, "route: %s\n", error.c_str());
        return 2;
    }
    run.route.shot_dir = state.options.capture.substr(0, state.options.capture.rfind('/') + 1);
    // the world's generator: its table, and the index retail seeds from the clock at boot (a
    // capture starts at 0 so it repeats; --seed sets it)
    if (!load_rand_table(run.packs.core, error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        report_send("startup", "start: " + error);
        return 1;
    }
    const int seed = state.options.seed >= 0 ? state.options.seed
                     : state.options.frames > 0
                         ? 0
                         : static_cast<int>(SDL_GetTicksNS() % rand_table_size);
    state.world.random.index = seed % rand_table_size;
    if (!init_ui(ui, run.renderer, run.packs, run.audio, state.options.data_dir, error) ||
        !init_front_end(fe, ui, state.world, state.machine, error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        report_send("startup", "start: " + error);
        return 1;
    }
    fe.seats = &state.seats;
    if (state.options.vs > 0) {
        // --vs N: one VS round straight away (state 6 with vs_mode), seat 0 on the keyboard and
        // the rest on the pads plugged in (PHANTOMCRASH_PADS for headless runs), the stock PLAIN
        // builds in turn; then the front end goes on from its answer
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            input_event(run.devices, run.gubsy, event);
        }
        seats_split(state.seats);
        seats_sync(state.seats, run.gubsy);
        const int stock[3] = {1082, 1081, 1083}; // vs_stock_loadouts
        for (int j = 0; j < std::min(state.options.vs, max_pad_players); ++j) {
            const SeatDevice device = j == 0 ? SeatDevice{true, -1}
                                      : j <= static_cast<int>(state.seats.pads_here.size())
                                          ? SeatDevice{false, state.seats.pads_here[j - 1]}
                                          : SeatDevice{false, -100 - j};
            const int seat = seat_join(state.seats, device);
            if (seat >= 0) {
                fe.vs.entered[static_cast<std::size_t>(seat)] = true;
                fe.vs.build[static_cast<std::size_t>(seat)] = stock[j % 3];
            }
        }
        fe.vs.selector = state.options.stage;
        fe.vs_mode = true;
        state.options.start_state = 6;
    }
    if (state.options.start_state >= 0) {
        // entered from black, as after a state's fade out, on a new game's day 0 (money and
        // today's line-up, as New Game leaves them)
        fe.entering = state.options.start_state;
        story_new_game(state.world.story, fe.story_tables, 0);
        tournaments_refill(state.world, fe.story_tables, fe.campaign.roster, 0);
        dev_story_seed(fe); // PHANTOMCRASH_DEV_DAY and the rest over day 0
        rumble_dev_entry(state.world, fe.campaign.roster);
        ui.fade.state = 2;
    }
    run.capturing = state.options.frames > 0;
    // PHANTOMCRASH_DEV_FAST=N (port-only): a capture runs N steps a drawn frame
    const char* fast_env = std::getenv("PHANTOMCRASH_DEV_FAST");
    run.fast = fast_env ? std::clamp(std::atoi(fast_env), 1, max_steps_per_frame) : 1;
    // PHANTOMCRASH_DEV_BETWEEN=N (port-only): a capture draws N frames a step, the ones between
    // two steps with their interpolation alpha, as a display faster than 60 Hz does
    const char* between_env = std::getenv("PHANTOMCRASH_DEV_BETWEEN");
    run.between = between_env ? std::clamp(std::atoi(between_env), 1, 16) : 1;
    run.previous_time = static_cast<double>(SDL_GetTicksNS()) / 1e9;
    return 0;
}

// pump input (quitting is the window's close button or the menus); a screen listening for a
// binding takes the keys and buttons first
void pump_events(FrontEndRun& run) {
    // PHANTOMCRASH_MOUSE_MOVE and the rest by drawn frame, once each
    if (run.pads_frame != run.state.frames) {
        run.pads_frame = run.state.frames;
        virtual_pads_frame(run.state.frames);
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            run.state.running = false;
        }
        if (pc_window_event(run.pc, event)) {
            continue; // F11 or Alt+Enter
        }
        if (front_end_event(run.fe, run.ui, event)) {
            continue;
        }
        input_event(run.devices, run.gubsy, event);
        if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
            // gubsy hands a new pad to its primary player: the seats take it back at once,
            // before any later event of this pump is read against the lobby
            seats_sync(run.state.seats, run.gubsy);
        }
        pointer_event(run.ui.pointer, event, run.window);
        SeatPress press;
        if (seat_press_of(event, press)) {
            run.state.seats.presses.push_back(press);
        }
    }
}

// the 2D handed over: a fight's HUD each frame over the front end's last 2D
// (src/battle_run.hpp), the PC's frame rate over it all (SHOW FRAME RATE). Between two of a
// menu's steps the renderer keeps the 2D it was handed.
void present_2d(FrontEndRun& run, int steps) {
    State& state = run.state;
    Ui& ui = run.ui;
    BattleRun& battle = run.battle;
    // the front end's own 2D, taken only when it updated: a 30 Hz fight's skipped step left
    // ui.draw as the last frame handed it, HUD and all, and taking that drew the HUD twice
    // until the next update (docs/performance.md, "The HUD drawn twice")
    if (run.updated) {
        run.front_draw = ui.draw;
    }
    if (battle.active) {
        ui.draw = run.front_draw;
        for (int seat = 0; seat < max_pad_players; ++seat) {
            const std::size_t s = static_cast<std::size_t>(seat);
            battle.hud.mouse_target[s] = {run.devices.owed_twist[s], run.devices.owed_pitch[s]};
        }
        battle_run_hud(
            battle, state, ui, run.renderer,
            run.fe.campaign.paused ? 1.0f : static_cast<float>(state.accumulator / step_seconds));
    }
    if (battle.active || steps > 0) {
        // the HUD and the frame rate never kept in the screen's list
        const std::size_t screen_quads =
            battle.active ? run.front_draw.quads.size() : ui.draw.quads.size();
        pc_fps_draw(ui, run.fe.pc_art, run.pc);
        ui_present(ui);
        ui.draw.quads.resize(screen_quads);
    }
}

// draw frame: a fight's 3D under the 2D, or the 2D and the screen's 3D (the garage screens' model
// stand); a capture's picture, --route's shot or REPORT A BUG's. False when the renderer failed.
bool draw_frame(FrontEndRun& run, bool last) {
    State& state = run.state;
    const std::string capture = last ? state.options.capture
                                : run.capturing
                                    ? capture_path_for(state.options, state.frames, false)
                                    : std::string();
    std::string shot = last || run.route.shot.empty() ? capture : run.route.shot; // --route's
    shot = shot.empty() ? report_panel_shot(run.fe) : shot; // REPORT A BUG's picture
    run.route.shot.clear();
    const float alpha =
        run.fe.campaign.paused ? 1.0f : static_cast<float>(state.accumulator / step_seconds);
    FrameSetup setup;
    setup.clip_to_2d = true; // the stands and trophies inside the 2D's reach
    if (run.battle.active
            ? !battle_run_draw(run.battle, state, run.renderer, run.audio, alpha, shot)
            : !renderer_draw_frame(run.renderer, setup, run.ui.scene, shot)) {
        std::fprintf(stderr, "draw: %s\n", SDL_GetError());
        report_send("renderer", std::string("draw: ") + SDL_GetError());
        return false;
    }
    return true;
}

} // namespace

// the front end from the boot's 0x9E (or --state): the 2D only, until a state shows 3D
int run_front_end(State& state, SDL_Window* window, Renderer& renderer, PackLibrary& packs,
                  Audio* audio, GubsyRuntime& gubsy, InputDevices& devices, PcHost& pc) {
    FrontEndRun run{state, window, renderer, packs, audio, gubsy, devices, pc};
    if (const int status = front_end_start(run)) {
        return status;
    }
    while (state.running) {
        // calc frame time (a capture steps exactly one step a frame)
        const std::uint64_t frame_start = SDL_GetTicksNS();
        const double now = static_cast<double>(frame_start) / 1e9;
        report_frame(); // the watchdog's (src/report/reports.hpp)
        if (run.capturing && run.between > 1) {
            // frames 1..N-1 of a step hold that much of one, the Nth the whole step
            run.between_phase = (run.between_phase + 1) % run.between;
            state.accumulator = run.between_phase == 0
                                    ? step_seconds * run.fast
                                    : step_seconds * run.between_phase / run.between;
        } else {
            state.accumulator += run.capturing ? step_seconds * run.fast : now - run.previous_time;
        }
        run.previous_time = now;

        // pump input
        pump_events(run);

        // run fixed steps
        const int steps = front_end_steps(run);
        state.accumulator = std::min(state.accumulator, step_seconds);
        if (!state.running) {
            break; // the round ended the run (its capture is the last frame)
        }

        present_2d(run, steps);
        // the system cursor only once the mouse has moved
        if (run.ui.pointer.visible) {
            SDL_ShowCursor();
        } else {
            SDL_HideCursor();
        }

        // follow the sound device
        if (audio) {
            update_audio(*audio);
        }

        // draw frame
        const bool last =
            (run.capturing && state.frames + 1 >= state.options.frames) || run.route.quit;
        if (!draw_frame(run, last)) {
            break;
        }

        // capture and quit, the frame limit (a fight's, or the display's refresh for the rest)
        ++state.frames;
        if (last) {
            state.running = false;
        }
        const bool still = !run.battle.active || run.fe.campaign.paused;
        pc_frame_end(pc, frame_start, run.capturing, still);
    }
    shutdown_front_end(run.fe, run.ui);
    if (run.battle.active) {
        pad_rumble_stop(run.battle.rumble, gubsy);
        battle_run_stop(run.battle, state, audio);
    }
    return 0;
}
