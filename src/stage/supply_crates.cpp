// What shots and fighters ask of the supply craft and its crates (decomp
// src/hand/falling_debris.c; docs/port/battle.md, "The supply craft and the crates"): a shot's
// hit on them and the nearest crate a fighter can take. supply_craft.cpp steps them.
#include "fx/hit.hpp"
#include "stage/stage_world.hpp"

// decomp: supply_craft_hit_test 0x67583
int supply_craft_hit_test(StageWorld& stage, FxVec3 from, FxVec3 to, FxVec3* at) {
    int bits = 0;
    // a parked craft (out of sight at its route's start, or never flown) isn't a target: retail
    // leaves it shootable for 1000 (docs/bugs.md)
    const bool flying = stage.craft.state != craft_off && stage.craft.state != craft_parked;
    if (stage.craft.shootable && flying &&
        segment_sphere_entry(from, to, stage.craft.pos, fx(3), at)) {
        bits |= hit_craft;
        stage.craft.shootable = false;
    }
    for (Crate& crate : stage.crates) {
        if (crate.shootable && segment_sphere_entry(from, to, crate.pos, fx(1), at)) {
            bits |= hit_crate;
            crate.shootable = false;
            crate.state = crate_burst;
            break;
        }
    }
    return bits;
}

// decomp: pickup_find_nearest 0x67878
int pickup_find_nearest(const StageWorld& stage, FxVec3 at, int slot, Fx range) {
    // retail marks its claim on crate 3 (the loop counter after the loop, a stray byte into
    // craft_fx_points), so no crate is ever claimed and `claimed` stays clear: kept
    int nearest = -1;
    std::int64_t best = 0;
    for (int i = 0; i < 3; ++i) {
        const Crate& crate = stage.crates[i];
        if ((crate.state != crate_falling && crate.state != crate_lying) || crate.claimed[slot]) {
            continue;
        }
        const std::int64_t d = distance_q32(crate.pos, at);
        if (nearest == -1 || d < best) {
            best = d;
            nearest = i;
        }
    }
    if (nearest != -1 && shorter_than(stage.crates[nearest].pos - at, range)) {
        return nearest;
    }
    return -1;
}
