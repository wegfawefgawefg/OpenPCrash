// The stage viewer's loop (--viewer, --stage, --build, --camera, --script): a battle on one stage
// with player 1's fighter driven by the pads (or the script) behind the chase camera, or the free
// camera alone; --trace prints the step.
#include "viewer_run.hpp"

#include "audio/audio.hpp"
#include "cook/pack_library.hpp"
#include "effects/effects.hpp"
#include "hud/hud.hpp"
#include "input/devices.hpp"
#include "input/pad_rumble.hpp"
#include "input/script_input.hpp"
#include "input/virtual_pads.hpp"
#include "mech/dance_pose.hpp"
#include "perf_log.hpp"
#include "render/renderer.hpp"
#include "render/scenery.hpp"
#include "screens/pc/pc_settings.hpp"
#include "screens/pc/pc_settings_art.hpp"
#include "screens/ui/ui.hpp"
#include "stage/stage_audio.hpp"
#include "view/cockpit_view.hpp"
#include "view/combat_sounds.hpp"
#include "view/combat_viewer.hpp"
#include "view/fighter_sounds.hpp"
#include "view/scene_camera.hpp"
#include "viewer_battle.hpp"
#include "viewer_pose.hpp"
#include "viewer_world.hpp"
#include "world/world_hash.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <memory>

// the stage viewer: a battle on the stage with the build as player 1's fighter, driven by the
// pads (or --script) with the chase camera, or (--camera) the free camera alone. The battle's
// data goes to fixed point here, once
int run_viewer(State& state, SDL_Window* window, Renderer& renderer, PackLibrary& packs,
               Audio* audio, GubsyRuntime& gubsy, InputDevices& devices, PcHost& pc) {
    InputScript script;
    std::string error;
    // --pose: the scene's stage, fighters and frames (src/viewer_pose.hpp)
    Pose pose;
    const bool posed = !state.options.pose.empty();
    if (posed) {
        if (!load_pose(pose, state.options.pose, error)) {
            std::fprintf(stderr, "pose: %s\n", error.c_str());
            return 2;
        }
        pose_options(pose, state.options);
    }
    if (!state.options.script.empty()) {
        if (!load_input_script(script, state.options.script, error)) {
            std::fprintf(stderr, "script: %s\n", error.c_str());
            return 2;
        }
        if (state.options.frames <= 0) {
            state.options.frames = script_steps(script) + 1;
        }
    }

    // the stage and the fighter from their packs
    Scenery scenery;
    const std::uint64_t load_start = SDL_GetTicksNS();
    if (!load_scenery(scenery, renderer, packs, state.options.stage, state.options.build, error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        return 1;
    }
    std::printf("load: %.1f ms\n", static_cast<double>(SDL_GetTicksNS() - load_start) / 1e6);
    scenery.day = state.options.day;
    // --dance: slot 0's build dancing alone in the free camera (mech/dance_pose.hpp)
    Dance dance;
    if (!state.options.dance.empty()) {
        if (!state.options.camera_given || !load_dance(dance, state.options.dance, error)) {
            std::fprintf(stderr, "dance: %s\n",
                         state.options.camera_given ? error.c_str() : "needs --camera");
            return 2;
        }
        dance.white = state.options.dance_white;
        dance_build_clips(dance, scenery.mech);
        scenery.dance = &dance;
        // at the origin facing +z, so --camera is the same on every stage (only the light differs)
        std::fill(std::begin(scenery.mech_position), std::end(scenery.mech_position), 0.0f);
        scenery.mech_heading = 0;
        std::printf("dance: %d ticks, %zu channels\n", dance.ticks, dance.channels.size());
    }
    scenery.safe_rect = state.options.safe_rect || pc.settings.safe_area;

    // the world RNG's table. The index is retail's clock seed; 0 here so runs and captures
    // repeat, or --seed's
    if (!load_rand_table(packs.core, error)) {
        std::fprintf(stderr, "start: %s\n", error.c_str());
        return 1;
    }
    state.world.random.index = std::max(state.options.seed, 0) % rand_table_size;

    StageAudio stage_audio;
    CombatSounds combat_sounds;
    FighterSounds fighter_sounds;

    // the battle's effects (src/effects): the pool cleared as the effects task's first update does
    auto effects = std::make_unique<Effects>();
    if (!effects_load(*effects, packs, scenery, renderer, error)) {
        std::fprintf(stderr, "effects: %s\n", error.c_str());
        return 1;
    }
    effects_reset(*effects, state.options.stage, 0);
    {
        const float* d = scenery.lighting.stages[state.options.stage].light_direction;
        effects->sun[0] = -d[0], effects->sun[1] = -d[1], effects->sun[2] = -d[2];
    }
    scenery.effects = effects.get();

    // the world: a battle on the stage with the build as player 1's fighter, or (--camera) the
    // free camera alone. The battle's data goes to fixed point here, once
    const auto to_fx = [](float value) { return fx_bits(std::bit_cast<std::uint32_t>(value)); };
    const auto to_angle = [](float turns) {
        return Angle{static_cast<std::uint32_t>(
            static_cast<std::int64_t>(std::llround(turns * 4294967296.0)))};
    };
    if (state.options.camera_given) {
        const float* c = state.options.camera;
        state.world.camera.position = {to_fx(c[0]), to_fx(c[1]), to_fx(c[2])};
        state.world.camera.yaw = to_angle(c[3] / 360.0f);
        state.world.camera.pitch = to_angle(c[4] / 360.0f);
    } else {
        // slot 0 the build, slots 1.. the --opponents as AIs (src/viewer_battle.cpp), with the
        // seats' PC control settings as a game's fight has them
        pc_controls_commit(state.world, gubsy);
        if (!viewer_battle_start(state, scenery, renderer, packs, error)) {
            std::fprintf(stderr, "battle: %s\n", error.c_str());
            return 1;
        }
        if (posed) {
            pose_place(state.world, pose);
        }
        perf_load("viewer battle", load_start); // the stage, the effects, the machines
        const Fighter& spawned = state.world.fighters[0];
        std::printf("fighter: leg class %d weight %.2f radius %.3f walk %.4f run %.4f%s jump "
                    "%.3f (speed %.5f thrust %.5f an update) turn %.3f deg dodge %.3f / %.3f "
                    "head %.3f body %.3f\n",
                    spawned.leg_class, fx_float(spawned.weight), fx_float(spawned.radius),
                    fx_float(spawned.walk_rate), fx_float(spawned.run_rate),
                    spawned.walk_root_motion ? " (clip rates)" : "",
                    fx_float(spawned.jump_height), fx_float(spawned.jump_speed_30),
                    fx_float(spawned.jump_thrust_30), fx_float(angle_degrees(spawned.turn_step)),
                    fx_float(spawned.dodge_speed_ground), fx_float(spawned.dodge_speed_air),
                    fx_float(spawned.head_height),
                    fx_float(spawned.pose.head_position.y - spawned.position.y));
        if (state.options.pose_compare) {
            pose_compare(scenery.mech, *state.world.battle, state.options.build);
            return 0;
        }
        // a fight takes the mouse (docs/input.md)
        SDL_SetWindowRelativeMouseMode(window, true);
    }
    // init the stage's part of the world: battle_stage_init with the world RNG (src/stage), after
    // the fighters as battle_task_begin makes the stage task after theirs (their gate and AI draws
    // come first)
    viewer_stage_start(state.world, state.options, scenery.stage_data);
    if (state.options.dummy >= 0) {
        viewer_open_gate(state.world); // the entry gate stands between player and --dummy
    }
    stage_view_step(scenery.stage_view, state.world.stage);
    take_step_poses(state.view, state.world, state.world);
    // where the stage's ambience hears from: the local seat's fighter (first_human_actor),
    // or the free camera when there is none
    const auto ambience_at = [&]() -> const float* {
        const int slot = seat_slot(state.world, 0);
        return slot >= 0 ? state.view.current.fighters[slot].position
                         : state.view.current.camera.position;
    };
    if (audio) {
        stage_audio_start(stage_audio, *audio, scenery.stage_data, state.options.stage,
                          ambience_at(), scenery.stage_view, state.world.stage);
    }

    // init the battle HUD (src/hud) over the 2D layer: the followed fighter's, after a human's
    // intro (READY at 90 ticks, GO at 30)
    Ui ui;
    PcArt pc_art; // the frame rate's letters
    HudView hud;
    PadRumble rumble;
    const bool with_hud = !state.options.camera_given && !(posed && pose.camera);
    if (with_hud) {
        if (!init_ui(ui, renderer, packs, audio, state.options.data_dir, error) ||
            !hud_init(hud, ui, state.world, error)) {
            std::fprintf(stderr, "hud: %s\n", error.c_str());
            return 1;
        }
        ui.frame_cadence = 30;
        // the fight's widescreen anchors (state 6's), so HUD WIDTH and HUD SIZE reach the
        // viewer's HUD as they do a fight's
        ui_set_anchors(ui, 6);
        hud.viewer = state.view.follow >= 0 ? state.view.follow
                                            : std::max(seat_slot(state.world, 0), 0);
        if (state.world.fighters[static_cast<std::size_t>(hud.viewer)].controller != -1) {
            hud_intro_start(hud, 92);
        }
    }

    const bool capturing = state.options.frames > 0;
    double previous_time = static_cast<double>(SDL_GetTicksNS()) / 1e9;
    while (state.running) {
        // calc frame time (a capture steps exactly one step a frame); the fight holds still
        // while the window is away, when the PC settings say so
        const std::uint64_t frame_start = SDL_GetTicksNS();
        const double now = static_cast<double>(frame_start) / 1e9;
        const bool held = !capturing && !pc.focused && pc.settings.focus_pause;
        state.accumulator += capturing ? step_seconds : held ? 0.0 : now - previous_time;
        previous_time = now;
        if (held) {
            pad_rumble_stop(rumble, gubsy); // held still: the pads stop at once, as a pause
        }

        // pump input (Escape is the pad's Start in a fight; closing the window quits)
        virtual_pads_frame(state.frames);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                state.running = false;
            }
            if (pc_window_event(pc, event)) {
                continue; // F11 or Alt+Enter
            }
            input_event(devices, gubsy, event);
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                seats_sync(state.seats, gubsy); // gubsy gave it its primary player: take it back
            }
            handle_event(state.view, event, window);
        }

        // run fixed steps: the pads read before each one
        int steps = 0;
        while (state.accumulator >= step_seconds && steps < max_steps_per_frame) {
            const PerfScope timed(PerfPart::step);
            ReadOptions read;
            read.mouse_captured = SDL_GetWindowRelativeMouseMode(window);
            read.fight = true;
            seats_sync(state.seats, gubsy);
            seat_aim_rates(state.world, read);
            read_pads(devices, gubsy, read);
            Inputs inputs = build_inputs(state.view);
            inputs.pads = devices.pads;
            inputs.aims = devices.aims;
            if (!script.lines.empty()) {
                inputs.pads[0] = next_script_pad(script);
                inputs.aims[0] = MouseAim{};
            }
            if (posed) {
                pose_pads(pose, inputs);
            }
            const World before = state.world;
            viewer_debug_hurt(state.world);
            const std::uint64_t world_start = perf_now();
            step_world(state.world, inputs);
            perf_world_step(perf_now() - world_start);
            world_hash_print(state.world, state.options.hash);
            take_step_poses(state.view, before, state.world);
            // the HUD follows the step: its events, rolls, marks and timers
            if (with_hud) {
                hud_step(hud, ui, state.world);
            }
            // the stage's presentation follows the step: its animation, the props and craft,
            // their sounds
            stage_animation_step(scenery.stage.animation);
            stage_view_step(scenery.stage_view, state.world.stage);
            effects_step(*effects, before, state.world);
            // the pads' rumble from the hits (the viewer has no OPTION: on)
            pad_rumble_events(rumble, gubsy, state.world);
            pad_rumble_step(rumble, gubsy, true);
            if (audio) {
                set_sound_trace_step(*audio, static_cast<long long>(state.world.step));
                stage_audio_step(stage_audio, *audio, scenery.stage_data, state.world.stage,
                                 scenery.stage_view, ambience_at());
                combat_sounds_step(combat_sounds, *audio, before, state.world);
                fighter_sounds_step(fighter_sounds, *audio, before, state.world, effects.get());
            }
            state.accumulator -= step_seconds;
            ++steps;

            // --trace N: fighter 1 every N steps
            const Fighter& player = state.world.fighters[0];
            if (state.options.trace > 0 && player.present &&
                state.world.step % state.options.trace == 0) {
                std::printf("step %lld state %d mode %d clip %d phase %.2f pos %.4f %.4f %.4f "
                            "vel %.4f %.4f %.4f heading %.2f %s contact %x head %.3f\n",
                            static_cast<long long>(state.world.step), player.motion_state,
                            player.motion_mode, player.motion_slot_a,
                            fx_float(player.motion_phase), fx_float(player.position.x),
                            fx_float(player.position.y), fx_float(player.position.z),
                            fx_float(player.velocity.x), fx_float(player.velocity.y),
                            fx_float(player.velocity.z),
                            fx_float(angle_degrees(player.heading)),
                            player.grounded ? "ground" : "air", player.contact_flags,
                            fx_float(player.head_height));
            }
            // --trace-ai N: every AI every N steps
            if (state.options.trace_ai > 0 && state.world.step % state.options.trace_ai == 0) {
                viewer_trace_ai(state.world);
            }
            // --trace N: the combat events every step, the weapons every N steps
            if (state.options.trace > 0) {
                viewer_combat_trace(state.world, state.options.trace);
            }
        }
        state.accumulator = std::min(state.accumulator, step_seconds);

        // follow the sound device
        if (audio) {
            update_audio(*audio);
        }

        // draw frame: the fighter where the world puts it, seen by the chase camera
        const float alpha = static_cast<float>(state.accumulator / step_seconds);
        const bool last = capturing && state.frames + 1 >= state.options.frames;
        const float ticks = static_cast<float>(state.world.step) + alpha;
        CameraPose camera = interpolate_camera(state.view, alpha);
        FighterPose fighters[fighter_slots];
        for (int slot = 0; slot < fighter_slots; ++slot) {
            fighters[slot] = interpolate_fighter(state.view, slot, alpha);
            fighters[slot].body_hidden = own_body_hidden(state.world, slot, -1);
        }
        // the cockpit's hit shake (view/cockpit_view.hpp)
        const float shake = static_cast<float>(renderer.options.camera_shake) / 100.0f;
        cockpit_shake_apply(camera, state.world, seat_slot(state.world, 0), shake);
        // FIELD OF VIEW on the fighter's camera (not the free camera nor a pose's)
        if (state.world.battle && !(posed && pose.camera)) {
            fight_fov_apply(camera, 0, renderer.options.fov);
        }
        // PHANTOMCRASH_DEV_EYE=x,y,z[,ahead] (captures): the main view from that offset of fighter
        // 0, looking at the point `ahead` in front of it (heading's forward), presentation only
        if (const char* eye = SDL_getenv("PHANTOMCRASH_DEV_EYE"); eye && fighters[0].present) {
            float off[4] = {0, 0, 0, 0};
            std::sscanf(eye, "%f,%f,%f,%f", &off[0], &off[1], &off[2], &off[3]);
            const float* at = fighters[0].position;
            const float s = std::sin(fighters[0].heading), c = std::cos(fighters[0].heading);
            camera.target[0] = at[0] + s * off[3];
            camera.target[1] = at[1] + 2.0f;
            camera.target[2] = at[2] + c * off[3];
            camera.position[0] = at[0] + c * off[0] + s * off[2];
            camera.position[1] = at[1] + off[1];
            camera.position[2] = at[2] - s * off[0] + c * off[2];
            const float dx = camera.target[0] - camera.position[0];
            const float dy = camera.target[1] - camera.position[1];
            const float dz = camera.target[2] - camera.position[2];
            camera.looks_at = true;
            camera.yaw = std::atan2(dx, dz);
            camera.pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
        }
        if (posed && pose.camera) {
            pose_camera(pose, fighters, camera);
        }
        if (audio) {
            stage_audio_listener(*audio, camera.position, camera.yaw, camera.pitch);
        }
        if (with_hud && !SDL_getenv("PHANTOMCRASH_DEV_EYE")) {
            for (int seat = 0; seat < max_pad_players; ++seat) {
                hud.mouse_target[static_cast<std::size_t>(seat)] = {
                    devices.owed_twist[static_cast<std::size_t>(seat)],
                    devices.owed_pitch[static_cast<std::size_t>(seat)]};
            }
            ui_begin_update(ui);
            HudScreen screen{camera, renderer.width, renderer.height, scenery.safe_rect};
            screen.shake = shake;
            hud_draw(hud, ui, state.world, screen, alpha);
            pc_fps_draw(ui, pc_art, pc); // SHOW FRAME RATE
            ui_present(ui);
        } else {
            renderer_set_sprites(renderer, {}, {}); // no HUD: the frame's 2D starts empty
            hud.cockpit.shown = false;
        }
        scenery.cockpit = &hud.cockpit;
        if (!draw_scenery_fighters(scenery, renderer, camera, fighters, ticks, alpha,
                          capture_path_for(state.options, state.frames, last))) {
            std::fprintf(stderr, "draw: %s\n", SDL_GetError());
            break;
        }

        // capture and quit
        ++state.frames;
        if (last) {
            state.running = false;
        }
        pc_frame_end(pc, frame_start, capturing, false);
    }
    // stop the pads' rumble, the stage's ambience and engine loops
    pad_rumble_stop(rumble, gubsy);
    if (audio) {
        stage_audio_stop(stage_audio, *audio);
        combat_sounds_stop(combat_sounds, *audio);
        fighter_sounds_stop(fighter_sounds, *audio);
    }
    return 0;
}
