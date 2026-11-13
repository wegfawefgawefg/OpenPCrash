// The AI's triggers and camouflage (docs/port/ai.md "Weapons", "Camouflage"): ai_fire_pacing_init,
// ai_weapons_bind with ai_weapon_slot_add, ai_fire_arm,
// ai_want_fire, ai_weapons_update, ai_camo_setup, ai_camo_update,
// ai_stat_bands_apply. Fire buttons go into the record by mount_fire_bits, as
// actor_trigger_press ORs them.
#include "ai/ai_internal.hpp"
#include "battle/battle_flow.hpp"
#include "ai/ai_links.hpp"

namespace {

const AiWeaponRecord& record_of(const Ai& ai, int mount, int row) {
    const std::vector<AiWeaponRecord>& rows = mount < 2 ? ai.data.arms : ai.data.backs;
    static const AiWeaponRecord none{};
    if (rows.empty()) {
        return none;
    }
    return rows[static_cast<std::size_t>(row < 0 || row >= static_cast<int>(rows.size()) ? 0
                                                                                         : row)];
}

// free_weapon_classes: bit 0 behaviour 5, bit 1 behaviour 1, bit 2 behaviour 0, bit 3 behaviour 2
bool free_class(const AiController& c, int behavior) {
    return (behavior == 5 && (c.free_weapon_classes & 1)) ||
           (behavior == 1 && (c.free_weapon_classes & 2)) ||
           (behavior == 0 && (c.free_weapon_classes & 4)) ||
           (behavior == 2 && (c.free_weapon_classes & 8));
}

// half in state 5, x1.5 at aspect 3 (its x1.25 arm tests aspect
// 3 again and can't run: kept)
// decomp: ai_fire_chance_adjusted 0x5D03E
int fire_chance_adjusted(const AiController& c, int chance) {
    std::int64_t scaled = chance * 4; // quarters, exact for x0.5 and x1.5
    if (c.state == 5) {
        scaled /= 2;
    }
    if (c.target_aspect == 3) {
        scaled = scaled * 3 / 2;
    }
    // (int) of the float: toward zero
    return static_cast<int>(scaled / 4);
}

// a press: the fire flag held until the next decision, a new delay
void fire(Ai& ai, AiWeapon& w) {
    ai.c.fire[static_cast<std::size_t>(w.mount)] = true;
    w.want_fire = false;
    w.delay = static_cast<std::int16_t>(ai_jittered(ai, ai.c.pacing.refire_delay, 2));
}

void stop(Ai& ai, const AiWeapon& w) {
    if (w.bound) {
        ai.c.fire[static_cast<std::size_t>(w.mount)] = false;
    }
}

} // namespace

// decomp: ai_fire_pacing_init 0x5B16D
void ai_fire_pacing_init(Ai& ai) {
    AiFirePacing& p = ai.c.pacing;
    const int level = ai_level(ai);
    p.firing = 0;
    p.max_firing = (level + 128) * (level + 128) / 10000 + 1;
    // (L + 45) x 256 / 125: 92 at level 0, 294 at 99
    p.fire_chance = static_cast<std::int16_t>((level + 45) * 256 / 125);
    // (L (cadence/30 - cadence/2) + cadence/2 x 99 - cadence/30 x 45) / 54: 26 at level 0
    p.refire_delay =
        static_cast<std::int16_t>((level * (1 - ai_cadence / 2) + (ai_cadence / 2 * 99 - 45)) / 54);
    p.rearm_delay = static_cast<std::int16_t>((level + 99) * ai_cadence / 198);
    p.burst_delay = static_cast<std::int16_t>((ai_cadence * 70 - ai_cadence * level) / 70);
}

// decomp: ai_weapons_bind 0x58230
void ai_weapons_bind(Ai& ai) {
    AiController& c = ai.c;
    c.weapon = {};
    // the slot's build: the roster entry's (battle/battle_flow.hpp), or a build row
    const BuildRecord& build =
        ai.f.build < 0 ? battle_slot_build(ai.world, ai.slot)
                       : ai.world.battle->parts.builds[static_cast<std::size_t>(
                             ai.f.build >= static_cast<int>(ai.world.battle->parts.builds.size())
                                 ? 0
                                 : ai.f.build)];
    int next = 0;
    for (int mount = 0; mount < 4; ++mount) {
        const int row = build.weapon[mount];
        if (row == -1) {
            continue;
        }
        // ai_weapon_slot_add: the first free AiWeapon, its delay and burst delay drawn
        AiWeapon& w = c.weapon[static_cast<std::size_t>(next++)];
        const AiWeaponRecord& record = record_of(ai, mount, row);
        w.bound = true;
        w.mount = mount;
        w.want_fire = false;
        w.delay = static_cast<std::int16_t>(ai_jittered(ai, c.pacing.refire_delay, 2));
        w.burst_delay = static_cast<std::int16_t>(ai_jittered(ai, c.pacing.burst_delay, 2));
        w.armed = 0;
        w.behavior = record.behavior;
        w.lock_rule = record.lock_rule;
    }
}

// decomp: ai_fire_arm 0x5AFE1
void ai_fire_arm(Ai& ai) {
    // armed has no reader: the draws are the whole effect
    const int level = ai_level(ai);
    const int chance = level + 0x80 < 0 ? 0 : level + 0x80 > 0xFF ? 0xFF : level + 0x80;
    for (AiWeapon& w : ai.c.weapon) {
        if (w.bound) {
            w.armed = 0;
            if (ai_roll(ai, chance, 0xFF)) {
                w.armed |= 1;
            }
        }
    }
}

// decomp: ai_fire_disarm 0x5E16C
void ai_fire_disarm(Ai& ai) {
    for (AiWeapon& w : ai.c.weapon) {
        if (w.bound) {
            w.armed = 0;
        }
    }
}

// decomp: ai_fire_stop_all 0x5BD4E
void ai_fire_stop_all(Ai& ai) {
    for (const AiWeapon& w : ai.c.weapon) {
        stop(ai, w);
    }
}

// decomp: ai_weapon_class6_ready 0x5EB95
bool ai_weapon_class6_ready(const Ai& ai) {
    for (const AiWeapon& w : ai.c.weapon) {
        if (w.bound && w.behavior == 6) {
            return true;
        }
    }
    return false;
}

void ai_weapons_stop_if_empty(Ai& ai) {
    AiController& c = ai.c;
    const Fx resource[4] = {
        weapon_resource(ai.world, ai.slot, 0), weapon_resource(ai.world, ai.slot, 1),
        weapon_resource(ai.world, ai.slot, 2), weapon_resource(ai.world, ai.slot, 3)};
    // retail passes the back mounts' resources swapped, so back A stops when back B is empty and
    // the other way round (kept)
    const Fx tested[4] = {resource[0], resource[1], resource[3], resource[2]};
    for (const AiWeapon& w : c.weapon) {
        if (w.bound && tested[w.mount] <= Fx{}) {
            c.fire[static_cast<std::size_t>(w.mount)] = false;
        }
    }
    c.out_of_ammo = resource[0] + resource[1] + resource[2] + resource[3] <= Fx{};
}

// decomp: ai_want_fire 0x5B0B9
void ai_want_fire(Ai& ai) {
    AiController& c = ai.c;
    const int level = ai_level(ai);
    if (c.pacing.firing >= (level + 0x80) * (level + 0x80) / 10000 + 1) {
        return;
    }
    for (AiWeapon& w : c.weapon) {
        if (!w.bound) {
            continue;
        }
        Fx reach_min, reach_max;
        weapon_reach(ai.world, ai.slot, w.mount, &reach_min, &reach_max);
        if (c.target_distance < reach_max && c.target_distance >= reach_min) {
            w.want_fire = true;
        }
    }
}

// decomp: ai_weapons_update 0x5B2E0
void ai_weapons_update(Ai& ai) {
    AiController& c = ai.c;
    const HudSense* sense = ai_target_sense(ai);
    const int level = ai_level(ai);
    std::array<bool, 4> handled{};
    if (c.class5_fire_frames > 0) {
        --c.class5_fire_frames;
    }
    // a target in view: on screen and unoccluded (at level 0 or under the screen test is waived)
    if (sense && (c.target >= 0 || level <= 0) && (level <= 0 || sense->on_screen) &&
        !sense->occluded) {
        for (std::size_t i = 0; i < 4; ++i) {
            AiWeapon& w = c.weapon[i];
            if (!w.bound) {
                continue;
            }
            const bool free = free_class(c, w.behavior);
            handled[i] = true;
            if (w.delay > 0) {
                --w.delay;
            }
            if (!free && w.delay > 0) {
                continue;
            }
            if (!w.want_fire) {
                stop(ai, w);
                w.delay = static_cast<std::int16_t>(ai_jittered(ai, c.pacing.refire_delay, 2));
                continue;
            }
            // the first press waits out the burst delay, counted every time this is reached
            if (!(w.burst_delay-- < 0)) {
                continue;
            }
            int chance = fire_chance_adjusted(c, c.pacing.fire_chance);
            if (free) {
                chance = 0x100;
            }
            if (w.behavior == 5) {
                c.class5_fire_frames = c.class5_fire_hold;
            }
            if (!ai_roll(ai, chance, 0x100)) {
                stop(ai, w);
                w.delay = static_cast<std::int16_t>(ai_jittered(ai, c.pacing.refire_delay, 2));
                continue;
            }
            switch (w.lock_rule) {
            case 1:
                // a blade swings only with the melee lock
                if (w.behavior != 6 || ai.hud.melee_locked) {
                    fire(ai, w);
                }
                break;
            case 2:
            case 3:
                // a lock-on weapon waits for a finished lock
                if (c.target < 0 || ai.hud.locked_on[static_cast<std::size_t>(c.target)]) {
                    fire(ai, w);
                }
                break;
            default:
                fire(ai, w);
                break;
            }
        }
    }
    // a laser keeps firing while its hold lasts; anything not decided this update lets go
    for (std::size_t i = 0; i < 4; ++i) {
        AiWeapon& w = c.weapon[i];
        if (w.bound && w.behavior == 5 && c.class5_fire_frames > 0) {
            handled[i] = true;
            fire(ai, w);
        }
        if (!handled[i]) {
            stop(ai, w);
        }
    }
    c.pacing.firing = 0;
    for (const AiWeapon& w : c.weapon) {
        if (w.bound && c.fire[static_cast<std::size_t>(w.mount)]) {
            ++c.pacing.firing;
        }
    }
    for (int mount = 0; mount < 4; ++mount) {
        if (c.fire[static_cast<std::size_t>(mount)]) {
            press(ai, mount_fire_bits[mount]);
        }
    }
}

// decomp: ai_camo_setup 0x5C885
void ai_camo_setup(Ai& ai) {
    AiController& c = ai.c;
    const int level = ai_level(ai);
    c.camo_hold = 0;
    c.camo_cooldown_base = static_cast<std::int16_t>((level + 0x7F) * ai_cadence / 20);
    c.camo_cooldown = c.camo_cooldown_base;
    c.camo_chance = static_cast<std::int16_t>(((level << 8) + 0x1400) / 80);
    c.camo_fraction = fx_clamp(fx_ratio(level + 0x80, 256), Fx{}, fx(1));
}

// decomp: ai_camo_update 0x5C9AC
void ai_camo_update(Ai& ai) {
    AiController& c = ai.c;
    if (c.camo_held) {
        c.use_camo = c.target >= 0;
    }
    // ai_camo_reaction_chance: twice a second, role 1 and up, by the tournament's rule;
    // a lock mark on this fighter then asks for camouflage
    int reaction = 0;
    const int role = fighter_role(ai.world, ai.slot);
    const int tier = tournament_rule_tier_now(ai.world);
    if (c.age_frames % (ai_cadence / 2) == 0 && role >= 1 && tier >= 0) {
        constexpr int by_tier[9] = {100, 100, 90, 70, 30, 20, 40, 100, 50};
        reaction = by_tier[tier < 9 ? tier : 8];
        if (role == 1) {
            reaction /= 2;
        }
    }
    if (ai_roll(ai, reaction, 0x64)) {
        for (int i = 0; i < fighter_slots; ++i) {
            if (i != ai.slot && ai.hud.locking_me[static_cast<std::size_t>(i)]) {
                c.use_camo = true;
                break;
            }
        }
    }
    const bool use_camo = c.use_camo;
    release(ai, control_camouflage);
    if (camo_state(ai.world, ai.slot) == 0) {
        if (c.camo_cooldown-- < 0 || use_camo) {
            if ((ai_roll(ai, c.camo_chance, 0x100) && !c.camo_when_shot) || use_camo) {
                press(ai, control_camouflage);
                // camo_max x clamp((r / 32767 x 0.1 + 1) x fraction, 0, 1); counted in updates
                // though it is ticks (kept)
                const std::int64_t r = ai_draw(ai);
                const std::int64_t scale = 32767 * 10;
                const std::int64_t share = (scale + r) * (c.camo_fraction.raw);
                const std::int64_t whole = scale * fx_one_raw;
                const std::int64_t most = camo_max(ai.world, ai.slot);
                c.camo_hold =
                    static_cast<std::int16_t>(share >= whole ? most : most * share / whole);
            }
            c.camo_cooldown = static_cast<std::int16_t>(ai_jittered(ai, c.camo_cooldown_base, 2));
        }
        return;
    }
    if (c.camo_held) {
        if (!use_camo) {
            press(ai, control_camouflage);
        }
        return;
    }
    if (c.camo_hold-- < 0) {
        press(ai, control_camouflage);
    }
}

// decomp: ai_stat_bands_apply 0x5CC69
void ai_stat_bands_apply(Ai& ai) {
    AiController& c = ai.c;
    const int level = ai_level(ai);
    if (level > 0) {
        const int row = level / 20 > 19 ? 19 : level / 20;
        const auto& band = ai.data.tables.stat_bands[static_cast<std::size_t>(row)];
        c.band_button1_chance = static_cast<std::int16_t>(band[0]);
        c.band_jump_chance = static_cast<std::int16_t>(band[1]);
        c.band_dodge_chance = static_cast<std::int16_t>(band[2]);
        return;
    }
    c.band_button1_chance = c.band_jump_chance = c.band_dodge_chance = 0;
}
