#include "battle_replay.hpp"

#include "battle/battle_data.hpp"
#include "battle/battle_spawn.hpp"
#include "battle_run.hpp"
#include "cook/pack_library.hpp"
#include "render/renderer.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/pc_settings.hpp"
#include "state.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"
#include "world/world_hash.hpp"

#include <cinttypes>
#include <cstdio>

void replay_record_start(BattleReplay& replay, const World& world) {
    // replay_snapshot_take and replay_cut_clear: the port's loads never run late, so no
    // cut is ever marked (retail cuts a replay at a replacement whose load wasn't done in time)
    replay.recording = true;
    replay.start = std::make_shared<World>(world);
    replay.inputs.clear();
    replay.live_hashes.clear();
}

void replay_record_step(BattleReplay& replay, const Inputs& inputs, const World& after) {
    // replay_input_record: up to replay_inputs' length; a longer fight's replay stops there
    if (!replay.recording || static_cast<int>(replay.inputs.size()) >= replay_max_steps) {
        return;
    }
    replay.inputs.push_back(inputs);
    replay.live_hashes.push_back(world_fight_hash(after));
}

bool replay_available(const BattleReplay& replay) {
    return replay.start && !replay.inputs.empty();
}

void replay_play_start(BattleReplay& replay, World& world) {
    // replay_start: battle phase 2, the live state saved, the snapshot put back
    replay.recording = false;
    replay.live = std::make_shared<World>(world);
    world = *replay.start;
    world.battle_phase = 2;
    replay.playing = true;
    replay.done = false;
    replay.played = 0;
    replay.first_mismatch = -1;
}

bool replay_next_inputs(BattleReplay& replay, Inputs& out) {
    // replay_input_play
    if (replay.played >= static_cast<int>(replay.inputs.size())) {
        return false;
    }
    out = replay.inputs[static_cast<std::size_t>(replay.played)];
    return true;
}

void replay_play_step(BattleReplay& replay, const World& after) {
    const int at = replay.played++;
    const std::uint64_t hash = world_fight_hash(after);
    if (replay.first_mismatch < 0 && hash != replay.live_hashes[static_cast<std::size_t>(at)]) {
        replay.first_mismatch = at;
        std::printf("replay: step %d hash %016" PRIx64 " differs from the live fight's %016" PRIx64
                    "\n",
                    at, hash, replay.live_hashes[static_cast<std::size_t>(at)]);
    }
    // replay_update's ends: the recorded length, and the door's or a ranker kill's end_state 1
    // (replay_end_request in scene_door's exit). A death plays on to the recorded length
    const bool ended = after.battle_state.end_state == 1;
    if (replay.played >= static_cast<int>(replay.inputs.size()) || ended) {
        if (!replay.done) {
            std::printf("replay: %d of %zu steps, final hash %016" PRIx64 " %s\n", replay.played,
                        replay.inputs.size(), hash,
                        replay.first_mismatch < 0 ? "matches the live fight's"
                                                  : "DIFFERS from the live fight's");
        }
        replay.done = true;
    }
}

void replay_play_stop(BattleReplay& replay, World& world) {
    // replay_end: the live state back; the record stays for another look
    if (replay.playing && replay.live) {
        world = *replay.live;
    }
    replay.live.reset();
    replay.playing = false;
}

// --- the replay in the battle session (state 5) ---

bool battle_replay_start(BattleRun& run, State& state, FrontEnd& fe, Renderer& renderer,
                         PackLibrary& packs, Audio* audio, std::string& error) {
    BattleReplay& replay = run.replay;
    if (!replay_available(replay)) {
        error = "replay: nothing recorded";
        return false;
    }
    const World& start = *replay.start;
    const int selector = start.story.rumble_selected;
    run.renderer = &renderer;
    run.packs = &packs;
    run.fe = &fe;
    run.demo = false;
    run.vs = false;

    // the stage and every fighter the record starts with, as the fight loaded them (the battle's
    // data is the record's own); a replacement loads as the replay draws it
    renderer_set_owner(renderer, render_owner_battle);
    run.scenery = std::make_unique<Scenery>();
    Scenery& scenery = *run.scenery;
    const RosterEntry& player = start.rumble[rumble_slot_of_selector(selector)].roster[0];
    const int player_rig = start.battle_state.slots[0].rig;
    if (!load_scenery(scenery, renderer, packs, selector, player_rig, error,
                      {fighter_build_of(build_record_of(player.build))})) {
        return false;
    }
    scenery.day = start.story.day;
    scenery.safe_rect = state.options.safe_rect || (fe.pc && fe.pc->settings.safe_area);
    run.mech_rig[0] = player_rig;
    for (int slot = 1; slot < fighter_slots; ++slot) {
        if (!start.fighters[static_cast<std::size_t>(slot)].present) {
            continue;
        }
        const int rig = start.battle_state.slots[slot].rig;
        if (!load_scenery_fighter(scenery, renderer, packs, slot, rig, error)) {
            return false;
        }
        run.mech_rig[slot] = rig;
    }
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
    renderer_set_owner(renderer, 0);

    // the world: the record's start in phase 2, the live one kept
    replay_play_start(replay, state.world);
    World& world = state.world;
    // the stage's constant tables are the scenery's, loaded again (the world points at them)
    world.stage.data = &scenery.stage_data;
    stage_view_step(scenery.stage_view, world.stage);
    if (!run.camera_tables.loaded &&
        !load_scene_camera_tables(run.camera_tables, packs.core, error)) {
        std::fprintf(stderr, "scene cameras: %s\n", error.c_str());
        error.clear();
    }
    run.cameras = SceneCameraState{};
    run.cameras.selector = selector;
    run.cameras.replay_view = replay.view;
    state.view = View{};
    take_step_poses(state.view, world, world);
    scene_cameras_step(run.cameras, run.camera_tables, state.view, world);
    if (audio) {
        const FighterPose& p = state.view.current.fighters[0];
        stage_audio_start(run.stage_audio, *audio, scenery.stage_data, selector,
                          p.present ? p.position : state.view.current.camera.position,
                          scenery.stage_view, world.stage);
    }
    std::printf("replay: %zu steps from step %lld\n", replay.inputs.size(),
                static_cast<long long>(world.step));
    run.active = true;
    return true;
}
