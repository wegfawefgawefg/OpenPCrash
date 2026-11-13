// Who the AI fights (docs/port/ai.md "Perception", "Targeting"): ai_pick_target with the
// camouflage watch, the view cone test, retaliation and the aspect.
#include "ai/ai_internal.hpp"
#include "ai/ai_links.hpp"

namespace {

// once a second a level + 1 of 99 roll to see through camouflage
// decomp: ai_camo_watch_tick 0x5C7B7
void ai_camo_watch_tick(Ai& ai) {
    AiController& c = ai.c;
    --c.camo_watch_timer;
    if (c.camo_watch_timer < 0) {
        if (ai_roll(ai, ai_level(ai) + 1, 0x63)) {
            c.flags |= 2;
        } else {
            c.flags &= ~2;
        }
        c.camo_watch_timer = ai_cadence;
    }
}

// inside the cone, not occluded, and not hidden unless seen through
// decomp: ai_can_see 0x5D09E
bool ai_can_see(Ai& ai, int other, const HudSense& sense) {
    const AiController& c = ai.c;
    if (!(fx_abs(sense.yaw) < c.view_yaw && fx_abs(sense.pitch) < c.view_pitch &&
          !sense.occluded)) {
        return false;
    }
    if (!camo_hides(ai.world, other) || (c.flags & 2)) {
        return true;
    }
    if (c.camo_watch_timer != 0) {
        return false;
    }
    // close: level + 16 of 99; raising an arm: level + 31 of 99
    if (sense.distance < fx(5) && ai_roll(ai, ai_level(ai) + 1 + 15, 0x63)) {
        return true;
    }
    bool raising = false;
    for (int t = 0; t < 4; ++t) {
        raising = raising || trigger_pressed(ai.world, other, t);
    }
    return raising && ai_roll(ai, ai_level(ai) + 1 + 30, 0x63);
}

// the memory refresh, only when a target is held: its slot is the one held on entry
void refresh_memory(Ai& ai) {
    AiController& c = ai.c;
    if (c.target >= 0) {
        const int base = ai_level(ai) + 0x7F;
        c.target_memory = static_cast<std::int16_t>(base * base * base / 100000);
        c.target_slot = c.target;
    }
}

// the first slot with hits pending; a fixed opponent on a 10/255 roll
// decomp: ai_find_attacker 0x5D437
int ai_find_attacker(Ai& ai) {
    for (int i = 0; i < fighter_slots; ++i) {
        if (fighter_hit_pending(ai.world, ai.slot, i)) {
            if (fighter_role(ai.world, i) < 2 || ai_roll(ai, 0xA, 0xFF)) {
                return i;
            }
        }
    }
    return -1;
}

// decomp: ai_retaliate 0x5D23D
void ai_retaliate(Ai& ai) {
    AiController& c = ai.c;
    if (c.handler == AiHandler::wander_update || c.target >= 0) {
        return;
    }
    const int attacker = ai_find_attacker(ai);
    if (attacker == -1 || !ai.world.fighters[static_cast<std::size_t>(attacker)].present) {
        return;
    }
    c.target = attacker;
    bool hit_back = false;
    if (!camo_hides(ai.world, attacker)) {
        hit_back = ai_roll(ai, ai_level(ai) + 0x7F, 0xFF);
    } else {
        hit_back = ai_roll(ai, ai_level(ai) < 0xA ? 0xA : ai_level(ai), 0xFF);
    }
    if (fighter_role(ai.world, ai.slot) >= 1) {
        hit_back = true;
    }
    if (!hit_back) {
        c.target = -1;
        return;
    }
    // chasing a repair crate: let it be
    if ((c.move_flags & move_crate_chase) && c.pickup >= 0) {
        const int kind = pickup_kind(ai.world, c.pickup);
        if (kind >= 4 && kind <= 6) {
            c.target = -1;
            return;
        }
    }
    c.target_memory =
        static_cast<std::int16_t>(static_cast<std::int16_t>(ai_cadence * 2) +
                                  static_cast<std::int16_t>(ai_level(ai) * ai_cadence) / 10);
    c.target_slot = attacker;
    c.move_flags &= ~move_any;
    c.ranker_timer = static_cast<std::int16_t>(ai_cadence << 3);
    ai_change_state(ai, 3);
}

// the heading difference in radians against two cosines, so 3 under
// 0.7071 (every negative difference included), else 0 (kept)
// decomp: ai_target_aspect 0x5CF4C
void ai_target_aspect(Ai& ai) {
    AiController& c = ai.c;
    if (c.target < 0) {
        return;
    }
    // the original's headings are the port's negated, each in -pi..pi, the difference unwrapped
    const Fighter& target = ai.world.fighters[static_cast<std::size_t>(c.target)];
    const Fx difference = angle_radians(target.heading) - angle_radians(ai.f.heading);
    if (difference < fx_f(0.70710677)) {
        c.target_aspect = 3;
    } else if (difference > fx_f(0.5)) {
        c.target_aspect = 0;
    }
}

} // namespace

const HudSense* ai_target_sense(const Ai& ai) {
    return ai.c.target_sense >= 0 ? &ai.hud.sense[static_cast<std::size_t>(ai.c.target_sense)]
                                  : nullptr;
}

// decomp: ai_pick_target 0x5B941
void ai_pick_target(Ai& ai) {
    AiController& c = ai.c;
    FighterHud& hud = ai.hud;
    ai_camo_watch_tick(ai);
    // the visible list within 100, in its order
    std::array<int, hud_slots> near{};
    int near_count = 0;
    for (int k = 0; k < hud.visible_count; ++k) {
        const int i = hud.visible[static_cast<std::size_t>(k)];
        if (hud.sense[static_cast<std::size_t>(i)].distance < fx(100)) {
            near[static_cast<std::size_t>(near_count++)] = i;
        }
    }
    int picked = -1;
    // pass 1: a human it sees ends the pass; an AI under the reticle is taken and the pass goes on
    for (int k = 0; k < near_count; ++k) {
        const int i = near[static_cast<std::size_t>(k)];
        if (ai.world.fighters[static_cast<std::size_t>(i)].controller != -1) {
            if (ai_can_see(ai, i, hud.sense[static_cast<std::size_t>(i)])) {
                refresh_memory(ai);
                picked = i;
                break;
            }
        } else if (hud.reticle_target == i) {
            refresh_memory(ai);
            picked = i;
        }
    }
    // pass 2: the first seen with no target held, the last with one
    if (picked < 0) {
        for (int k = 0; k < near_count; ++k) {
            const int i = near[static_cast<std::size_t>(k)];
            if (ai_can_see(ai, i, hud.sense[static_cast<std::size_t>(i)]) &&
                (picked < 0 || c.target >= 0)) {
                refresh_memory(ai);
                picked = i;
            }
        }
    }
    if (picked >= 0) {
        c.target = picked;
    } else if (c.target_memory > 0) {
        c.target =
            c.target_slot >= 0 && fighter_alive(ai.world, c.target_slot) ? c.target_slot : -1;
    } else {
        c.target = -1;
        c.target_slot = -1;
    }
    ai_retaliate(ai);
    c.target_sense = c.target;
    ai_target_aspect(ai);
    --c.target_memory;
}
