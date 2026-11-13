// Phantom Crash on PC. The loop in plain paragraphs: pump input, run fixed 60 Hz steps with a
// catch-up cap, draw with an interpolation alpha (AGENTS.md, "Simulation"). The game boots into
// the original's front end (src/screens, the application states); --viewer runs the stage viewer,
// a battle on one stage with player 1's fighter and the chase camera, instead.
#include "audio/audio.hpp"
#include "battle/battle_data.hpp"
#include "battle_run.hpp"
#include "build_info.hpp"
#include "cli.hpp"
#include "cook/data_hash.hpp"
#include "cook/pack_library.hpp"
#include "input/bindings.hpp"
#include "input/dev_route.hpp"
#include "input/devices.hpp"
#include "input/menu_input.hpp"
#include "input/script_input.hpp"
#include "input/virtual_pads.hpp"
#include "render/renderer.hpp"
#include "run_log.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/pc_settings.hpp"
#include "setup/setup_job.hpp"
#include "setup/setup_screen.hpp"
#include "state.hpp"
#include "story/rumble.hpp"
#include "talk/talk_player.hpp"
#include "viewer_run.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <gubsy/app.hpp>
#include <gubsy/runtime.hpp>

namespace {

// the front end from the boot's 0x9E (or --state): the 2D only, until a state shows 3D
int run_front_end(State& state, SDL_Window* window, Renderer& renderer, PackLibrary& packs,
                  Audio* audio, GubsyRuntime& gubsy, InputDevices& devices, PcHost& pc) {
    // init the front end: the 2D, the application states
    Ui ui;
    FrontEnd fe;
    BattleRun battle; // the fight a battle state asks for (src/battle_run.hpp)
    Draw2D front_draw; // the front end's 2D as its last update left it
    fe.pc = &pc;
    std::string error;
    InputScript battle_script;
    if (!state.options.battle_script.empty() &&
        !load_input_script(battle_script, state.options.battle_script, error)) {
        std::fprintf(stderr, "battle script: %s\n", error.c_str());
        return 2;
    }
    DevRoute route; // --route FILE: a headless run's presses by state (input/dev_route.hpp)
    if (!state.options.route.empty() && !load_dev_route(route, state.options.route, error)) {
        std::fprintf(stderr, "route: %s\n", error.c_str());
        return 2;
    }
    route.shot_dir = state.options.capture.substr(0, state.options.capture.rfind('/') + 1);
    // the world's generator: its table, and the index retail seeds from the clock at boot (a
    // capture starts at 0 so it repeats; --seed sets it)
    if (!load_rand_table(packs.core, error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        return 1;
    }
    const int seed = state.options.seed >= 0 ? state.options.seed
                     : state.options.frames > 0
                         ? 0
                         : static_cast<int>(SDL_GetTicksNS() % rand_table_size);
    state.world.random.index = seed % rand_table_size;
    if (!init_ui(ui, renderer, packs, audio, state.options.data_dir, error) ||
        !init_front_end(fe, ui, state.world, state.machine, error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        return 1;
    }
    fe.seats = &state.seats;
    if (state.options.vs > 0) {
        // --vs N: one VS round straight away (state 6 with vs_mode), seat 0 on the keyboard and
        // the rest on the pads plugged in (PHANTOMCRASH_PADS for headless runs), the stock PLAIN
        // builds in turn; then the front end goes on from its answer
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            input_event(devices, gubsy, event);
        }
        seats_split(state.seats);
        seats_sync(state.seats, gubsy);
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

    const bool capturing = state.options.frames > 0;
    // PHANTOMCRASH_DEV_FAST=N (port-only): a capture runs N steps a drawn frame
    const char* fast_env = std::getenv("PHANTOMCRASH_DEV_FAST");
    const int fast = fast_env ? std::clamp(std::atoi(fast_env), 1, max_steps_per_frame) : 1;
    int fe_phase = 0;           // a 30 Hz state's steps: it updates on the even ones
    std::uint16_t carried = 0;  // pad presses of a skipped step
    double previous_time = static_cast<double>(SDL_GetTicksNS()) / 1e9;
    while (state.running) {
        // calc frame time (a capture steps exactly one step a frame)
        const std::uint64_t frame_start = SDL_GetTicksNS();
        const double now = static_cast<double>(frame_start) / 1e9;
        state.accumulator += capturing ? step_seconds * fast : now - previous_time;
        previous_time = now;

        // pump input (quitting is the window's close button or the menus); a screen listening
        // for a binding takes the keys and buttons first
        virtual_pads_frame(state.frames);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                state.running = false;
            }
            if (pc_window_event(pc, event)) {
                continue; // F11 or Alt+Enter
            }
            if (front_end_event(fe, ui, event)) {
                continue;
            }
            input_event(devices, gubsy, event);
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                // gubsy hands a new pad to its primary player: the seats take it back at once,
                // before any later event of this pump is read against the lobby
                seats_sync(state.seats, gubsy);
            }
            pointer_event(ui.pointer, event, window);
            SeatPress press;
            if (seat_press_of(event, press)) {
                state.seats.presses.push_back(press);
            }
        }

        // run fixed steps: the pads read, a battle's world step, then the front end's update at its
        // state's cadence (60 Hz menus, 30 Hz battle and result: every other step)
        int steps = 0;
        while (state.accumulator >= step_seconds && steps < max_steps_per_frame) {
            // the battle the state asks for loads in this step; one whose state is gone stops
            if (fe.campaign.battle_stop && battle.active) {
                pad_rumble_stop(battle.rumble, gubsy); // fighter_task_destroy's rumble_halt
                battle_run_stop(battle, state, audio);
                fe.campaign.battle_running = false;
                SDL_SetWindowRelativeMouseMode(window, false);
            }
            fe.campaign.battle_stop = false;
            if (fe.campaign.battle_wanted && !battle.active) {
                // 2 the result screen's demo battle, 3 the replay: watched, not played
                const bool watched = fe.campaign.battle_wanted >= 2;
                const bool replay = fe.campaign.battle_wanted == 3;
                const bool started =
                    battle_run_start(battle, state, fe, ui, renderer, packs, audio, error);
                fe.campaign.battle_wanted = 0; // read by the start
                if (!started) {
                    std::fprintf(stderr, "battle: %s\n", error.c_str());
                    if (watched) {
                        // the sheet over black, as before; a replay with nothing to play ends
                        battle_run_stop(battle, state, audio);
                        fe.campaign.replay_done = replay;
                    } else {
                        state.running = false;
                        break;
                    }
                }
                // a fight takes the mouse; the result screen's demo battle is the menu's
                // backdrop, the replay a show
                fe.campaign.battle_running = started && !battle.demo;
                SDL_SetWindowRelativeMouseMode(window, started && !watched);
                battle_script.line = battle_script.done = 0;
                battle_script.previous = 0;
            }
            ReadOptions read;
            read.cadence = ui.frame_cadence;
            read.blank = front_end_pads_blank(fe, ui);
            seats_sync(state.seats, gubsy);
            if (battle.active && !battle.demo && !battle.replay.playing) {
                read.fight = true;
                read.mouse_captured = SDL_GetWindowRelativeMouseMode(window);
                seat_aim_rates(state.world, read);
            }
            read_pads(devices, gubsy, read);
            // a fight steps the world unless paused (the pause task stops the game's subtree)
            if (battle.active && !fe.campaign.paused) {
                Inputs inputs = build_inputs(state.view);
                inputs.pads = devices.pads;
                inputs.aims = devices.aims;
                inputs.scene_text_done = fe.campaign.scene_text_done; // the lessons' lines
                inputs.dev = route.fight | route.hold; // --route's kills and doors
                route.fight = 0;
                if (!battle_script.lines.empty()) {
                    inputs.pads[0] = next_script_pad(battle_script);
                    inputs.aims[0] = MouseAim{};
                }
                battle.replay.view = fe.campaign.replay_view;
                battle.cameras.replay_view = battle.replay.playing ? fe.campaign.replay_view : -1;
                battle_run_step(battle, state, ui, renderer, packs, audio, inputs);
                pad_rumble_events(battle.rumble, gubsy, state.world);
                pad_rumble_step(battle.rumble, gubsy, state.machine.vibration == 0);
                fe.campaign.replay_done = fe.campaign.replay_done || battle.replay.done;
                if (!battle.demo) {
                    battle_events_play(fe, ui);
                }
            }
            // Start on any seat's pad pauses a fight (Escape is Start there), PAUSE IN
            // BACKGROUND opens the pause while the window is away (docs/settings.md), and a
            // pulled pad pauses it (the lost-pad watch)
            const bool away = !capturing && !pc.focused && pc.settings.focus_pause &&
                              !fe.campaign.paused;
            bool start = away;
            for (const PadRecord& pad : devices.pads) {
                start = start || menu_start_pressed(pad);
            }
            start = start || route.start; // --route's start pauses a fight too, once
            route.start = false;
            if (battle.active && !battle.demo && !battle.replay.playing) {
                battle_pause_toggle(fe, ui, start);
            }
            // the pause stops every pad's rumble on the spot, and again each paused update
            // (pause_enter and pause_menu_update's rumble_halt)
            if (battle.active && fe.campaign.paused) {
                pad_rumble_stop(battle.rumble, gubsy);
            }
            // the front end's pad: what went down on a step it skips carries to its next update
            ui.pad = devices.pads[0];
            ui.pads = devices.pads;
            ui.pc_held = devices.pc_held[0];
            fe.story_talk_skipped = story_talk_skipped(gubsy); // PC: "Story conversations"
            battle.hud.barks.skipped = battle_chatter_skipped(gubsy); // and "Battle chatter"
            ui.pad.b.rising |= carried;
            pointer_update(ui.pointer);
            if (read.blank) {
                ui.pointer.left = ui.pointer.right = false;
                ui.pointer.wheel = 0;
            }
            int gamepads = 0;
            SDL_free(SDL_GetGamepads(&gamepads));
            ui.pads_connected = std::max(1, gamepads);
            state.accumulator -= step_seconds;
            ++steps;
            if (ui.frame_cadence < 60 && (fe_phase++ & 1) != 0) {
                carried = ui.pad.b.rising; // a 30 Hz state updates every other step
                continue;
            }
            carried = 0;
            // scripted presses and clicks for headless runs (--press, --click)
            for (const auto& [update, bits] : state.options.presses) {
                if (update == fe.updates && !read.blank) {
                    ui.pad.b.rising |= static_cast<std::uint16_t>(bits);
                    ui.pad.b.cur |= static_cast<std::uint16_t>(bits);
                    ui.pad.b.repeat |= static_cast<std::uint16_t>(bits);
                    ui.pc_held |= bits >> 16;
                }
            }
            for (const Drag& drag : state.options.drags) {
                const int t = static_cast<int>(fe.updates) - drag.update;
                if (t >= 0 && t <= drag_updates && !read.blank) {
                    const float f = static_cast<float>(t) / static_cast<float>(drag_updates);
                    ui.pointer.x = drag.from[0] + (drag.to[0] - drag.from[0]) * f;
                    ui.pointer.y = drag.from[1] + (drag.to[1] - drag.from[1]) * f;
                    ui.pointer.visible = ui.pointer.moved = true;
                    ui.pointer.left = t == 0;
                    ui.pointer.held = t < drag_updates;
                    ui.pointer.released = t == drag_updates;
                }
            }
            for (const auto& [update, at] : state.options.clicks) {
                if (update == fe.updates && !read.blank) {
                    ui.pointer.x = at.first;
                    ui.pointer.y = at.second;
                    ui.pointer.visible = ui.pointer.moved = ui.pointer.left = true;
                }
            }
            for (const auto& [update, at] : state.options.hovers) {
                if (update == fe.updates && !read.blank) {
                    ui.pointer.x = at.first;
                    ui.pointer.y = at.second;
                    ui.pointer.visible = ui.pointer.moved = true;
                }
            }
            if (!route.steps.empty()) {
                dev_route_update(route, fe.state, read.blank, state.world);
                ui.pad.b.rising |= static_cast<std::uint16_t>(route.press);
                ui.pad.b.cur |= static_cast<std::uint16_t>(route.press);
                ui.pad.b.repeat |= static_cast<std::uint16_t>(route.press);
                if (route.click) {
                    ui.pointer.x = route.click_x;
                    ui.pointer.y = route.click_y;
                    ui.pointer.visible = ui.pointer.moved = ui.pointer.left = true;
                }
            }
            ui_begin_update(ui);
            front_end_update(fe, ui);
            state.seats.presses.clear();
        }
        state.accumulator = std::min(state.accumulator, step_seconds);
        if (!state.running) {
            break; // the round ended the run (its capture is the last frame)
        }
        // a fight's HUD each frame over the front end's last 2D (src/battle_run.hpp)
        if (steps > 0) {
            front_draw = ui.draw;
        }
        if (battle.active) {
            ui.draw = front_draw;
            for (int seat = 0; seat < max_pad_players; ++seat) {
                battle.hud.mouse_target[static_cast<std::size_t>(seat)] = {
                    devices.owed_twist[static_cast<std::size_t>(seat)],
                    devices.owed_pitch[static_cast<std::size_t>(seat)]};
            }
            battle_run_hud(battle, state, ui, renderer,
                           fe.campaign.paused ? 1.0f
                                              : static_cast<float>(state.accumulator / step_seconds));
        }
        if (battle.active || steps > 0) {
            // the PC's frame rate over it all (SHOW FRAME RATE), never kept in the screen's list
            const std::size_t screen_quads = ui.draw.quads.size();
            pc_fps_draw(ui, fe.pc_art, pc);
            ui_present(ui);
            ui.draw.quads.resize(screen_quads);
        }
        // the system cursor only once the mouse has moved
        if (ui.pointer.visible) {
            SDL_ShowCursor();
        } else {
            SDL_HideCursor();
        }

        // follow the sound device
        if (audio) {
            update_audio(*audio);
        }

        // draw frame: a fight's 3D under the 2D, or the 2D and the screen's 3D (the garage
        // screens' model stand)
        const bool last =
            (capturing && state.frames + 1 >= state.options.frames) || route.quit;
        const std::string capture = last        ? state.options.capture
                                    : capturing ? capture_path_for(state.options, state.frames,
                                                                   false)
                                                : std::string();
        const std::string shot = last || route.shot.empty() ? capture : route.shot; // --route's
        route.shot.clear();
        const float alpha =
            fe.campaign.paused ? 1.0f : static_cast<float>(state.accumulator / step_seconds);
        FrameSetup setup;
        setup.clip_to_2d = true; // the stands and trophies inside the 2D's reach
        if (battle.active ? !battle_run_draw(battle, state, renderer, audio, alpha, shot)
                          : !renderer_draw_frame(renderer, setup, ui.scene, shot)) {
            std::fprintf(stderr, "draw: %s\n", SDL_GetError());
            break;
        }

        // capture and quit
        ++state.frames;
        if (last) {
            state.running = false;
        }
        pc_frame_end(pc, frame_start, capturing);
    }
    shutdown_front_end(fe, ui);
    if (battle.active) {
        pad_rumble_stop(battle.rumble, gubsy);
        battle_run_stop(battle, state, audio);
    }
    return 0;
}

} // namespace

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

    // init the run log (src/run_log.hpp): this run's SDL_Log lines in a file under the pref path
    run_log_start(argc, argv);
    SDL_Log("%s", build_label().c_str());

    // init window (and PHANTOMCRASH_PADS's virtual pads, src/input/virtual_pads.hpp)
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    virtual_pads_attach();
    // first run: no data/ yet, so the setup screen reads the player's own disc into it
    // (src/setup); a quit there ends the run
    if (!setup_data_ready(state.options.data_dir) && run_setup_screen(state.options) != 0) {
        run_log_stop();
        SDL_Quit();
        return 0;
    }
    SDL_Window* window = SDL_CreateWindow("Phantom Crash", state.options.width,
                                          state.options.height, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // init renderer, open the packs (cooking data/ on first run, docs/assets.md)
    Renderer renderer;
    PackLibrary packs;
    std::string error;
    if (!renderer_init(renderer, window, error) ||
        !packs_start(packs, state.options.data_dir, state.options.cooked_dir, error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
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
    gubsy_config.window_title = "Phantom Crash";
    if (const char* root = std::getenv("PHANTOMCRASH_GUBSY_ROOT")) {
        gubsy_config.data_root = root;
    } else if (char* prefs = SDL_GetPrefPath("PhantomCrash", "PhantomCrash")) {
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
    run_log_stop();
    SDL_Quit();
    return status;
}
