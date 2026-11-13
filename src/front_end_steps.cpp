// The front end's fixed steps (src/front_end_run.hpp): at the top of each the fight the state asks
// for starts or stops, the pads are read, a fight steps the world, then the front end updates at
// its state's cadence (60 Hz menus, 30 Hz battle and result: every other step).
#include "front_end_run.hpp"
#include "input/bindings.hpp"
#include "input/devices.hpp"
#include "input/menu_input.hpp"
#include "report/reports.hpp"
#include "screens/pc/pc_settings.hpp"
#include "story/rumble.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>

namespace {

// the battle the state asks for loads in this step; one whose state is gone stops. False when a
// fight couldn't start and the run ends.
bool battle_start_stop(FrontEndRun& run) {
    State& state = run.state;
    FrontEnd& fe = run.fe;
    BattleRun& battle = run.battle;
    if (fe.campaign.battle_stop && battle.active) {
        pad_rumble_stop(battle.rumble, run.gubsy); // fighter_task_destroy's rumble_halt
        battle_run_stop(battle, state, run.audio);
        fe.campaign.battle_running = false;
        SDL_SetWindowRelativeMouseMode(run.window, false);
    }
    fe.campaign.battle_stop = false;
    if (!fe.campaign.battle_wanted || battle.active) {
        return true;
    }
    // 2 the result screen's demo battle, 3 the replay: watched, not played
    const bool watched = fe.campaign.battle_wanted >= 2;
    const bool replay = fe.campaign.battle_wanted == 3;
    const bool started =
        battle_run_start(battle, state, fe, run.ui, run.renderer, run.packs, run.audio, run.error);
    fe.campaign.battle_wanted = 0; // read by the start
    if (!started) {
        std::fprintf(stderr, "battle: %s\n", run.error.c_str());
        report_send("startup", "battle: " + run.error);
        if (!watched) {
            state.running = false;
            return false;
        }
        // the sheet over black, as before; a replay with nothing to play ends
        battle_run_stop(battle, state, run.audio);
        fe.campaign.replay_done = replay;
    }
    // a fight takes the mouse; the result screen's demo battle is the menu's backdrop, the
    // replay a show
    fe.campaign.battle_running = started && !battle.demo;
    SDL_SetWindowRelativeMouseMode(run.window, started && !watched);
    run.battle_script.line = run.battle_script.done = 0;
    run.battle_script.previous = 0;
    return true;
}

// a played fight takes the mouse (relative mode: it aims) unless paused, when the cursor is free
// to click the pause's items, its panels or leave the window and nothing it does turns the view.
// Taken back on resuming with the moves made meanwhile dropped, so the view doesn't jump. The
// pause's settings panel (screens/pc/pause_settings.hpp) also reads Escape as Back, as the menus
// do; Escape stays Back until it is let go after the panel closed, or it would come back as Start
// and end the pause
void pause_input(FrontEndRun& run) {
    const BattleRun& battle = run.battle;
    run.panel_keys = run.fe.pause_settings ||
                     (run.panel_keys && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_ESCAPE]);
    const bool played = battle.active && !battle.demo && !battle.replay.playing;
    const bool capture = played && !run.fe.campaign.paused;
    if (played && capture != run.mouse_captured) {
        run.mouse_captured = capture;
        SDL_SetWindowRelativeMouseMode(run.window, capture);
        InputDevices& devices = run.devices;
        devices.mouse_x = devices.mouse_y = 0.0f;
        devices.owed_twist.fill(0.0f);
        devices.owed_pitch.fill(0.0f);
    }
    run.mouse_captured = played && run.mouse_captured;
}

// a fight steps the world unless paused (the pause task stops the game's subtree)
void fight_step(FrontEndRun& run) {
    State& state = run.state;
    FrontEnd& fe = run.fe;
    BattleRun& battle = run.battle;
    Inputs inputs = build_inputs(state.view);
    inputs.pads = run.devices.pads;
    inputs.aims = run.devices.aims;
    inputs.scene_text_done = fe.campaign.scene_text_done; // the lessons' lines
    inputs.dev = run.route.fight | run.route.hold;        // --route's kills and doors
    run.route.fight = 0;
    if (!run.battle_script.lines.empty()) {
        inputs.pads[0] = next_script_pad(run.battle_script);
        inputs.aims[0] = MouseAim{};
    }
    battle.replay.view = fe.campaign.replay_view;
    battle.cameras.replay_view = battle.replay.playing ? fe.campaign.replay_view : -1;
    battle_run_step(battle, state, run.ui, run.renderer, run.packs, run.audio, inputs);
    pad_rumble_events(battle.rumble, run.gubsy, state.world);
    pad_rumble_step(battle.rumble, run.gubsy, state.machine.vibration == 0);
    fe.campaign.replay_done = fe.campaign.replay_done || battle.replay.done;
    if (!battle.demo) {
        battle_events_play(fe, run.ui);
    }
}

// Start on any seat's pad pauses a fight (Escape is Start there), PAUSE IN BACKGROUND opens the
// pause while the window is away (docs/settings.md), and a pulled pad pauses it (the lost-pad
// watch)
void fight_pause(FrontEndRun& run) {
    FrontEnd& fe = run.fe;
    BattleRun& battle = run.battle;
    const bool away =
        !run.capturing && !run.pc.focused && run.pc.settings.focus_pause && !fe.campaign.paused;
    bool start = away;
    for (const PadRecord& pad : run.devices.pads) {
        start = start || menu_start_pressed(pad);
    }
    start = start || run.route.start; // --route's start pauses a fight too, once
    run.route.start = false;
    if (battle.active && !battle.demo && !battle.replay.playing) {
        battle_pause_toggle(fe, run.ui, start);
    }
    // the pause stops every pad's rumble on the spot, and again each paused update
    // (pause_enter and pause_menu_update's rumble_halt)
    if (battle.active && fe.campaign.paused) {
        pad_rumble_stop(battle.rumble, run.gubsy);
    }
}

// scripted presses and clicks for headless runs (--press, --click, --drag, --hover, --route)
void headless_presses(FrontEndRun& run, bool blank) {
    const Options& options = run.state.options;
    Ui& ui = run.ui;
    const int updates = static_cast<int>(run.fe.updates);
    for (const auto& [update, bits] : options.presses) {
        if (update == run.fe.updates && !blank) {
            ui.pad.b.rising |= static_cast<std::uint16_t>(bits);
            ui.pad.b.cur |= static_cast<std::uint16_t>(bits);
            ui.pad.b.repeat |= static_cast<std::uint16_t>(bits);
            ui.pc_held |= bits >> 16;
        }
    }
    for (const Drag& drag : options.drags) {
        const int t = updates - drag.update;
        if (t >= 0 && t <= drag_updates && !blank) {
            const float f = static_cast<float>(t) / static_cast<float>(drag_updates);
            ui.pointer.x = drag.from[0] + (drag.to[0] - drag.from[0]) * f;
            ui.pointer.y = drag.from[1] + (drag.to[1] - drag.from[1]) * f;
            ui.pointer.visible = ui.pointer.moved = true;
            ui.pointer.left = t == 0;
            ui.pointer.held = t < drag_updates;
            ui.pointer.released = t == drag_updates;
        }
    }
    for (const auto& [update, at] : options.clicks) {
        if (update == run.fe.updates && !blank) {
            ui.pointer.x = at.first;
            ui.pointer.y = at.second;
            ui.pointer.visible = ui.pointer.moved = ui.pointer.left = true;
        }
    }
    for (const auto& [update, at] : options.hovers) {
        if (update == run.fe.updates && !blank) {
            ui.pointer.x = at.first;
            ui.pointer.y = at.second;
            ui.pointer.visible = ui.pointer.moved = true;
        }
    }
    DevRoute& route = run.route;
    if (!route.steps.empty()) {
        dev_route_update(route, run.fe.state, blank, run.state.world);
        ui.pad.b.rising |= static_cast<std::uint16_t>(route.press);
        ui.pad.b.cur |= static_cast<std::uint16_t>(route.press);
        ui.pad.b.repeat |= static_cast<std::uint16_t>(route.press);
        if (route.click) {
            ui.pointer.x = route.click_x;
            ui.pointer.y = route.click_y;
            ui.pointer.visible = ui.pointer.moved = ui.pointer.left = true;
        }
    }
}

} // namespace

int front_end_steps(FrontEndRun& run) {
    State& state = run.state;
    FrontEnd& fe = run.fe;
    Ui& ui = run.ui;
    BattleRun& battle = run.battle;
    InputDevices& devices = run.devices;
    int steps = 0;
    run.updated = false;
    while (state.accumulator >= step_seconds && steps < max_steps_per_frame) {
        if (!battle_start_stop(run)) {
            break;
        }
        // the pads read, a fight's aim with them
        ReadOptions read;
        read.cadence = ui.frame_cadence;
        read.blank = front_end_pads_blank(fe, ui);
        seats_sync(state.seats, run.gubsy);
        pause_input(run);
        if (battle.active && !battle.demo && !battle.replay.playing) {
            read.fight = !run.panel_keys;
            read.mouse_captured = SDL_GetWindowRelativeMouseMode(run.window);
            seat_aim_rates(state.world, read);
        }
        read_pads(devices, run.gubsy, read);
        if (battle.active && !fe.campaign.paused) {
            fight_step(run);
        }
        fight_pause(run);
        // the front end's pad: what went down on a step it skips carries to its next update
        ui.pad = devices.pads[0];
        ui.pads = devices.pads;
        ui.pc_held = devices.pc_held[0];
        fe.story_talk_skipped = story_talk_skipped(run.gubsy);        // PC: "Story conversations"
        battle.hud.barks.skipped = battle_chatter_skipped(run.gubsy); // and "Battle chatter"
        ui.pad.b.rising |= run.carried;
        int gamepads = 0;
        SDL_free(SDL_GetGamepads(&gamepads));
        ui.pads_connected = std::max(1, gamepads);
        state.accumulator -= step_seconds;
        ++steps;
        if (ui.frame_cadence < 60 && (run.fe_phase++ & 1) != 0) {
            run.carried = ui.pad.b.rising; // a 30 Hz state updates every other step
            continue;
        }
        run.carried = 0;
        // the pointer's gathered moves and clicks go to an update that runs (a 30 Hz state skips
        // every other step, and a click taken on a skipped one was lost)
        pointer_update(ui.pointer);
        if (read.blank) {
            ui.pointer.left = ui.pointer.right = false;
            ui.pointer.wheel = 0;
        }
        headless_presses(run, read.blank);
        ui_begin_update(ui);
        front_end_update(fe, ui);
        // INFINITE MONEY (PC CONFIG's CHEATS): held outside a played fight, never in its step
        if (!battle.active || battle.demo || battle.replay.playing) {
            cheat_money_hold(state.world.story);
        }
        run.updated = true;
        state.seats.presses.clear();
    }
    return steps;
}
