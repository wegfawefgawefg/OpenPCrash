// The fighters' ending scenes (decomp src/hand/u_088880.c scene_player_death_* 0x8A55A..,
// scene_boss_defeat_* 0x897E1..; docs/port/battle.md "How a battle ends"): a human's death and the
// killer's moment, and a boss's defeat. The entrance, the door and the dispatch are
// battle_scenes.cpp's.
#include "battle/battle_scene_parts.hpp"

#include "ai/ai_links.hpp"
#include "mech/fighter_rates.hpp"
#include "story/rumble.hpp"

namespace scenes {

namespace {

// the killer's weapon made to fire at the wreck (scene_player_death_enter's writes through
// last_weapon: the trigger's fire and press, fitted, reloaded, full)
void killer_fire(World& world, Fighter& victim, int fire) {
    FighterScene& scene = world.battle_state.slots[victim.slot].scene;
    const int ref = victim.combat.damage.last_weapon[scene.killer];
    const int mount = ref >= 0 ? ref % 5 : -1;
    if (mount < 0 || mount > 3) {
        return;
    }
    Fighter& killer = world.fighters[static_cast<std::size_t>(scene.killer)];
    killer.combat.triggers[mount].fire = fire;
    killer.combat.triggers[mount].pressed = true;
    killer.reticle_center_world = victim.position;
    killer.combat.reticle_track_world = victim.position;
}

} // namespace

// the wreck lies 330 ticks, then the killer's moment
// decomp: scene_player_death_enter 0x8A55A
bool death_enter(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    scene.timer += fx(1);
    if (scene.timer > fx(death_watch)) {
        battle_result_build(world, f);
        const int ref =
            scene.killer >= 0 ? f.combat.damage.last_weapon[scene.killer] : -1;
        // retail plays the killer's moment only live (battle phase 0), so its replay of a death
        // ends differently; here a replay plays it too (docs/bugs.md)
        const bool shown = world.battle_phase == 0 || world.battle_phase == 2;
        if (shown && scene.killer != -1 && ref >= 0 && ref % 5 < 4) {
            scene.timer = fx(death_killer_cam);
            Fighter& killer = world.fighters[static_cast<std::size_t>(scene.killer)];
            Weapon& w = killer.combat.weapons[ref % 5];
            w.fitted = true;
            w.reload_left = 0;
            w.resource = fx(w.resource_max);
            killer_fire(world, f, 3);
            killer.position = scene.focus;
            killer.heading = scene.heading;
            freeze(world, scene.killer);
            motion_set_animation(killer, *world.battle, clip_idle, state_idle, motion_whole, Fx{});
            scene.step = 1;
            b.letterbox_state = 2;
        } else {
            scene.killer = f.slot;
            scene.timer = fx(death_tail);
            scene.step = 2;
        }
    }
    return false; // sv_motion_dispatch runs as well
}

// decomp: scene_player_death_update 0x8A7A6
void death_update(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    scene.timer -= fx(1);
    if (scene.timer < Fx{}) {
        scene.timer = fx(death_tail);
        scene.step = 2;
        return;
    }
    if (scene.timer > fx(death_watch)) {
        killer_fire(world, f, 2);
        f.motion_phase = Fx{};
        if (fx(1 + death_watch) >= scene.timer) {
            // the wreck burns out; the killer is free again and the camera turns to the wreck
            b.slots[scene.killer].ai_frozen = false;
            scene.killer = f.slot;
        }
        return;
    }
    f.motion_phase += fx(1);
    if (f.motion_phase > f.timing.end) {
        f.motion_phase = f.timing.end;
    }
}

// the battle ends (every update from here)
// decomp: scene_player_death_exit 0x8AAEA
void death_exit(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    if (b.menu_state != app_free_play && b.menu_state != app_test_drive) {
        b.end_state = 2;
    } else {
        world.battle_phase = 6;
    }
    scene.timer -= fx(1);
    f.motion_phase += fx(1);
    if (f.motion_phase > f.timing.end) {
        f.motion_phase = f.timing.end;
    }
}

// decomp: scene_boss_defeat_enter 0x897E1
void boss_enter(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    f.motion_phase += fx(1);
    if (f.motion_phase >= f.timing.end) {
        f.motion_phase = f.timing.end;
    }
    const RosterEntry& entry = entry_of(world, f);
    if (f.controller == -1 && roster_gets_boss_scenes(world, entry)) {
        scene.timer = fx(boss_ticks);
        scene.focus = f.position;
        scene.heading = f.heading;
        b.letterbox_state = 2;
        pause_others(world, f);
        supply_craft_pause(world.stage);
        motion_set_animation(f, *world.battle, clip_death, state_idle, motion_whole, Fx{});
        const int tournament = world.story.lineup[b.rumble];
        const int rule = world.story_tables ? tournament_rule(*world.story_tables, tournament) : -1;
        const int area = world.story.rumble_selected / 2;
        bool line = false;
        if (rule == rule_first_ranker) {
            // the First Ranker: a running bark or line stops
            battle_event(world, BattleEventKind::SceneText, -1);
            scene.state = scene_boss_defeat_first;
            scene.timer = fx(first_ranker_ticks);
        } else if (rule == rule_area_ranker && area >= 0 && area < 3) {
            // the area ranker's defeat line (ranker_defeat_scripts) plays only if it came unbeaten
            scene.state = scene_boss_defeat_area + area;
            line = true;
        }
        if (entry.queue != 1 && line) {
            b.letterbox_state = 3;
            line = false;
        }
        // scene_text_start (a tournament only): the line over any bark; the scene waits out its
        // length, measured from the script (BattleData::ranker_line_ticks)
        scene.text_left = Fx{};
        // a replay waits the line out unheard (retail's replay starts no line and ends the
        // scene sooner: docs/bugs.md)
        if (line && (world.battle_phase == 0 || world.battle_phase == 2)) {
            if (world.battle_phase == 0) {
                battle_event(world, BattleEventKind::SceneText, area);
            }
            scene.text_left = fx(world.battle->ranker_line_ticks[area]);
        }
    } else {
        scene.timer = Fx{};
        scene.state = 0;
        scene.step = 1;
    }
    scene.step = 1;
}

// at least 180 ticks, and until the battle's script task (the
// ranker's line) has ended. A bark running then isn't waited for: barks are presentation's,
// picked by CRT rand, and none is longer than the 180 (docs/bugs.md)
// decomp: scene_boss_defeat_update 0x89B39
void boss_update(World& world, Fighter& f) {
    FighterScene& scene = world.battle_state.slots[f.slot].scene;
    scene.timer -= fx(1);
    if (scene.text_left > Fx{}) {
        scene.text_left -= fx(1);
    }
    f.motion_phase_prev = f.motion_phase;
    f.motion_phase += fx(1);
    scene.focus = f.position;
    if (f.motion_phase >= f.timing.end) {
        f.motion_phase = f.timing.end;
    }
    if (fx(1) >= scene.timer) {
        scene.timer = fx(1);
        if (scene.text_left <= Fx{}) {
            scene.step = 2;
        }
    }
    f.velocity = {Fx{}, f.velocity.y, Fx{}};
}

// decomp: scene_boss_defeat_exit 0x89C9B
void boss_exit(World& world, Fighter& f) {
    BattleState& b = world.battle_state;
    FighterScene& scene = b.slots[f.slot].scene;
    motion_set_animation(f, *world.battle, clip_death, state_dead, motion_lead_in, f.timing.end);
    if (scene.state == scene_boss_defeat_first) {
        // the First Ranker: the battle ends into the ending (the settlement is the front end's)
        b.ending_requested = true;
        if (world.battle_phase == 0) {
            world.battle_phase = 6;
        }
        // fighter 0's machine as it stands, for the ending's shots
        const Fighter& player = world.fighters[0];
        if (player.present) {
            EndingCarry& carry = world.story.ending_carry;
            for (int m = 0; m < 4; ++m) {
                carry.resource[m] = player.combat.weapons[m].resource;
                carry.fitted[m] = player.combat.weapons[m].fitted;
                carry.broken[m] = player.combat.weapon_broken[m];
            }
            carry.durability = player.combat.durability;
            carry.carry = true;
        }
        return;
    }
    if (scene.state != scene_boss_defeat) {
        b.end_state = 1;
        return;
    }
    scene.state = 0;
    scene.step = 1;
    if (f.controller == -1 && roster_gets_boss_scenes(world, entry_of(world, f))) {
        resume_others(world, f);
        supply_craft_resume(world.stage);
        b.letterbox_state = 3;
    }
}

} // namespace scenes
