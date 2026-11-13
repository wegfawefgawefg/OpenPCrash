#pragma once
// A fighter's footsteps, read off a world step (motion_select_descriptor, decomp
// src/hand/beam_quad.c, docs/port/data.md "Flag groups"). Retail tests them while it poses the
// fighter: each leg graph node flagged 0x20000 is a foot, counted in sv_pose_walk's order
// (Actor.foot_count, the same count that places attachments 14 on), and a grounded foot whose
// clip track crosses one of its 0x3800 marker keys, or any foot every third update of a skid,
// raises motion event 0x400 << foot and adds one to Actor.footsteps. The legs' sounds hear the
// count (fighter_sounds.cpp) and actor_effects_update starts that foot's landing set
// (fighter_effects.cpp). Presentation: it reads two worlds and writes neither.
#include <cstdint>

struct Fighter;
struct World;

// the feet that stepped on the step from before to now, bit k foot k (attachment 14 + k); 0 none
std::uint32_t fighter_footsteps(const World& world, const Fighter& before, const Fighter& now);
