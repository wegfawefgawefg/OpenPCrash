// pickup_apply's fighter side, actor_heal_fraction 0x6AC5A and
// actor_ammo_refill_fraction (decomp src/hand/falling_debris.c). Integer arithmetic gives
// retail's float results: max x 0.1f and the like round up from the exact product, so the
// truncations land on the same whole numbers.
#include "stage/pickup_apply.hpp"

#include "ai/ai_links.hpp"
#include "audio/audio.hpp"
#include "weapons/weapons.hpp"
#include "world/world.hpp"

#include <cstdint>

namespace {

// the score crates, in units of 10 NY (kinds 0..3)
constexpr int crate_score[4] = {100, 500, 1000, 10000};
// repair and refill shares in tenths (kinds 4..6: 0.1, 0.3, 0.5; 7..9: 0.3, 0.5, 1.0)
constexpr int repair_tenths[3] = {1, 3, 5};
constexpr int refill_tenths[3] = {3, 5, 10};
constexpr int fixed_opponent_role = 2;
constexpr int pickup_sound = 0x26; // bank 3

// actor_heal_fraction: durability += (int)(max x share), halved for a fixed opponent, at most max
void heal(World& world, Fighter& f, int tenths) {
    FighterCombat& c = f.combat;
    const int role = battle_roster_entry(world, f.slot).ticket.progress.role;
    const std::int64_t denominator = role == fixed_opponent_role ? 20 : 10;
    c.durability += static_cast<int>(std::int64_t{c.durability_max} * tenths / denominator);
    if (c.durability > c.durability_max) {
        c.durability = c.durability_max;
    }
}

// actor_ammo_refill_fraction: one of the fitted, not full, non-laser, non-melee weapons, drawn
// with rand_float, gains floor(resource_max x share), at most full; the gauge's refill blink
void refill(World& world, Fighter& f, int tenths) {
    FighterCombat& c = f.combat;
    int eligible[4];
    int count = 0;
    for (int mount = 0; mount < 4; ++mount) {
        const Weapon& w = c.weapons[mount];
        if (w.fitted && w.record && w.record->behavior != 5 && w.record->behavior != 6 &&
            fx(w.resource_max) > w.resource) {
            eligible[count++] = mount;
        }
    }
    if (count == 0) {
        return;
    }
    const int roll = rand_int(world.random, count);
    if (roll < 0 || roll >= count) {
        return;
    }
    Weapon& w = c.weapons[eligible[roll]];
    w.resource += fx(static_cast<int>(std::int64_t{w.resource_max} * tenths / 10));
    if (w.resource > fx(w.resource_max)) {
        w.resource = fx(w.resource_max);
    }
    combat_event(world, event_gauge_refill, f.slot, w.mount, f.position);
}

void apply(World& world, Fighter& f, int kind, FxVec3 at) {
    FighterCombat& c = f.combat;
    // effect_get_item: the "get" set by kind from the crate toward the collector (src/effects)
    stage_event(world.stage, StageEventKind::GetItem, at, f.slot, kind);
    if (!c.alive) {
        return;
    }
    if (f.controller != -1) {
        stage_event(world.stage, StageEventKind::Sound, f.position, bank_battle_player,
                    pickup_sound);
    }
    if (kind >= 0 && kind <= 3) {
        c.score += crate_score[kind];
    } else if (kind >= 4 && kind <= 6) {
        heal(world, f, repair_tenths[kind - 4]);
        fighter_bark(f, std::int64_t{c.durability} * 4 < c.durability_max ? bark_armor_pickup_at_25
                                                                          : bark_armor_pickup);
    } else if (kind >= 7 && kind <= 9) {
        refill(world, f, refill_tenths[kind - 7]);
        // the fraction as the last settle left it
        if (c.ammo_fraction < fx_ratio(1, 2)) {
            fighter_bark(f, bark_ammo_pickup_low);
        } else if (c.ammo_fraction < fx(1)) {
            fighter_bark(f, bark_ammo_pickup);
        }
    }
}

} // namespace

void pickups_apply(World& world, Fighter& f) {
    for (int i = 0; i < 3; ++i) {
        if (f.crate_kind[i] >= 0) {
            apply(world, f, f.crate_kind[i], f.crate_at[i]);
            f.crate_kind[i] = -1;
        }
    }
}
