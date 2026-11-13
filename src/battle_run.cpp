#include "battle_run.hpp"

#include "assets/fighter_tables.hpp"
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "battle/battle_data.hpp"
#include "battle/battle_ending.hpp"
#include "battle/battle_spawn.hpp"
#include "battle_world.hpp"
#include "cook/pack_library.hpp"
#include "input/menu_input.hpp"
#include "render/renderer.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/pc_settings.hpp"
#include "state.hpp"
#include "story/rumble.hpp"
#include "view/cockpit_view.hpp"
#include "view/combat_viewer.hpp"
#include "world/world.hpp"
#include "world/world_hash.hpp"

#include <algorithm>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <cstdio>
#include <cstdlib>

bool battle_run_start(BattleRun& run, State& state, FrontEnd& fe, Ui& ui, Renderer& renderer,
                      PackLibrary& packs, Audio* audio, std::string& error) {
    if (fe.vs_mode && fe.campaign.battle_wanted == 1) {
        return battle_vs_start(run, state, fe, ui, renderer, packs, audio, error);
    }
    if (fe.campaign.battle_wanted == 3) {
        run.ui = &ui;
        return battle_replay_start(run, state, fe, renderer, packs, audio, error);
    }
    World& world = state.world;
    const int selector = world.story.rumble_selected;
    const int context = rumble_slot_of_selector(selector);
    RumbleSlot& rumble = world.rumble[context];
    // the result screen's demo battle (result_screen_setup, demo_battle_enter):
    // what its roster draw borrows is put back when it stops; fought by AIs alone in phase 1
    run.demo = fe.campaign.battle_wanted == 2;
    if (run.demo) {
        run.demo_rumble = rumble;
        run.demo_lineup = world.story.lineup[context];
        run.demo_result = world.battle_state.result;
    }
    // the fight in the world (battle_world.cpp): the demo's draw and every build that may fight
    BattleWorldSetup setup;
    setup.menu_state = fe.state;
    setup.phase = fe.campaign.battle_phase;
    setup.demo = run.demo;
    setup.demo_tournament = fe.campaign.demo_tournament;
    setup.rental_build = fe.campaign.rental_build;
    const BattleWorldPlan plan =
        battle_world_plan(world, setup, fe.campaign.roster, fe.story_tables);

    // the stage and the human's fighter, the battle's data in fixed point; what reaches the GPU
    // from here to the HUD is the battle's, freed when it stops
    const std::uint64_t load_start = SDL_GetTicksNS();
    run.renderer = &renderer;
    run.packs = &packs;
    run.fe = &fe;
    run.ui = &ui;
    {
        // what the GPU and the process hold as the battle loads (flat across battles)
        const RendererMemory m = renderer_memory(renderer);
        long pages = 0, resident = 0;
        if (std::FILE* statm = std::fopen("/proc/self/statm", "r")) {
            if (std::fscanf(statm, "%ld %ld", &pages, &resident) != 2) {
                resident = 0;
            }
            std::fclose(statm);
        }
        // the heap in use apart from what the allocator keeps (glibc), so a leak shows from
        // fragmentation and the software Vulkan's memory, which resident counts too
        double heap = 0;
#if defined(__GLIBC__)
        heap = static_cast<double>(mallinfo2().uordblks) / 1048576.0;
#endif
        std::printf("battle memory: %d meshes %.1f MB, %d textures %.1f MB, %d handles, %zu packs "
                    "open, resident %.1f MB, heap in use %.1f MB\n",
                    m.meshes, static_cast<double>(m.mesh_bytes) / 1048576.0, m.textures,
                    static_cast<double>(m.texture_texels) / 1048576.0, m.slots, packs.open.size(),
                    static_cast<double>(resident) * 4096.0 / 1048576.0, heap);
    }
    renderer_set_owner(renderer, render_owner_battle);
    run.scenery = std::make_unique<Scenery>();
    Scenery& scenery = *run.scenery;
    if (!load_scenery(scenery, renderer, packs, selector, plan.player_rig, error,
                      {fighter_build_of(plan.extra[0])})) {
        return false;
    }
    scenery.day = world.story.day;
    scenery.safe_rect = state.options.safe_rect || (fe.pc && fe.pc->settings.safe_area);
    const std::shared_ptr<BattleData> battle = battle_world_data(world, plan, packs, error);
    if (!battle) {
        return false;
    }
    std::printf("battle: selector %d tournament %d, %d entrants, %zu builds loaded in %.1f ms\n",
                selector, world.story.lineup[context], rumble.entrants, plan.rigs.size(),
                static_cast<double>(SDL_GetTicksNS() - load_start) / 1e6);

    // the battle state, each fighter (its look loaded just before it spawns) and the stage
    run.mech_rig[0] = plan.player_rig;
    const auto look = [&](int slot, int rig, std::string& why) {
        if (!load_scenery_fighter(scenery, renderer, packs, slot, rig, why)) {
            return false;
        }
        run.mech_rig[slot] = rig;
        return true;
    };
    if (!battle_world_spawn(world, setup, fe.campaign.roster, scenery.stage_data, look, error)) {
        return false;
    }
    stage_view_step(scenery.stage_view, world.stage);
    // the effects (the pool cleared as the effects task's first update does) and the HUD
    run.effects = std::make_unique<Effects>();
    if (!effects_load(*run.effects, packs, scenery, renderer, error)) {
        return false;
    }
    effects_reset(*run.effects, selector, 0);
    {
        const float* d = scenery.lighting.stages[selector].light_direction;
        run.effects->sun[0] = -d[0], run.effects->sun[1] = -d[1], run.effects->sun[2] = -d[2];
    }
    scenery.effects = run.effects.get();
    renderer_set_owner(renderer, 0); // the HUD's sheets are the front end's, kept
    run.hud = HudView{};
    if (!run.demo && !hud_init(run.hud, ui, world, error)) {
        return false;
    }
    run.hud.viewer = std::max(seat_slot(world, 0), 0);
    // the Area Rankers' defeat lines' lengths, which their scene waits out (the world's)
    for (int area = 0; area < 3; ++area) {
        battle->ranker_line_ticks[area] =
            scene_text_ticks(ui, run.hud.barks.tables.ranker_scripts[area]);
    }
    battle->scene_text_scripts = scene_text_addresses(ui); // the lessons' lines that exist
    // the scene cameras (their rail clocks zeroed as camera_init does)
    if (!run.camera_tables.loaded &&
        !load_scene_camera_tables(run.camera_tables, packs.core, error)) {
        std::fprintf(stderr, "scene cameras: %s\n", error.c_str()); // the fighters' own only
        error.clear();
    }
    run.cameras = SceneCameraState{};
    run.cameras.selector = selector;
    state.view = View{};
    take_step_poses(state.view, world, world);
    scene_cameras_step(run.cameras, run.camera_tables, state.view, world);
    if (audio && run.demo) {
        // the demo's battle sounds muted (effect banks 1..3)
        for (int bank : {bank_battle, bank_battle_old, bank_battle_player}) {
            mute_sound_bank(*audio, bank, true);
        }
    } else if (audio) {
        const FighterPose& p = state.view.current.fighters[0];
        stage_audio_start(run.stage_audio, *audio, scenery.stage_data, selector,
                          p.present ? p.position : state.view.current.camera.position,
                          scenery.stage_view, world.stage);
    }
    // a tournament's fight is recorded for its replay (replay_snapshot_take: battle phase 0;
    // only state 6 reaches the result screen that offers it)
    run.replay.recording = false;
    if (!run.demo && fe.state == app_battle) {
        replay_record_start(run.replay, world);
    }
    run.active = true;
    return true;
}

void battle_run_step(BattleRun& run, State& state, Ui& ui, Renderer& renderer, PackLibrary& packs,
                     Audio* audio, const Inputs& inputs) {
    World& world = state.world;
    Scenery& scenery = *run.scenery;
    if (run.vs) {
        battle_vs_debug_step(world);
    }
    viewer_debug_hurt(world); // PHANTOMCRASH_HURT (view/combat_viewer.hpp)
    // a replay steps the recorded input instead (replay_input_play)
    Inputs played;
    const bool replaying = run.replay.playing;
    if (replaying && !replay_next_inputs(run.replay, played)) {
        run.replay.done = true;
        return;
    }
    const Inputs& in = replaying ? played : inputs;
    const World before = world;
    step_world(world, in);
    world_hash_print(world, state.options.hash);
    if (replaying) {
        replay_play_step(run.replay, world);
    } else {
        replay_record_step(run.replay, in, world);
    }
    take_step_poses(state.view, before, world);
    scene_cameras_step(run.cameras, run.camera_tables, state.view, world);
    if (!run.demo && !replaying) {
        // the bark interpreter reads player 0's pad (seat 0's): accept and the skip chord
        const PadRecord& pad = in.pads[0];
        run.hud.barks.pads.accept = run.hud.barks.pads.accept || menu_accept_pressed(pad);
        run.hud.barks.pads.chord = text_skip_held(pad);
        hud_step(run.hud, ui, world);
    }
    stage_animation_step(scenery.stage.animation);
    stage_view_step(scenery.stage_view, world.stage);
    run.effects->replay_view = run.cameras.replay_view;
    effects_step(*run.effects, before, world);
    if (audio) {
        // where the stage's ambience hears from: VS's one point, else seat 0's fighter
        // (first_human_actor), or the camera when there is none
        const int heard = seat_slot(world, 0);
        const float* at = run.vs ? battle_vs_sound_point()
                          : heard >= 0 && state.view.current.fighters[heard].present
                              ? state.view.current.fighters[heard].position
                              : state.view.current.camera.position;
        set_sound_trace_step(*audio, static_cast<long long>(world.step));
        stage_audio_step(run.stage_audio, *audio, scenery.stage_data, world.stage,
                         scenery.stage_view, at);
        combat_sounds_step(run.combat_sounds, *audio, before, world);
        fighter_sounds_step(run.fighter_sounds, *audio, before, world, run.effects.get());
    }
    // a replacement drawn this step: its mech (retail's loader thread; the world spawns it 300
    // ticks on)
    const int slot = world.battle_state.loading_slot;
    if (slot > 0 && slot < 6 && run.mech_rig[slot] != world.battle_state.slots[slot].rig) {
        std::string error;
        renderer_set_owner(renderer, render_owner_battle);
        const bool loaded = load_scenery_fighter(scenery, renderer, packs, slot,
                                                 world.battle_state.slots[slot].rig, error);
        renderer_set_owner(renderer, 0);
        if (loaded) {
            run.mech_rig[slot] = world.battle_state.slots[slot].rig;
        } else {
            std::fprintf(stderr, "replacement: %s\n", error.c_str());
        }
    }
    // PHANTOMCRASH_DEV_BATTLE_LOOP: the same fight again from its start, for the memory check
    static const char* loop = SDL_getenv("PHANTOMCRASH_DEV_BATTLE_LOOP");
    int loop_steps = 0, loop_count = 0;
    if (loop && std::sscanf(loop, "%d:%d", &loop_steps, &loop_count) == 2 && run.fe &&
        run.dev_loops_done < loop_count && world.battle_state.frame >= loop_steps) {
        const int done = run.dev_loops_done + 1;
        FrontEnd& fe = *run.fe;
        battle_run_stop(run, state, audio);
        std::string error;
        if (!battle_run_start(run, state, fe, ui, renderer, packs, audio, error)) {
            std::fprintf(stderr, "battle loop: %s\n", error.c_str());
        }
        run.dev_loops_done = done;
    }
}

bool battle_run_draw(BattleRun& run, State& state, Renderer& renderer, Audio* audio, float alpha,
                     const std::string& capture) {
    if (run.vs) {
        return battle_vs_draw(run, state, renderer, alpha, capture);
    }
    World& world = state.world;
    Scenery& scenery = *run.scenery;
    const float ticks = static_cast<float>(world.step) + alpha;
    CameraPose camera = interpolate_camera(state.view, alpha);
    // the hit shake on the cockpit view, seat 0's (src/view/cockpit_view.hpp), outside a scene
    // camera's own
    const int viewer = seat_slot(world, 0);
    const float shake = static_cast<float>(renderer.options.camera_shake) / 100.0f;
    if (run.cameras.main.source == 0 && !run.demo && !run.replay.playing) {
        cockpit_shake_apply(camera, world, viewer, shake);
    } else if (run.replay.playing && replay_cockpit_shown(run.cameras)) {
        replay_shake_apply(camera, world, viewer, shake);
    }
    if (run.replay.playing && run.cameras.main.source >= 1100) {
        dying_tilt_apply(camera, world, viewer, shake);
    }
    FighterPose fighters[fighter_slots];
    for (int slot = 0; slot < fighter_slots; ++slot) {
        fighters[slot] = interpolate_fighter(state.view, slot, alpha);
        if (world.battle_state.slots[slot].parked) {
            fighters[slot].present = false; // a parked task draws nothing
        }
        // actor_draw draws nothing in the ending's last shot: the camera is in its cockpit
        if (world.battle_state.slots[slot].scene.state == scene_ending_last) {
            fighters[slot].present = false;
        }
    }
    // a human's own body in its cockpit view, or a replay's view 0 (actor_draw)
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const bool replay_cockpit = run.replay.playing && run.cameras.main.source >= 1100;
        fighters[slot].body_hidden =
            own_body_hidden(world, slot, replay_cockpit ? run.cameras.replay_view : -1);
    }
    if (audio) {
        stage_audio_listener(*audio, camera.position, camera.yaw, camera.pitch);
    }
    // the front end's own 3D over it (the result screen's trophy, render pass 13)
    scenery.late = run.ui && run.ui->scene_drawn ? &run.ui->scene : nullptr;
    // the cockpit instruments the HUD picked this frame (none in a demo or a replay)
    scenery.cockpit = run.demo || run.replay.playing ? nullptr : &run.hud.cockpit;
    return draw_scenery_fighters(scenery, renderer, camera, fighters, ticks, alpha, capture);
}

void battle_run_hud(BattleRun& run, State& state, Ui& ui, const Renderer& renderer, float alpha) {
    if (run.demo || run.replay.playing) {
        return; // no one's HUD over the result sheet, nor in a replay (battle phase 2)
    }
    if (run.vs) {
        battle_vs_hud(run, state, ui, renderer, alpha);
        return;
    }
    HudScreen screen{interpolate_camera(state.view, alpha), renderer.width, renderer.height,
                     run.scenery->safe_rect};
    screen.shake = static_cast<float>(renderer.options.camera_shake) / 100.0f;
    hud_draw(run.hud, ui, state.world, screen, alpha);
}

void battle_run_stop(BattleRun& run, State& state, Audio* audio) {
    if (audio) {
        stage_audio_stop(run.stage_audio, *audio);
        combat_sounds_stop(run.combat_sounds, *audio);
        fighter_sounds_stop(run.fighter_sounds, *audio);
    }
    if (run.vs) {
        battle_vs_stop(run, state, audio);
    }
    // camera_view_save (battle_destroy): a fight of the player's own, outside VS, keeps
    // slot 0's view for the next fight
    if (!run.replay.playing && !run.demo) {
        battle_camera_view_save(state.world);
    }
    // a replay puts the live world back (replay_end); a live fight's record is complete
    if (run.replay.playing) {
        replay_play_stop(run.replay, state.world);
    }
    run.replay.recording = false;
    run.rumble = PadRumble{}; // main halted the pads first (pad_rumble_stop)
    run.scenery.reset();
    run.effects.reset();
    run.active = false;
    // the GPU memory the battle uploaded (the stage, the machines, the effects) and the packs it
    // read them from: the stage's, the parts' and their paint
    if (run.renderer) {
        renderer_set_owner(*run.renderer, 0);
        renderer_free_owner(*run.renderer, render_owner_battle);
    }
    if (run.packs) {
        std::vector<std::string> names;
        for (const auto& [name, pack] : run.packs->open) {
            for (const char* prefix : {"stage", "leg", "arm_", "back_", "body", "paint", "cab_"}) {
                if (name.rfind(prefix, 0) == 0) {
                    names.push_back(name);
                    break;
                }
            }
        }
        for (const std::string& name : names) {
            pack_close(*run.packs, name);
        }
    }
    for (int& rig : run.mech_rig) {
        rig = -1;
    }
    // a demo battle's borrowings put back (demo_battle_destroy: the roster, the line-up, the
    // result its sheet shows), its sounds unmuted
    World& world = state.world;
    if (run.demo) {
        const int context = rumble_slot_of_selector(world.story.rumble_selected);
        world.rumble[context] = run.demo_rumble;
        world.story.lineup[context] = run.demo_lineup;
        world.battle_state.result = run.demo_result;
        world.battle_phase = 0;
        if (audio) {
            for (int bank : {bank_battle, bank_battle_old, bank_battle_player}) {
                mute_sound_bank(*audio, bank, false);
            }
        }
        run.demo = false;
    }
    // the world keeps the result and the rosters; its fight is over
    state.world.battle.reset();
    for (Fighter& f : state.world.fighters) {
        f = Fighter{};
    }
}
