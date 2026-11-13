#pragma once
// The stage viewer's side of weapons (src/weapons): the --dummy target, the shots as placeholder
// boxes and the --trace lines.
// Presentation and setup only; the effects and sounds of hits are a later job.
#include "render/scenery.hpp"
#include "world/world.hpp"

struct Options;

// --dummy BUILD: a still fighter in the slot (controller -2: no pad, no AI), ahead of player 1
void viewer_spawn_dummy(World& world, const Options& options, int slot);
// the entry gate between player 1 and the dummy, opened as the intro would (scene_intro_update)
void viewer_open_gate(World& world);
// --trace: the combat events of the last step and both fighters' durability every N steps
void viewer_combat_trace(const World& world, int every);
// PHANTOMCRASH_HURT="STEP:SLOT:DAMAGE[,...]" (headless captures of the hit shake, the static and
// the rumble): that much damage queued on the slot from another slot before the step, as a shot's
// hit would leave it for the settle
void viewer_debug_hurt(World& world);
