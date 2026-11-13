// Port-only dev actions for headless campaign runs (docs/playthrough.md): a kill by seat 0's
// fighter, its death, or a walk out of the nearest gate, asked through Inputs::dev so a replay of
// the fight plays them again. Each goes through the game's own paths: a kill is damage queued
// from the human and settled by actor_settle_damage's port (the beaten byte, the boss scenes, the
// records), the door is the human standing in an entry point's zone (battle_door_check).
#include "battle/battle_dev.hpp"

#include "ai/ai_links.hpp"
#include "mech/fighter_rates.hpp"
#include "world/world.hpp"

namespace {

// a fighter a dev kill may take: present, alive, out of every scene and not immune
bool killable(const World& world, int slot) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    const BattleSlot& s = world.battle_state.slots[slot];
    return f.present && !s.parked && f.combat.durability > 0 && s.scene.state == 0 &&
           f.combat.damage.immune_frames <= Fx{} && f.motion_state != state_dead;
}

// seat 0's fighter can act: present, alive and out of every scene but a lesson's (immune or
// not)
bool active(const World& world, int slot) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    const BattleSlot& s = world.battle_state.slots[slot];
    const int scene = s.scene.state;
    return f.present && !s.parked && f.combat.durability > 0 &&
           (scene == scene_play || scene_is_lesson(scene)) && f.motion_state != state_dead;
}

// the whole durability queued from `by` with its first fitted weapon, settled this step
void lethal_hit(World& world, Fighter& victim, const Fighter& by) {
    DamageState& d = victim.combat.damage;
    d.pending[by.slot] += victim.combat.durability;
    int mount = 0;
    while (mount < 3 && !by.combat.weapons[static_cast<std::size_t>(mount)].present) {
        ++mount;
    }
    d.last_weapon[by.slot] = mount;
    d.last_hit_step[by.slot] = world.step;
}

} // namespace

void battle_dev_apply(World& world, unsigned dev) {
    if (dev == 0 || !world.battle || !world.battle_state.live) {
        return;
    }
    int human = -1;
    for (int slot = 0; slot < battle_slots && human < 0; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        human = f.present && f.controller == 0 ? slot : -1;
    }
    if (human < 0) {
        return;
    }
    Fighter& me = world.fighters[static_cast<std::size_t>(human)];
    if ((dev & dev_shield) != 0 && me.combat.damage.immune_frames < fx(2)) {
        me.combat.damage.immune_frames = fx(2);
    }
    // kill: the fixed opponent first, then one not beaten yet (drawn from queue 0: its kill moves
    // the tournament on), then any; ties to the lowest slot
    if ((dev & (dev_kill | dev_kill_all)) != 0 && active(world, human)) {
        const auto rank = [&](int slot) {
            const RosterEntry& e = battle_roster_entry(world, slot);
            return e.ticket.progress.role == 2 ? 2 : e.queue == 1 ? 1 : 0;
        };
        bool taken[battle_slots] = {};
        for (int n = 0; n < battle_slots; ++n) {
            int pick = -1;
            for (int slot = 0; slot < battle_slots; ++slot) {
                if (slot != human && !taken[slot] && killable(world, slot) &&
                    world.fighters[static_cast<std::size_t>(slot)].controller == -1 &&
                    (pick < 0 || rank(slot) > rank(pick))) {
                    pick = slot;
                }
            }
            if (pick < 0) {
                break;
            }
            taken[pick] = true;
            lethal_hit(world, world.fighters[static_cast<std::size_t>(pick)], me);
            if ((dev & dev_kill_all) == 0) {
                break;
            }
        }
    }
    // die: the nearest opponent's kill
    if ((dev & dev_die) != 0 && active(world, human)) {
        int by = -1;
        Fx best;
        for (int slot = 0; slot < battle_slots; ++slot) {
            const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
            if (slot == human || !f.present || f.combat.durability <= 0) {
                continue;
            }
            const Fx d = length(f.position - me.position);
            if (by < 0 || d < best) {
                by = slot;
                best = d;
            }
        }
        if (by >= 0) {
            lethal_hit(world, me, world.fighters[static_cast<std::size_t>(by)]);
        }
    }
    // door: the human stands in its nearest gate's entry point; the door check finds it there
    if ((dev & dev_door) != 0 && active(world, human) && world.battle->entries.size() > 0) {
        const std::vector<EntryPoint>& entries = world.battle->entries;
        std::size_t near = 0;
        for (std::size_t i = 1; i < entries.size(); ++i) {
            if (length(entries[i].position - me.position) <
                length(entries[near].position - me.position)) {
                near = i;
            }
        }
        BattleSlot& s = world.battle_state.slots[human];
        if (s.door_progress == 0) {
            s.door_progress = 1;
        }
        me.position = entries[near].position;
        me.velocity = FxVec3{};
        me.combat.damage.center = me.position + FxVec3{Fx{}, me.radius, Fx{}};
    }
}
