// The world's bark requests that need the battle around the fighter (decomp actor_bark's
// call sites in src/hand/mech_part_assets.c actor_settle_damage and actor_step,
// src/hand/u_088880.c actor_weapon_break_apply). A request is a bit in Fighter::barks;
// the speaker rule, the chance and the pick are presentation's (src/hud/hud_barks.cpp).
#include "battle/battle_flow.hpp"

#include "ai/ai_links.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

namespace {

// durability / durability_max below a quarter, in integers (the float quotient of two small
// integers is exactly a quarter only when 4 x durability is the max)
bool under_quarter(const FighterCombat& c) {
    return c.durability * 4 < c.durability_max;
}

} // namespace

void battle_damage_barks(World& world, Fighter& f, int total) {
    const FighterCombat& c = f.combat;
    const long long before = c.durability, after = c.durability - total, most = c.durability_max;
    // 0.25 < before / max && 0.25 >= after / max, else the same about a half
    if (before * 4 > most && after * 4 <= most) {
        bool by_boss = false;
        for (int i = 0; i < battle_slots; ++i) {
            if (c.damage.pending[i] > 0 &&
                battle_roster_entry(world, i).ticket.progress.role == 2) {
                by_boss = true;
                break;
            }
        }
        fighter_bark(f, by_boss ? bark_armor_25_by_boss : bark_armor_25);
    } else if (before * 2 > most && after * 2 <= most) {
        fighter_bark(f, bark_armor_50);
    }
}

void battle_kill_barks(World& world, Fighter& victim, Fighter& killer) {
    const RosterEntry& lost = battle_roster_entry(world, victim.slot);
    const RosterEntry& won = battle_roster_entry(world, killer.slot);
    fighter_bark(victim, won.ticket.progress.role == 2 ? bark_lost_to_boss : bark_lost);
    if (killer.controller == -1) {
        return;
    }
    // a fixed opponent's first defeat ever has its own line (its ticket's byte, set after this by
    // battle_kill)
    if (lost.ticket.progress.role == 2 && ticket_flag(world.story, lost.ticket.progress) == 0) {
        fighter_bark(killer, bark_killed_new_boss);
    } else {
        fighter_bark(killer, under_quarter(killer.combat) ? bark_kill_at_25 : bark_kill);
    }
}

void battle_ammo_barks(Fighter& f) {
    // actor_ammo_fraction: the mean resource / resource_max over the fitted weapons but
    // lasers and blades, 1 with none; crossing a half downward
    FighterCombat& c = f.combat;
    Fx sum{};
    int count = 0;
    for (const Weapon& w : c.weapons) {
        if (w.present && w.fitted && w.record && w.record->behavior != 5 &&
            w.record->behavior != 6 && w.resource_max > 0) {
            sum += fx_div_near(w.resource, fx(w.resource_max));
            ++count;
        }
    }
    const Fx before = c.ammo_fraction;
    c.ammo_fraction = count > 0 ? fx_div_near(sum, fx(count)) : fx(1);
    if (fx_f(0.5) < before && fx_f(0.5) >= c.ammo_fraction) {
        fighter_bark(f, under_quarter(c) ? bark_ammo_50_at_25 : bark_ammo_50);
    }
}

void battle_break_barks(Fighter& f, bool back) {
    const FighterCombat& c = f.combat;
    if (under_quarter(c)) {
        fighter_bark(f, bark_weapon_broken_at_25);
    } else if (!back) {
        // an arm: "both arms" once both are gone. Retail tests the two arms before the break is
        // marked, so the line comes only on a third break (docs/bugs.md); the port tests after
        fighter_bark(f, c.weapon_broken[0] && c.weapon_broken[1] ? bark_weapon_broken_last
                                                                  : bark_weapon_broken);
    }
}
