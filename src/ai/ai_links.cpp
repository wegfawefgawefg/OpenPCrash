// The AI's links to the other systems (ai_links.hpp). Damage, weapons, shots and the lock-on HUD
// answer from src/weapons, camouflage from the fighter's block (mech/fighter_camo.hpp).
#include "ai/ai_links.hpp"

#include "weapons/weapons.hpp"
#include "world/world.hpp"

namespace {

const Fighter& fighter_at(const World& world, int slot) {
    return world.fighters[static_cast<std::size_t>(slot)];
}

ThreatShot threat_of(const World& world, int index) {
    const Shot& shot = world.combat.shots[static_cast<std::size_t>(index)];
    ThreatShot t;
    t.shot = index;
    t.pos = shot.pos;
    t.prev_pos = shot.prev_pos;
    t.velocity = shot.velocity;
    t.owner = shot.weapon >= 0 ? ref_slot(shot.weapon) : -1;
    t.dodge_rolled = shot.dodge_rolled;
    return t;
}

// a tournament rule to the tier ai_camo_reaction_chance reads
// decomp: tournament_rule_tier 0x5F58F
int tournament_rule_tier(int rule) {
    switch (rule) {
    case rule_first_ranker:
        return 0;
    case rule_area_ranker:
        return 1;
    case 1:
        return 2;
    case 2:
        return 3;
    case 3:
        return 4;
    case 4:
        return 5;
    case 0:
        return 6;
    case 0x18: // 24
        return 7;
    default:
        return 8;
    }
}

} // namespace

int fighter_durability(const World& world, int slot) {
    const Fighter& f = fighter_at(world, slot);
    return f.present ? f.combat.durability : 0;
}

int fighter_durability_max(const World& world, int slot) {
    return fighter_at(world, slot).combat.durability_max;
}

bool fighter_alive(const World& world, int slot) {
    const Fighter& f = fighter_at(world, slot);
    return f.present && f.combat.alive;
}

FxVec3 fighter_aim_point(const Fighter& f) {
    return f.combat.aim_point;
}

bool fighter_hit_pending(const World& world, int slot, int attacker) {
    // damage settles every step; the AI (even steps) sees what the passes of the two steps since
    // its last update queued, as retail's update sees the one pass since its last
    const std::int64_t last =
        fighter_at(world, slot).combat.damage.last_hit_step[static_cast<std::size_t>(attacker)];
    return last >= world.step - 2;
}

Fx weapon_resource(const World& world, int slot, int mount) {
    const Weapon& w = fighter_at(world, slot).combat.weapons[mount];
    return w.present ? w.resource : Fx{};
}

void weapon_reach(const World& world, int slot, int mount, Fx* reach_min, Fx* reach_max) {
    const Weapon& w = fighter_at(world, slot).combat.weapons[mount];
    *reach_min = w.present ? w.reach_min : Fx{};
    *reach_max = w.present ? w.reach_max : Fx{};
    // --ai-reach: every fitted weapon's reach, for checking the fire decisions
    if (w.present && world.debug_weapon_reach > Fx{}) {
        *reach_max = world.debug_weapon_reach;
    }
}

// decomp: actor_triggers_update 0x7645E
bool trigger_pressed(const World& world, int slot, int mount) {
    return fighter_at(world, slot).combat.triggers[mount].pressed;
}

void triggers_clear_pressed(World& world, int slot) {
    for (MountTrigger& t : world.fighters[static_cast<std::size_t>(slot)].combat.triggers) {
        t.pressed = false;
    }
}

// decomp: camo_update 0x74E00
int camo_state(const World& world, int slot) {
    return fighter_at(world, slot).camo.state;
}

int camo_max(const World& world, int slot) {
    // the options' sum, whole ticks
    return fx_trunc_div(fighter_at(world, slot).camo.max, fx(1));
}

// decomp: camo_is_state_1_to_4 0x74DAA
bool camo_hides(const World& world, int slot) {
    return camo_hides(fighter_at(world, slot).camo);
}

bool world_shot_threats(const World& world, int slot, ShotThreats* out) {
    *out = ShotThreats{};
    const ShotThreatLists& lists = world.combat.threats[slot];
    out->locked_on_me_count = lists.locked_on_me_count;
    for (int i = 0; i < lists.locked_on_me_count; ++i) {
        out->locked_on_me[static_cast<std::size_t>(i)] = threat_of(world, lists.locked_on_me[i]);
    }
    out->nearby_count = lists.nearby_count;
    for (int i = 0; i < lists.nearby_count; ++i) {
        out->nearby[static_cast<std::size_t>(i)] = threat_of(world, lists.nearby[i]);
    }
    out->melee_count = lists.melee_count;
    for (int i = 0; i < lists.melee_count; ++i) {
        out->melee[static_cast<std::size_t>(i)] = threat_of(world, lists.melee[i]);
    }
    out->beam_count = lists.beam_count;
    for (int i = 0; i < lists.beam_count; ++i) {
        out->beams[static_cast<std::size_t>(i)] = threat_of(world, lists.beams[i]);
    }
    return true;
}

void shot_mark_dodge_rolled(World& world, int shot, int slot) {
    if (shot >= 0 && shot < shot_pool_size && slot >= 0 && slot < fighter_slots) {
        world.combat.shots[static_cast<std::size_t>(shot)].dodge_rolled |=
            static_cast<std::uint16_t>(1u << slot);
    }
}

void reticle_box(const World& world, int slot, Fx* half_x, Fx* half_y) {
    const LockHud& lock = fighter_at(world, slot).combat.hud;
    *half_x = lock.box_a[0];
    *half_y = lock.box_a[1];
}

// decomp: tournament_rule_tier 0x5F58F
int tournament_rule_tier_now(const World& world) {
    // the selected area's tournament (GameState.rumble[area].tournament) and its rule
    const int slot = day_slot_of_selector(world.story);
    const int rule = world.story_tables && slot >= 0 && slot < 4
                         ? tournament_rule(*world.story_tables, world.story.lineup[slot])
                         : -1;
    return rule < 0 ? -1 : tournament_rule_tier(rule);
}

// decomp: pickup_find_nearest 0x67878
int ai_pickup_nearest(const World& world, FxVec3 position, int slot, Fx range) {
    return pickup_find_nearest(world.stage, position, slot, range);
}
FxVec3 pickup_position(const World& world, int pickup) {
    return world.stage.crates[pickup].pos;
}
int pickup_state(const World& world, int pickup) {
    return world.stage.crates[pickup].state;
}
int pickup_kind(const World& world, int pickup) {
    return world.stage.crates[pickup].kind;
}
const RosterEntry& battle_roster_entry(const World& world, int slot) {
    return world.rumble[day_slot_of_selector(world.story)].roster[slot];
}
RosterEntry& battle_roster_entry(World& world, int slot) {
    return world.rumble[day_slot_of_selector(world.story)].roster[slot];
}
