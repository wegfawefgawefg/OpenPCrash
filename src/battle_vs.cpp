// A split-screen VS round in the battle session (decomp docs/port/battle.md "Split-screen VS", "The
// battle's own parent in state 6"): state 6 with vs_mode, from the VS setup screen's battle start
// to the end menu's answer. Each entrant's seat plays its own fighter in its own view, with its own
// HUD; the round and its end menu are the world's (src/battle/vs_battle.hpp). The battle state's
// screen (battle_screen.cpp) runs it as any fight, with the pause and the lost-pad pause.
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "battle/battle_data.hpp"
#include "battle/battle_spawn.hpp"
#include "battle_run.hpp"
#include "cook/pack_library.hpp"
#include "render/renderer.hpp"
#include "render/vk_sprites.hpp"
#include "screens/flow/front_end.hpp"
#include "state.hpp"
#include "view/combat_viewer.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

// VS's 3D sounds all sit here (vs_sound_pos), heard from the origin facing +z
constexpr float vs_sound_pos[3] = {0, 0, 2};

// the battle part of the world back to none, the saved part and the VS wins kept (battle_destroy,
// then battle_task_begin's payload zeroed)
void battle_clear(World& world) {
    world.step = 0;
    world.battle.reset();
    world.fighters = {};
    world.combat = CombatWorld{};
    world.huds = {};
    world.lock_warnings = {};
    world.ai = {};
    world.stage = StageWorld{};
    world.battle_phase = 0;
    world.battle_state = BattleState{};
    VsBattle& vs = world.vs;
    const auto wins = vs.wins, losses = vs.losses;
    vs = VsBattle{};
    vs.wins = wins;
    vs.losses = losses;
}

} // namespace

bool battle_vs_start(BattleRun& run, State& state, FrontEnd& fe, Ui& ui, Renderer& renderer,
                     PackLibrary& packs, Audio* audio, std::string& error) {
    // the entered seats in seat order: slot, team and gate by join order; a stock machine plays
    // with the default config (player_config_reset), a kept one with the config kept with it
    // (player_config_store of its save.stats). A kept machine (KEEP SV) is a build row appended
    // after the table, as the campaign's garage machine is (battle_run.cpp)
    struct Entrant {
        int seat, build;
        ButtonConfig buttons;
    };
    std::vector<Entrant> entrants;
    std::vector<BuildRecord> kept;
    const int first_kept = static_cast<int>(fe.campaign.roster.builds.size());
    for (int seat = 0; seat < max_pad_players; ++seat) {
        if (!fe.vs.entered[static_cast<std::size_t>(seat)]) {
            continue;
        }
        const int save = fe.vs.kept[static_cast<std::size_t>(seat)];
        if (save >= 0 && save < static_cast<int>(fe.vs.saves.size()) &&
            fe.vs.saves[static_cast<std::size_t>(save)].present) {
            const VsSave& kept_save = fe.vs.saves[static_cast<std::size_t>(save)];
            entrants.push_back(
                {seat, first_kept + static_cast<int>(kept.size()), kept_save.buttons});
            kept.push_back(build_record_of(kept_save.build));
        } else {
            entrants.push_back({seat, fe.vs.build[static_cast<std::size_t>(seat)], ButtonConfig{}});
        }
    }
    std::vector<FighterBuild> kept_rigs;
    for (const BuildRecord& row : kept) {
        kept_rigs.push_back(fighter_build_of(row));
    }
    const int count = static_cast<int>(entrants.size());
    if (count < 1) {
        error = "vs: nobody entered";
        return false;
    }
    const int selector = fe.vs.selector;
    World& world = state.world;
    battle_clear(world);
    run.vs = true;
    run.renderer = &renderer;
    run.packs = &packs;
    run.fe = &fe;
    run.ui = &ui;

    // the stage and every entrant's machine (battle_task_begin's loads), freed when it stops
    renderer_set_owner(renderer, render_owner_battle);
    run.scenery = std::make_unique<Scenery>();
    Scenery& scenery = *run.scenery;
    if (!load_scenery(scenery, renderer, packs, selector, entrants[0].build, error, kept_rigs)) {
        return false;
    }
    scenery.day = world.story.day;
    scenery.safe_rect = state.options.safe_rect || (fe.pc && fe.pc->settings.safe_area);
    std::vector<int> builds;
    for (int j = 0; j < count; ++j) {
        builds.push_back(entrants[static_cast<std::size_t>(j)].build);
        run.mech_rig[j] = entrants[static_cast<std::size_t>(j)].build;
        if (j > 0 && !load_scenery_fighter(scenery, renderer, packs, j,
                                           entrants[static_cast<std::size_t>(j)].build, error)) {
            return false;
        }
    }
    auto battle = std::make_shared<BattleData>();
    if (!load_battle_data(*battle, packs, selector, builds, error, kept) ||
        battle->entries.empty()) {
        error = error.empty() ? "no entry points" : error;
        return false;
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

    // the area page's battle start: vs_mode, the context refilled with tournament 0 (no
    // opponents), each entered seat's build in roster slot j with its seat as the entry's player,
    // team j (all against all), gate j (player_gate), in play, and its button config
    // (player_config_store); then battle_task_begin's spawns, each through its gate's entrance
    world.battle = battle;
    world.story.rumble_selected = selector;
    world.battle_phase = 0;
    world.vs.mode = true;
    battle_state_begin(world, 6);
    run.vs_rumble_slot = world.battle_state.rumble;
    RumbleSlot& rumble = world.rumble[run.vs_rumble_slot];
    run.vs_rumble = rumble; // the save's day, put back after the round
    rumble.entrants = count;
    unsigned seat_mask = 0;
    for (int j = 0; j < fighter_slots; ++j) {
        rumble.roster[j] = RosterEntry{};
        rumble.roster[j].player = -2; // no one
    }
    const std::vector<BuildRecord>& rows = battle->parts.builds;
    for (int j = 0; j < count; ++j) {
        const Entrant& e = entrants[static_cast<std::size_t>(j)];
        RosterEntry& entry = rumble.roster[j];
        entry.player = static_cast<std::int16_t>(e.seat);
        entry.pilot.team = static_cast<std::int16_t>(j);
        BattleSlot& slot = world.battle_state.slots[j];
        slot.build = rows[e.build < 0 || e.build >= static_cast<int>(rows.size())
                              ? 0
                              : static_cast<std::size_t>(e.build)];
        slot.rig = e.build;
        world.battle_state.player_gate[e.seat] = j;
        world.player_buttons[e.seat] = e.buttons;
        battle_spawn(world, j);
        world.vs.in_play[static_cast<std::size_t>(j)] = true;
        seat_mask |= 1u << e.seat;
        std::printf("vs: slot %d seat %d build %d\n", j, e.seat, e.build);
    }
    StageSetup stage_setup;
    stage_setup.selector = selector;
    stage_setup.vs = true;
    stage_setup.gates_locked = true;
    stage_setup.crate_tournament = 0; // the context refilled with tournament 0
    stage_world_init(world.stage, scenery.stage_data, stage_setup, world.random);
    stage_view_step(scenery.stage_view, world.stage);
    state.view = View{};
    state.view.follow = -1;
    take_step_poses(state.view, world, world);
    if (!run.camera_tables.loaded &&
        !load_scene_camera_tables(run.camera_tables, packs.core, error)) {
        std::fprintf(stderr, "scene cameras: %s\n", error.c_str());
        error.clear();
    }
    run.cameras = SceneCameraState{};
    run.cameras.selector = selector;
    scene_cameras_step(run.cameras, run.camera_tables, state.view, world);

    // the split screen
    run.layout = split_layout(seat_mask, scenery.safe_rect);
    float sprite_views[4][4], drawn_views[4][4];
    for (int v = 0; v < run.layout.count; ++v) {
        const SplitView& split = run.layout.views[static_cast<std::size_t>(v)];
        std::copy(split.rect, split.rect + 4, sprite_views[v]);
        std::copy(split.drawn, split.drawn + 4, drawn_views[v]);
    }
    renderer_set_sprite_views(renderer, sprite_views, drawn_views,
                              run.layout.count > 1 ? run.layout.count : 0);

    // the VS sound rules: banks 1 and 2 muted for the battle, every 3D sound at one point, the
    // listener pinned (audio.md "VS")
    if (audio) {
        stage_audio_start(run.stage_audio, *audio, scenery.stage_data, selector, vs_sound_pos,
                          scenery.stage_view, world.stage);
        mute_sound_bank(*audio, bank_battle, true);
        mute_sound_bank(*audio, bank_battle_old, true);
        set_sound_fixed_point(*audio, vs_sound_pos);
        const float origin[3] = {0, 0, 0}, forward[3] = {0, 0, 1}, up[3] = {0, 1, 0};
        set_listener(*audio, origin, forward, up);
    }

    // the HUD for every view, after the humans' intro
    renderer_set_owner(renderer, 0);
    run.hud = HudView{};
    if (!hud_init(run.hud, ui, world, error)) {
        return false;
    }
    run.hud.view_count = run.layout.view_count;
    run.hud.viewer = std::max(seat_slot(world, run.layout.views[0].seat), 0);
    run.active = true;
    return true;
}

const float* battle_vs_sound_point() {
    return vs_sound_pos;
}

void battle_vs_debug_step(World& world) {
    // PHANTOMCRASH_VS_KO="STEP:SLOT" knocks a slot out at that step (headless runs of the round's
    // end and its menu)
    static const char* ko = std::getenv("PHANTOMCRASH_VS_KO");
    int at = -1, slot = -1;
    if (ko && std::sscanf(ko, "%d:%d", &at, &slot) == 2 && at == world.step && slot >= 0 &&
        slot < fighter_slots) {
        world.fighters[static_cast<std::size_t>(slot)].combat.durability = 0;
    }
}

bool battle_vs_draw(BattleRun& run, State& state, Renderer& renderer, float alpha,
                    const std::string& capture) {
    World& world = state.world;
    const float ticks = static_cast<float>(world.step) + alpha;
    FighterPose fighters[fighter_slots];
    for (int slot = 0; slot < fighter_slots; ++slot) {
        fighters[slot] = interpolate_fighter(state.view, slot, alpha);
    }
    SceneryView views[4];
    for (int v = 0; v < run.layout.count; ++v) {
        const SplitView& split = run.layout.views[static_cast<std::size_t>(v)];
        views[v].camera = interpolate_seat_camera(state.view, split.seat, alpha);
        std::copy(split.drawn, split.drawn + 4, views[v].rect);
        std::copy(split.rect, split.rect + 4, views[v].sprite_rect);
        views[v].seat = split.seat;
    }
    return draw_scenery_views(*run.scenery, renderer, views, run.layout.count, fighters, ticks,
                              alpha, capture);
}

void battle_vs_hud(BattleRun& run, State& state, Ui& ui, const Renderer& renderer, float alpha) {
    World& world = state.world;
    for (int v = 0; v < run.layout.count; ++v) {
        const SplitView& split = run.layout.views[static_cast<std::size_t>(v)];
        HudScreen screen{interpolate_seat_camera(state.view, split.seat, alpha), renderer.width,
                         renderer.height, run.scenery->safe_rect};
        screen.view = run.layout.count > 1 ? v : -1;
        screen.sprite_shift = renderer_sprite_frame(renderer, screen.view).shift;
        std::copy(split.rect, split.rect + 4, screen.rect);
        std::copy(split.drawn, split.drawn + 4, screen.drawn);
        hud_draw_player(run.hud, ui, world, screen, alpha, seat_slot(world, split.seat));
    }
    hud_draw_overlay(run.hud, ui, world, alpha);
}

void battle_vs_stop(BattleRun& run, State& state, Audio* audio) {
    // battle_task_destroy: the banks unmuted, the streams stopped
    if (audio) {
        mute_sound_bank(*audio, bank_battle, false);
        mute_sound_bank(*audio, bank_battle_old, false);
        set_sound_fixed_point(*audio, nullptr);
        stop_all_sounds(*audio);
    }
    if (run.renderer) {
        renderer_set_sprite_views(*run.renderer, nullptr, nullptr, 0);
    }
    World& world = state.world;
    std::printf("vs: result %d, wins %d %d %d %d\n", world.vs.result, world.vs.wins[0],
                world.vs.wins[1], world.vs.wins[2], world.vs.wins[3]);
    world.rumble[run.vs_rumble_slot] = run.vs_rumble;
    battle_clear(world);
    run.vs = false;
}
