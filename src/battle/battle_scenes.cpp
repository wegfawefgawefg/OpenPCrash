// The fighters' scenes (decomp src/hand/u_088880.c scene_intro_* 0x88F7A.., scene_door_*
// 0x89F23.., scene_player_death_* 0x8A55A.., scene_boss_defeat_* 0x897E1..; scene_table;
// src/hand/loadout_assets.c actor_door_check; docs/port/battle.md "The entrance", "How a
// battle ends"). Every timer is in ticks and steps 1 a step; the door walk's per-update numbers
// are halved (sixty.md R8).
//
// The death scene and the boss defeat are battle_scene_ends.cpp's. The entrance walks out of the
// gate on its scene's root motion (scene_camera_rows 0x258E68 +4, BattleData::scene_walks); the
// scene cameras are presentation's (src/view/scene_camera.cpp).
#include "ai/ai_links.hpp"
#include "audio/sound_cues.hpp"
#include "battle/battle_ending.hpp"
#include "battle/battle_flow.hpp"
#include "battle/battle_scene_parts.hpp"
#include "mech/fighter_rates.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

namespace scenes {

std::int32_t angle_diff(Angle a, Angle b) {
    return static_cast<std::int32_t>(a.raw - b.raw);
}

RosterEntry& entry_of(World& world, const Fighter& f) {
    return battle_roster_entry(world, f.slot);
}

bool crossed(Fx timer, int mark, Fx step) {
    return timer <= fx(mark) && fx(mark) - step < timer;
}

void freeze(World& world, int slot) {
    world.battle_state.slots[slot].ai_frozen = true;
    world.fighters[static_cast<std::size_t>(slot)].control = ControlRecord{};
    world.ai[static_cast<std::size_t>(slot)].record = ControlRecord{};
}

// every other fighter parked
// decomp: battle_pause_others 0x91D44
void pause_others(World& world, const Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    for (int i = 0; i < battle_slots; ++i) {
        const Fighter& other = world.fighters[static_cast<std::size_t>(i)];
        const bool running = other.present && !b.slots[i].parked;
        if (i != f.slot && running && battle_roster_entry(world, i).player != -2) {
            b.slots[i].parked = true;
            scene.hid_others[i] = true;
        } else {
            scene.hid_others[i] = false;
        }
    }
}

// decomp: battle_resume_others 0x91E27
void resume_others(World& world, const Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    for (int i = 0; i < battle_slots; ++i) {
        if (scene.hid_others[i]) {
            b.slots[i].parked = false;
            scene.hid_others[i] = false;
        }
    }
}

} // namespace scenes

using namespace scenes;

namespace {

// motion_root_sample on a scene's root track: x but in the walk and run states, and z
// decomp: motion_root_sample 0x90F0A
FxVec3 scene_root_sample(const Fighter& f, const SceneWalks::Root& root, Fx frame) {
    const bool keeps_x = !state_in(f.motion_state, {state_walk, state_walk_back, state_run,
                                                    state_run_reverse});
    return {keeps_x ? fx_curve_value(root.x, frame) : Fx{}, Fx{}, fx_curve_value(root.z, frame)};
}

// the scene's root motion now less `back` ticks earlier, local; zero
// for a state with no root motion
// decomp: scene_root_delta 0xAE1E7
FxVec3 scene_root_delta(const World& world, const Fighter& f, const FighterScene& scene,
                        Fx back) {
    const SceneWalks& walks = world.battle->scene_walks;
    const int state = scene.state;
    if (state < 0 || state >= scene_state_count || !walks.roots[state].present) {
        return {};
    }
    const Fx duration = fx(walks.duration[state]);
    const Fx now = fx_clamp(duration - scene.timer, Fx{}, duration);
    const Fx before = fx_clamp(now - back, Fx{}, duration);
    const SceneWalks::Root& root = walks.roots[state];
    return scene_root_sample(f, root, now) - scene_root_sample(f, root, before);
}

// scene_intro_enter, run every update until it moves on
// decomp: scene_intro_enter 0x88F7A
void intro_enter(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    const RosterEntry& entry = entry_of(world, f);
    motion_set_animation(f, *world.battle, 0, state_idle, motion_whole, Fx{});
    f.combat.damage.immune_frames = fx(intro_immune);
    if (f.controller != -1) {
        b.letterbox_state = 1;
        // the camera move, one of four (the scene cameras aren't extracted: docs at the top)
        scene.state = rand_table_next(world.random) % 4 + 1;
    } else {
        freeze(world, f.slot);
        if (battle_other_in_scene(world, f.slot)) {
            return;
        }
        if (battle_ranker_bout(world)) {
            if (scene.door_wait > Fx{}) {
                scene.door_wait -= fx(1);
                return;
            }
        } else if (!stage_door_available(world.stage, scene.killer) && scene.door_wait > Fx{}) {
            scene.door_wait -= fx(1);
            return;
        }
        if (roster_gets_boss_scenes(world, entry) && !b.boss_scene_done) {
            b.letterbox_state = 2;
            scene.state = scene_intro_boss;
            pause_others(world, f);
            supply_craft_pause(world.stage);
            if (!battle_ranker_bout(world)) {
                battle_event(world, BattleEventKind::Music, 6, 0);
            } else {
                battle_event(world, BattleEventKind::Theme, world.story.lineup[b.rumble]);
            }
        }
    }
    if (world.battle_phase == 0 || world.battle_phase == 2) {
        const int bark = entry.ticket.progress.role == 2
                             ? (entry.queue == 1 ? bark_enter_new_boss : bark_enter_beaten_boss)
                             : bark_enter;
        fighter_bark(f, bark);
    }
    scene.step = 1;
}

// decomp: scene_intro_update 0x8920C
void intro_update(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    Fx step = fx(1);
    f.combat.damage.immune_frames = fx(intro_immune);
    const bool boss = roster_gets_boss_scenes(world, entry_of(world, f));
    if (f.controller == -1 && !boss) {
        // an ordinary AI walks out at triple speed and cuts to the end under 180
        if (scene.timer < fx(ticks_walk_end)) {
            scene.timer = fx(1);
            scene.step = 2;
        }
        step = step * 3;
    }
    scene.timer -= step;
    f.motion_phase_prev = f.motion_phase;
    // the walk out of the gate: the root motion over the step (a step of 1 tick moves half an
    // update's way, R1 by itself), the walk clip's phase by the distance over
    // actor_leg_walk_speed (the leg class's root motion walk column, units a clip tick)
    f.position_prev = f.position;
    const FxVec3 walk = scene_root_delta(world, f, scene, step);
    if (scene.timer >= fx(ticks_walk_end)) {
        // from 0.01 moved an update (0.005 a step); with no walk speed half an update's tick
        const Fx len = length(walk);
        if (len >= fx_f(0.005)) {
            f.motion_phase +=
                f.walk_scale > Fx{} ? fx_abs(fx_div_near(len, f.walk_scale)) : fx_f(0.5);
        }
        if (fx(1 + ticks_walk_end) >= scene.timer) {
            if (f.motion_slot_a != clip_idle) {
                motion_set_animation(f, *world.battle, clip_idle, state_idle, motion_whole, Fx{});
            } else {
                f.motion_phase += fx(1);
            }
        }
        if (f.motion_phase >= f.timing.loop_end) {
            f.motion_phase = f.timing.loop_start + f.motion_phase - f.timing.loop_end;
        }
    }
    if (crossed(scene.timer, ticks_gate, step)) {
        gate_open(world.stage, scene.killer);
    }
    if (scene.timer <= step) {
        scene.timer = fx(1);
        scene.step = 2;
        return;
    }
    const FxVec3 moved = turned_by(walk, f.heading);
    f.velocity = {moved.x, f.velocity.y, moved.z};
    if (f.controller != -1) {
        // READY and GO are drawn by presentation from the timer; their sounds and the music here
        if (crossed(scene.timer, ticks_go, step)) {
            battle_event(world, BattleEventKind::Sound, bank_sys, se_battle_intro_second);
        } else if (crossed(scene.timer, ticks_ready, step)) {
            battle_event(world, BattleEventKind::Sound, bank_sys, se_battle_intro_first);
        }
        if (fx(ticks_music) <= scene.timer && fx(ticks_music) + step > scene.timer &&
            (world.battle_phase == 0 || world.battle_phase == 2)) {
            battle_event(world, BattleEventKind::Music, 0x10, 0);
        }
    }
}

// decomp: scene_intro_exit 0x89722
void intro_exit(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    scene.timer = Fx{};
    scene.state = scene_play;
    scene.step = 1;
    motion_set_animation(f, *world.battle, clip_idle, state_idle, motion_whole, Fx{});
    if (f.controller != -1) {
        b.letterbox_state = 3;
        return;
    }
    b.slots[f.slot].ai_frozen = false;
    if (roster_gets_boss_scenes(world, entry_of(world, f)) && !b.boss_scene_done) {
        b.boss_scene_done = true;
        resume_others(world, f);
        supply_craft_resume(world.stage);
        b.letterbox_state = 3;
    }
}

// turn toward the entry point
// decomp: scene_door_enter 0x89F23
void door_enter(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    if (f.controller != -1) {
        b.letterbox_state = 2;
        battle_result_build(world, f);
    }
    const FxVec3 to = scene.focus - f.position;
    scene.door_heading = angle_atan2(to.x, to.z);
    motion_set_animation(f, *world.battle, 0, state_walk, motion_lead_in, Fx{});
    scene.step = 1;
}

// decomp: scene_door_update 0x8A0E1
void door_update(World& world, Fighter& f) {
    FighterScene& scene = world.battle_state.slots[f.slot].scene;
    scene.timer -= fx(1);
    f.motion_phase_prev = f.motion_phase;
    const std::int32_t off = angle_diff(scene.door_heading, f.heading);
    if (off > -static_cast<std::int32_t>(door_turn_near.raw) &&
        off < static_cast<std::int32_t>(door_turn_near.raw)) {
        FxVec3 walk = scene.focus - f.position;
        walk.y = Fx{};
        Fx len = length(walk);
        if (len > door_walk_step) {
            walk = scaled_to(walk, door_walk_step);
            len = door_walk_step;
        }
        f.velocity = {walk.x, f.velocity.y, walk.z};
        if (len >= fx_f(0.005)) {
            // actor_leg_walk_speed as the gate's walk (scene_door_update)
            f.motion_phase += f.walk_scale > Fx{} ? fx_div_near(len, f.walk_scale) : fx_f(0.5);
        } else if (f.motion_slot_a != clip_idle) {
            motion_set_animation(f, *world.battle, clip_idle, state_idle, motion_whole, Fx{});
        } else {
            f.motion_phase += fx(1);
        }
        if (f.motion_phase >= f.timing.loop_end) {
            f.motion_phase = f.timing.loop_start + f.motion_phase - f.timing.loop_end;
        }
    } else {
        // turn at most the step toward the heading; within it, take it
        const std::int32_t most = static_cast<std::int32_t>(door_turn_step.raw);
        const std::int32_t turn = off < -most ? -most : off > most ? most : off;
        f.heading_prev = f.heading;
        f.heading = Angle{f.heading.raw + static_cast<std::uint32_t>(turn)};
        if (turn > -most && turn < most) {
            f.heading = scene.door_heading;
        }
    }
    if (f.controller != -1 && scene.timer >= fx(door_close_ticks) &&
        fx(1 + door_close_ticks) > scene.timer && scene.killer != -1) {
        gate_close(world.stage, scene.killer);
    }
    if (fx(1) >= scene.timer) {
        scene.timer = fx(1);
        scene.step = 2;
    }
}

// the human's battle ends; an AI goes once its gate is free
// decomp: scene_door_exit 0x8A416
void door_exit(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    if (f.motion_slot_a != clip_idle) {
        motion_set_animation(f, *world.battle, clip_idle, state_idle, motion_whole, Fx{});
    } else {
        f.motion_phase += fx(1);
    }
    if (f.motion_phase >= f.timing.loop_end) {
        f.motion_phase = f.timing.loop_start + f.motion_phase - f.timing.loop_end;
    }
    f.velocity = {Fx{}, f.velocity.y, Fx{}};
    if (f.controller != -1) {
        if (b.menu_state != app_free_play && b.menu_state != app_test_drive) {
            b.end_state = 1;
        } else {
            world.battle_phase = 6;
        }
    } else {
        f.combat.damage.immune_frames = fx(door_out_immune);
        f.intangible_frames = fx(door_out_immune);
        if (scene.killer != -1 && stage_door_available(world.stage, scene.killer)) {
            f.combat.defeat_frames = door_retire_steps;
        }
        f.combat.defeat_frames += 1;
    }
    scene.timer = Fx{};
}

} // namespace

// decomp: actor_motion_or_scene 0x88AE9
bool battle_scene_step(World& world, Fighter& f) {
    f.scene_motion = false;
    BattleState& b = world.battle_state;
    if (!b.live) {
        return false;
    }
    // demo battles (phase 1) skip scenes
    const int phase = world.battle_phase;
    if (!(phase == 0 || phase == 2 || phase == 3 || phase == 4 || phase == 6)) {
        return false;
    }
    FighterScene& scene = b.slots[f.slot].scene;
    const int state = scene.state;
    const int step = scene.step;
    if (state == scene_play) {
        return false;
    }
    bool took = true;
    if (state >= scene_intro_first && state <= scene_intro_boss) {
        if (step == 0) {
            intro_enter(world, f);
        } else if (step == 1) {
            intro_update(world, f);
        } else {
            intro_exit(world, f);
        }
    } else if (state == scene_door) {
        if (step == 0) {
            door_enter(world, f);
        } else if (step == 1) {
            door_update(world, f);
        } else {
            door_exit(world, f);
        }
    } else if (state == scene_player_death) {
        if (step == 0) {
            took = death_enter(world, f);
        } else if (step == 1) {
            death_update(world, f);
        } else {
            death_exit(world, f);
        }
    } else if (state >= scene_boss_defeat && state <= scene_boss_defeat_first) {
        if (step == 0) {
            boss_enter(world, f);
        } else if (step == 1) {
            boss_update(world, f);
        } else {
            boss_exit(world, f);
        }
    } else if (scene_is_lesson(state)) {
        // free play's lessons (battle_tutorial.cpp): their scenes run the motion states as play
        battle_tutorial_step(world, f);
        took = false;
    } else if (state >= scene_ending_first && state <= scene_ending_last) {
        // the in-engine ending's shots (battle_ending.cpp)
        if (step == 0) {
            ending_enter(world, f);
        } else if (step == 1) {
            ending_update(world, f);
        } else {
            ending_exit(world, f);
        }
    } else {
        took = false;
    }
    f.scene_motion = took;
    return took;
}

// decomp: actor_step 0x756F3
void battle_door_check(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    if (!b.live) {
        return;
    }
    // stage_entry_point_find from the hit sphere's centre
    const FxVec3 at = f.combat.damage.center;
    int found = -1;
    const std::vector<EntryPoint>& entries = world.battle->entries;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const FxVec3 d = at - entries[static_cast<std::size_t>(i)].position;
        const Fx across = length(FxVec3{d.x, Fx{}, d.z});
        if (d.y < door_height && d.y > Fx{} && across < door_reach) {
            found = i;
            break;
        }
    }
    int& progress = b.slots[f.slot].door_progress;
    if (progress == 0) {
        if (found == -1) {
            progress = 1;
        }
        return;
    }
    if (progress != 1 || found == -1) {
        return;
    }
    progress = 2;
    FighterScene& scene = b.slots[f.slot].scene;
    if (!battle_ranker_bout(world) && f.combat.durability > 0) {
        scene.state = scene_door;
        scene.step = 0;
        scene.timer = fx(door_ticks);
        f.combat.damage.immune_frames = scene.timer;
        f.intangible_frames = scene.timer;
        scene.focus = entries[static_cast<std::size_t>(found)].position;
        scene.heading = entries[static_cast<std::size_t>(found)].heading;
        scene.killer = found;
    }
    const bool low = f.combat.durability * 4 < f.combat.durability_max;
    fighter_bark(f, low ? bark_door_at_25 : bark_door);
}
