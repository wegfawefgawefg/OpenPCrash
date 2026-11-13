#pragma once
// Port-only dev actions for headless campaign runs (docs/playthrough.md), asked by the front end's
// dev route through Inputs::dev so a fight's replay repeats them. Never set in normal play.
struct World;

enum : unsigned {
    dev_kill = 1,     // seat 0's fighter kills one opponent (the fixed opponent first)
    dev_kill_all = 2, // every opponent that can be killed
    dev_die = 4,      // seat 0's fighter dies to its nearest opponent
    dev_door = 8,     // seat 0's fighter stands in its nearest gate's entry point (the door scene)
    dev_shield = 16,  // seat 0's fighter takes no hits this step (immune, as after the entrance)
};

// at the start of the battle's step, before the fighters run
void battle_dev_apply(World& world, unsigned dev);
