#pragma once
// A crate's fighter side (decomp src/hand/falling_debris.c pickup_apply,
// actor_heal_fraction, actor_ammo_refill_fraction; docs/port/battle.md "Supply
// craft and crates"): kinds 0..3 score 100, 500, 1000, 10000; 4..6 repair 10, 30, 50% of the
// maximum (halved for a fixed opponent); 7..9 refill 30, 50, 100% of one random fitted, not full,
// non-laser, non-melee weapon. Then the bark by what it did.

struct World;
struct Fighter;

// the crates the fighter's sphere query took this step (Fighter.crate_kind), in crate order:
// the "get" effect toward the collector, a human's sound, the score, repair or refill and the bark.
// Run after fighter_step, before the settle, as retail's runs inside sv_contact_route
void pickups_apply(World& world, Fighter& f);
