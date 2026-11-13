#pragma once
// What a fighter's HUD knows about the others (decomp FighterHud, include/actor.h): the senses
// hud_senses_update fills each step after every fighter has moved, and the reticle's
// target (hud_lock_arrows_update 0x4F620). The AI has no senses of its own and reads these, one
// step old (docs/port/ai.md "Perception"). Angles are radians in the original's sense, so the
// AI's arithmetic reads as the decomp's.
#include "fx/fx.hpp"
#include "fx/vec.hpp"
#include "world/fighter_slots.hpp"

#include <array>
#include <cstdint>

inline constexpr int hud_slots = fighter_slots;

// HudSense: one other fighter as this one sees it
struct HudSense {
    Fx distance; // camera head to the other's aim point
    Fx yaw;      // off this fighter's heading + aim_yaw, original sense: + turns the legs negative
    Fx pitch;    // up positive
    bool on_screen = false;
    Fx screen_x, screen_y, screen_z; // through viewport 0's projection
    bool occluded = false;           // the stage blocks the line (worked out once a pair)
};

struct FighterHud {
    // every running, live fighter not on this one's team, then one bubble pass by distance
    std::array<int, hud_slots> visible{};
    int visible_count = 0;
    std::array<HudSense, hud_slots> sense{};
    Fx reticle_center_x, reticle_center_y; // the reticle's screen point (hud_update)
    int reticle_target = -1;
    std::array<bool, hud_slots> reticle_on_me{};
    // the lock-on HUD's (hud_locks_update, hud_warning_update): filled by the weapons code, false
    // until it is in the world
    std::array<bool, hud_slots> locked_on{};  // a finished lock on that slot
    std::array<bool, hud_slots> locking_me{}; // that slot holds a lock mark on this one
    bool melee_locked = false;
};

struct World;
// hud_update's reticle, hud_senses_update and hud_lock_arrows_update for every running fighter,
// in slot order, at the end of a step
void huds_update(World& world);
