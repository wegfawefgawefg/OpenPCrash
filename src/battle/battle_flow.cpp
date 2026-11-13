// The battle task's own update and what a kill, a death and the end write (decomp
// src/hand/battle_frame.c battle_task_begin, battle_update,
// battle_replacements_update, battle_slot_retire; src/hand/u_088880.c
// battle_other_in_scene, battle_result_build; src/hand/mech_part_assets.c
// actor_settle_damage's kill records; src/hand/loadout_assets.c the ranker and boss tests).
#include "battle/battle_flow.hpp"

#include "ai/ai_links.hpp"
#include "battle/battle_spawn.hpp"
#include "story/roster_tables.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

namespace {

// battle_replacements_update: a draw holds the clock at 300 ticks, wrapping there below 0
constexpr int replace_clock_start = 300;
// a dead AI lies `defeat_frames > 120` updates; the port counts steps (sixty.md R12 E)
constexpr int defeat_retire_steps = 241;
// fighter_retire_wait: 3 updates between the retire and the slot handed back
constexpr int retire_wait_steps = 6;
// the letterbox slides over 30 ticks
constexpr int letterbox_ticks = 30;
// records' caps (actor_settle_damage)
constexpr int kills_cap = 999999;

bool phase_records(const World& world, const BattleState& b) {
    // not in free play, a test drive or a replay
    return b.menu_state != app_free_play && b.menu_state != app_test_drive &&
           world.battle_phase != 2;
}

// battle_letterbox_draw's clock (the bars are drawn by presentation from it)
// decomp: battle_letterbox_draw 0x49000
void letterbox_step(BattleState& b) {
    switch (b.letterbox_state) {
    case 0:
        b.letterbox_time = Fx{};
        return;
    case 1:
        b.letterbox_time = fx(letterbox_ticks);
        break;
    case 2:
        b.letterbox_time += fx(1);
        if (b.letterbox_time >= fx(letterbox_ticks)) {
            b.letterbox_state = 1;
        }
        break;
    case 3:
        b.letterbox_time -= fx(1);
        if (b.letterbox_time <= Fx{}) {
            b.letterbox_state = 0;
        }
        break;
    default:
        break;
    }
    b.letterbox_time = fx_clamp(b.letterbox_time, Fx{}, fx(letterbox_ticks));
}

// the entry emptied, its opponent handed back with the fixed queue
// decomp: battle_slot_retire 0x48DDC
void slot_retire(World& world, int slot) {
    BattleState& b = world.battle_state;
    RosterEntry& entry = world.rumble[b.rumble].roster[slot];
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    entry.player = -2;
    const bool killed = f.combat.durability <= 0 && b.slots[slot].killed_by_player;
    if (world.story_tables && world.roster_tables) {
        release_opponent(world, *world.story_tables, *world.roster_tables, b.rumble, entry, killed,
                         true);
    }
}

// decomp: battle_replacements_update 0x47C1E
void replacements_update(World& world) {
    BattleState& b = world.battle_state;
    if (b.end_state != 0 && b.end_state != 3) {
        return; // the parent reads end_state and ends the battle
    }
    if (battle_other_in_scene(world, -1)) {
        return;
    }
    RumbleSlot& r = world.rumble[b.rumble];
    for (int i = 0; i < r.entrants && i < battle_slots; ++i) {
        RosterEntry& entry = r.roster[i];
        Fighter& f = world.fighters[static_cast<std::size_t>(i)];
        BattleSlot& s = b.slots[i];
        if (entry.player == 0) {
            continue; // the human's slot
        }
        // actor_task_destroy: the fighter stops (fighter_retire_wait), the slot follows 3 updates on
        if (entry.player != -2 && f.present && f.combat.defeat_frames > defeat_retire_steps) {
            f.present = false;
            s.parked = true;
            s.retire_left = retire_wait_steps;
        }
        if (b.loading_slot == -1 && entry.player == -2) {
            // battle_slot_draw_opponent: choose_opponent, materialize_actor; the port's load is
            // the presentation's and done before the clock runs out, so the slot is ready at once
            RumbleTicket ticket;
            int queue = 0;
            if (choose_opponent(world, b.rumble, ticket, queue)) {
                if (world.roster_tables) {
                    materialize_actor(world, *world.roster_tables, b.rumble, i, ticket, queue);
                }
                b.loading_slot = i;
                battle_slot_prepare(world, i);
            }
            b.replace_clock = fx(replace_clock_start);
        }
    }
    if (fx(1) > b.replace_clock) {
        if (b.loading_slot != -1) {
            battle_spawn(world, b.loading_slot);
            b.loading_slot = -1;
        }
    }
}

// demo_battle_enter_next, the parent of the demo's battle task (before it): the clock 2 ticks an
// update; past 600, the fighter gone or lying over 60 updates (121 steps), one draw for another
// slot, (slot + rand % 5 + 1) % 6, taken if it is alive and running. Once an update: on even
// steps (sixty.md R11, the world generator)
// decomp: demo_battle_enter_next 0x4A70C
void demo_follow_update(World& world) {
    BattleState& b = world.battle_state;
    if (world.step % 2 != 0) {
        return;
    }
    b.demo_clock += 2;
    const Fighter& f = world.fighters[static_cast<std::size_t>(b.demo_slot)];
    if (b.demo_clock > 600 || !f.present || f.combat.defeat_frames > 121) {
        const int slot = (b.demo_slot + rand_table_next(world.random) % 5 + 1) % battle_slots;
        const Fighter& next = world.fighters[static_cast<std::size_t>(slot)];
        if (next.present && next.combat.durability > 0 && !b.slots[slot].parked) {
            b.demo_slot = slot;
            b.demo_clock = 0;
        }
    }
    if (world.fighters[static_cast<std::size_t>(b.demo_slot)].present) {
        b.demo_followed = true;
    } else {
        b.demo_clock = 600;
    }
}

} // namespace

void battle_event(World& world, BattleEventKind kind, int a, int b) {
    BattleState& s = world.battle_state;
    if (s.event_count < battle_event_max) {
        s.events[static_cast<std::size_t>(s.event_count++)] = {kind, world.step, a, b};
    }
}

void battle_state_begin(World& world, int menu_state) {
    BattleState& b = world.battle_state;
    const BattleState keep = b; // the area select's gates and the result's rental flag
    b = BattleState{};
    b.live = true;
    b.menu_state = menu_state;
    b.rumble = rumble_slot_of_selector(world.story.rumble_selected);
    for (int i = 0; i < 4; ++i) {
        b.player_gate[i] = keep.player_gate[i];
    }
    b.ranker_roll = keep.ranker_roll;
    b.free_play_lesson = keep.free_play_lesson;
    b.scene_text_serial = keep.scene_text_serial; // kept counting so an old answer can't match
    b.result.rented = keep.result.rented;
    b.end_state = world.battle_phase == 1 ? 3 : 0;
    // rumble_fixed_opponent_queue: the fixed opponent queued once every pool is done
    if (world.story_tables) {
        rumble_fixed_opponent_queue(world, *world.story_tables, b.rumble);
    }
}

// decomp: battle_update 0x46F01
void battle_update(World& world) {
    BattleState& b = world.battle_state;
    b.event_count = 0;
    if (!b.live) {
        return;
    }
    if (world.battle_phase == 1) {
        demo_follow_update(world);
    }
    // the letterbox (not in replays), the replacements, the frame and the clock
    if (world.battle_phase != 2) {
        letterbox_step(b);
    }
    replacements_update(world);
    b.frame += 1;
    b.replace_clock -= fx(1);
    if (b.replace_clock < Fx{}) {
        b.replace_clock = fx(replace_clock_start);
    }
    // fighter_retire_wait: the retired slots handed back
    for (int i = 0; i < battle_slots; ++i) {
        BattleSlot& s = b.slots[i];
        if (s.retire_left > 0 && --s.retire_left == 0) {
            slot_retire(world, i);
        }
    }
}

// decomp: battle_other_in_scene 0x9191E
bool battle_other_in_scene(const World& world, int slot) {
    if (world.battle_phase == 4) {
        return false;
    }
    const BattleState& b = world.battle_state;
    for (int i = 0; i < battle_slots; ++i) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(i)];
        if (i == slot || !f.present) {
            continue;
        }
        const int state = b.slots[i].scene.state;
        if (f.controller != -1) {
            if (state != 0) {
                return true;
            }
        } else if (battle_roster_entry(world, i).ticket.progress.role == 2 && state != 0 &&
                   state != 6) {
            return true;
        }
    }
    return false;
}

// decomp: tournament_is_ranker_bout 0x82BD4, actor_gets_boss_scenes 0x82C7F
bool battle_ranker_bout(const World& world) {
    // today's tournament of the selected area (game_tournament_slot, raw schedule word)
    if (!world.story_tables) {
        return false;
    }
    const int area = world.story.rumble_selected / 2;
    if (area < 0 || area > 3) {
        return false;
    }
    const DayRow& day = day_row(*world.story_tables, world.story.day);
    int t = day.tournament[area];
    if (t == world.story_tables->first_ranker_tournament && world.story.game_beaten) {
        t = world.story_tables->burning_chrome_tournaments[0];
    }
    // retail reads slot 3's word (the chip shop's row) as a tournament for selectors 6 and 7; the
    // port has none there (docs/bugs.md, the day-58 door)
    if (area == 3 || t < 0 || t >= tournament_count) {
        return false;
    }
    const int rule = world.story_tables->tournaments[static_cast<std::size_t>(t)].rule;
    return rule == rule_area_ranker || rule == rule_first_ranker;
}

bool roster_gets_boss_scenes(const World& world, const RosterEntry& entry) {
    if (entry.ticket.progress.role != 2) {
        return false;
    }
    return entry.queue == 1 || battle_ranker_bout(world);
}

const BuildRecord& battle_slot_build(const World& world, int slot) {
    return world.battle_state.slots[slot < 0 || slot >= battle_slots ? 0 : slot].build;
}

// decomp: actor_settle_damage 0x766DE
void battle_kill(World& world, Fighter& victim, Fighter& killer) {
    BattleState& b = world.battle_state;
    if (!b.live || killer.controller == -1) {
        return;
    }
    // a human's kill: the beaten flag, the records, the fixed opponent's scenes
    b.slots[victim.slot].killed_by_player = true;
    RosterEntry& entry = battle_roster_entry(world, victim.slot);
    if (phase_records(world, b)) {
        Records& records = world.profile.records;
        records.kills = records.kills + 1 > kills_cap ? kills_cap : records.kills + 1;
        if (victim.body_class == 6) {
            records.drones_shot_down =
                records.drones_shot_down + 1 > kills_cap ? kills_cap : records.drones_shot_down + 1;
        }
    }
    if (entry.ticket.progress.role == 2) {
        b.result.fixed_opponent_destroyed = true;
        // a boss whose death ends the fight: the result now, scene 0x13 for the boss
        if (roster_gets_boss_scenes(world, entry)) {
            battle_result_build(world, killer);
            b.slots[victim.slot].scene.state = scene_boss_defeat;
            b.slots[victim.slot].scene.step = 0;
        }
    }
    // the opponent's ticket byte: beaten (for the fixed opponent, tournament_won)
    if (entry.ticket.opponent >= 0) {
        opponent_beaten(world.story, entry.ticket.progress);
    }
}

void battle_death(World& world, Fighter& victim, int killer_slot, bool killer_known) {
    BattleState& b = world.battle_state;
    if (!b.live || victim.controller == -1) {
        return;
    }
    // a human dying in a tournament: scene 7 watches the killer, frozen and safe for 840 ticks
    FighterScene& scene = b.slots[victim.slot].scene;
    scene.state = 7;
    scene.step = 0;
    scene.timer = Fx{};
    if (killer_known) {
        Fighter& killer = world.fighters[static_cast<std::size_t>(killer_slot)];
        scene.killer = killer_slot;
        killer.intangible_frames = fx(840);
        scene.focus = killer.position;
        scene.heading = killer.heading;
        b.slots[killer_slot].ai_frozen = true;
        killer.control = ControlRecord{};
        world.ai[static_cast<std::size_t>(killer_slot)].record = ControlRecord{};
    } else {
        scene.killer = -1;
    }
}

// decomp: battle_result_build 0x919E1
void battle_result_build(World& world, const Fighter& f) {
    BattleState& b = world.battle_state;
    if (!b.live || world.battle_phase != 0 || !world.roster_tables) {
        return;
    }
    const RosterTables& t = *world.roster_tables;
    BattleResult& r = b.result;
    const RosterBuild& build = battle_roster_entry(world, f.slot).build;
    r.destruction_count = f.combat.kills;
    r.elapsed = b.frame; // Battle.frame x ticks: the port's frame counts ticks
    r.money_before = world.story.money;
    r.prize = f.combat.score;
    if (r.rented) {
        r.prize = static_cast<int>(static_cast<unsigned>(r.prize) >> 1);
    }
    // a repair is 0.2 x the part's tune-0 price + 0.01 x its tuned price (float, as retail)
    const auto repair = [&](int kind, int id, int tune) {
        const int tuned = part_shop_price(t, kind, id, tune);
        const int base = part_shop_price(t, kind, id, 0);
        return static_cast<int>(static_cast<float>(static_cast<unsigned>(base)) * 0.2f +
                                static_cast<float>(static_cast<unsigned>(tuned)) * 0.01f);
    };
    const bool alive = f.combat.durability > 0;
    if (alive) {
        r.won = true;
        r.body_leg_repair = 0;
    } else {
        r.won = false;
        const int tuned = part_shop_price(t, 0, build.body, build.body_tune) +
                          part_shop_price(t, 1, build.leg, build.leg_tune);
        const int base = part_shop_price(t, 0, build.body, 0) + part_shop_price(t, 1, build.leg, 0);
        r.body_leg_repair =
            static_cast<int>(static_cast<float>(static_cast<unsigned>(base)) * 0.2f +
                             static_cast<float>(static_cast<unsigned>(tuned)) * 0.01f);
    }
    r.damaged_weapons = 0;
    for (int i = 0; i < 4; ++i) {
        const bool broken = f.combat.weapon_broken[i];
        if (!broken && alive) {
            r.weapon_repair[i] = 0;
            continue;
        }
        const RosterWeapon& w = build.weapon[i];
        r.weapon_repair[i] = w.id != -1 ? repair(i >= 2 ? 3 : 2, w.id, w.tune) : 0;
        if (broken) {
            r.damaged_weapons += 1;
        }
    }
    r.repair_total = r.body_leg_repair;
    for (const int w : r.weapon_repair) {
        r.repair_total += w;
    }
}
