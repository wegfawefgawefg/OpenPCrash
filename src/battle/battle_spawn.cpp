#include "battle/battle_spawn.hpp"

#include "ai/ai.hpp"
#include "ai/ai_links.hpp"
#include "story/roster_tables.hpp"
#include "world/world.hpp"

#include <array>

namespace {

// scene_gate_entry_start: the entrance's 540 ticks (scene_duration of states 1..5), its 300 ticks
// of immunity and the 120-tick wait for a busy gate
constexpr int entrance_ticks = 540;
constexpr int entrance_immune = 300;
constexpr int gate_wait_ticks = 120;

// the first point of the first node from `start` on (wrapping)
// that isn't a door path or kind 6 or 7
// decomp: enpt_scan_placement_nodes 0x57F4C
FxVec3 placement_node_point(const NavGraph& nav, int start) {
    const int count = static_cast<int>(nav.nodes.size());
    if (count == 0) {
        return {};
    }
    start = start < 0 ? 0 : start % count;
    for (int i = 0; i < count; ++i) {
        const int kind = nav.nodes[static_cast<std::size_t>(start)].kind;
        if (kind == 0 || kind == 1 || kind == 6 || kind == 7) {
            start = (start + 1) % count;
            continue;
        }
        break;
    }
    return nav.nodes[static_cast<std::size_t>(start)].points.front();
}

// an out-of-range point reads row 0 at the bad index in
// retail (battle.md bug 3); the port takes point 0
// decomp: stage_entry_transform_copy 0x4474C
EntryPoint entry_point(const World& world, int gate) {
    const std::vector<EntryPoint>& entries = world.battle->entries;
    if (gate < 0 || gate >= static_cast<int>(entries.size())) {
        return entries.front();
    }
    return entries[static_cast<std::size_t>(gate)];
}

} // namespace

BuildRecord build_record_of(const RosterBuild& b) {
    BuildRecord r;
    r.body = b.body;
    r.body_tune = b.body_tune;
    r.leg = b.leg;
    r.leg_tune = b.leg_tune;
    r.chip = b.chip;
    r.chip_tune = b.chip_tune;
    for (int w = 0; w < 4; ++w) {
        r.weapon[w] = b.weapon[w].id;
        r.weapon_tune[w] = b.weapon[w].tune;
    }
    r.colour = b.color;
    r.sticker = b.sticker;
    for (int k = 0; k < 10; ++k) {
        r.option[k] = b.option[k];
    }
    return r;
}

void battle_slot_prepare(World& world, int slot) {
    BattleSlot& s = world.battle_state.slots[slot];
    const RosterEntry& entry = battle_roster_entry(world, slot);
    s.build = build_record_of(entry.build);
    // an opponent is posed as its loadout's row (tunes don't change a rig)
    if (entry.ticket.opponent >= 0 && world.roster_tables) {
        s.rig = opponent_entry(*world.roster_tables, entry.ticket.opponent).loadout;
    }
}

// decomp: battle_actor_position_resolve 0x8255F
int battle_position_resolve(World& world, int slot, FxVec3& position, Angle& heading) {
    BattleState& b = world.battle_state;
    RosterEntry& entry = battle_roster_entry(world, slot);
    const int count = static_cast<int>(world.battle->entries.size());
    int gate = -1;
    heading = Angle{};
    if (entry.player != -1 && entry.player != -2) {
        // a human: free play (but lesson 3) at the origin, else the gate picked in the area select
        if (b.menu_state == app_free_play && b.free_play_lesson != 3) {
            position = {};
            return -1;
        }
        gate = b.player_gate[entry.player & 3];
        const EntryPoint at = entry_point(world, gate);
        position = at.position;
        heading = at.heading;
        return gate;
    }
    if (world.battle_phase == 0 || world.battle_phase == 2) {
        if (battle_ranker_bout(world) && b.ranker_roll >= 0 && b.ranker_roll < count) {
            gate = b.ranker_roll;
        } else if (!entry.start_set) {
            gate = rand_table_next(world.random) % count;
            for (int tried = 0; !stage_door_available(world.stage, gate) && tried < count; ++tried) {
                gate = (gate + 1) % count;
            }
        } else {
            position = entry.start;
            entry.start_set = false;
            return -1;
        }
        const EntryPoint at = entry_point(world, gate);
        position = at.position;
        heading = at.heading;
        return gate;
    }
    // free play: entry point slot % count
    if (b.menu_state == app_free_play && count > 0) {
        gate = slot % count;
        const EntryPoint at = entry_point(world, gate);
        position = at.position;
        heading = at.heading;
        return gate;
    }
    // demo battles and the ending: a random navigation node
    position = placement_node_point(world.battle->ai.nav, rand_table_next(world.random));
    return -1;
}

// decomp: actor_task_update 0x44A2E
void battle_spawn(World& world, int slot) {
    BattleState& b = world.battle_state;
    BattleSlot& s = b.slots[slot];
    RosterEntry& entry = battle_roster_entry(world, slot);
    s.scene = FighterScene{};
    s.ai_frozen = false;
    s.door_progress = 0;
    s.killed_by_player = false;
    s.parked = false;
    s.retire_left = 0;
    // the place, then the view: humans counted as they spawn (Battle.view_count); a human's
    // controller is its seat, RosterEntry.player (in VS its pad: its view, HUD and record)
    EntryPoint at;
    const int gate = battle_position_resolve(world, slot, at.position, at.heading);
    const int controller = entry.player >= 0 ? (entry.player & 3) : -1;
    if (controller >= 0) {
        b.view_count += 1;
    }
    Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    fighter_spawn_record(f, *world.battle, slot, controller, s.build, s.rig, at);
    f.restrict_input = b.menu_state == app_free_play; // actor_step's actor_input_restrict
    // camera_init: a human outside VS starts in the view it left its last fight in
    // (camera_view_get: anything outside 0..5 is 1); the AI and VS in view 1
    if (controller >= 0 && !world.vs.mode) {
        const int view = world.profile.options.camera_view;
        camera_view_cut(f.camera, view >= 0 && view < 6 ? view : 1);
    }
    // a human's buttons and control type are its seat's button config (actor_init reads
    // PlayerConfig.keys, and the control type player_config_flags_update makes of the two stick
    // rows: the default config is 3, Standard and View Change Normal); the AI keeps the defaults
    // and 0
    if (controller >= 0) {
        const ButtonConfig& buttons = world.player_buttons[controller];
        std::array<int, 8> keys{};
        for (int k = 0; k < 8; ++k) {
            keys[static_cast<std::size_t>(k)] = buttons.keys[k];
        }
        f.bindings = control_bindings_from_keys(keys);
        f.control_type = button_control_type(buttons);
        // beginner mode: the config's, but the lessons load the defaults (lesson 3 doesn't)
        f.camo.beginner =
            buttons.beginner != 0 && !(b.menu_state == app_free_play && b.free_play_lesson != 3);
    }
    f.combat.destruction_value = entry.destruction_value;
    if (controller == -1) {
        // ai_enter on a cleared controller (the level bug), then the roster's level and profile
        ai_spawn(world, slot, entry.level, entry.ai_profile);
    }
    // the scene: free play's human its lesson's tutorial (battle_tutorial.cpp) or, lesson 3, the
    // gate's entrance with the controls free; through a gate the entrance (scene 1), else playing
    // at once
    s.scene.door_wait = Fx{};
    if (b.menu_state == app_free_play && controller >= 0) {
        b.letterbox_state = 0;
        s.scene.step = 0;
        switch (b.free_play_lesson) {
        case 1:
            s.scene.state = scene_lesson_weapons_hello;
            return;
        case 2:
            s.scene.state = scene_lesson_camo_hello;
            return;
        case 3:
            break; // the gate's entrance below, controls unlocked
        default:
            s.scene.state = scene_lesson_hello;
            return;
        }
    }
    // the ending (battle phase 3): the human into its first shot with the durability and broken
    // weapons the last boss's defeat left (its weapons' rounds come back in each shot's lead-in)
    if (world.battle_phase == 3 && controller >= 0) {
        b.letterbox_state = 1;
        s.scene.state = scene_ending_first;
        s.scene.step = 0;
        const EndingCarry& carry = world.story.ending_carry;
        if (carry.carry) {
            f.combat.durability = carry.durability;
            for (int m = 0; m < 4; ++m) {
                f.combat.weapon_broken[m] = carry.broken[m];
            }
        }
        return;
    }
    if (gate >= 0) {
        s.scene.state = scene_intro_first;
        s.scene.step = 0;
        s.scene.timer = fx(entrance_ticks);
        f.combat.damage.immune_frames = fx(entrance_immune);
        s.scene.focus = f.position;
        s.scene.heading = f.heading;
        s.scene.killer = gate;
        s.scene.door_wait = fx(gate_wait_ticks);
        if (b.menu_state == app_free_play && controller >= 0) {
            control_unlock(f); // lesson 3
        } else if (b.menu_state == app_free_play) {
            // free play's AIs: frozen with their controls cleared, unable to fire or turn the
            // view, no immunity; lesson 2's target plays at once
            s.ai_frozen = true;
            f.control = ControlRecord{};
            f.control_allow = lesson_allow;
            f.combat.damage.immune_frames = Fx{};
            if (b.free_play_lesson == 2) {
                s.scene.state = scene_play;
                s.scene.step = 1;
                s.scene.killer = 0;
            }
        }
        return;
    }
    s.scene.state = scene_play;
    s.scene.step = 1;
    s.scene.killer = 0;
}

// decomp: camera_view_save 0x7E837
void battle_camera_view_save(World& world) {
    if (!world.battle || world.battle_phase != 0 || world.vs.mode ||
        battle_roster_entry(world, 0).player != 0) {
        return;
    }
    const Fighter& f = world.fighters[0];
    if (f.present) {
        world.profile.options.camera_view = f.camera.view;
    }
}
