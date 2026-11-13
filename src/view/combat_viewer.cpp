#include "view/combat_viewer.hpp"

#include "ai/ai_links.hpp"
#include "cli.hpp"
#include "weapons/weapons.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace {

Vec3f floats(FxVec3 v) {
    return {fx_float(v.x), fx_float(v.y), fx_float(v.z)};
}

const char* event_name(CombatEventKind kind) {
    switch (kind) {
    case event_impact:
        return "impact";
    case event_muzzle:
        return "muzzle";
    case event_hit_fighter:
        return "hit";
    case event_destroyed:
        return "destroyed";
    case event_weapon_break:
        return "break";
    case event_popup:
        return "popup";
    case event_lock_done:
        return "lock done";
    case event_lock_warning:
        return "lock warning";
    case event_gauge_refill:
        return "refill";
    case event_weapon_open:
        return "weapon open";
    case event_hit_rumble:
        return "rumble";
    case event_shot_hit:
        return "shot hit";
    case event_beam_hit:
        return "beam hit";
    }
    return "?";
}

} // namespace

void viewer_spawn_dummy(World& world, const Options& options, int slot) {
    const Fighter& player = world.fighters[0];
    EntryPoint at;
    const FxVec3 ahead = turned_by({Fx{}, Fx{}, fx(1)}, player.heading);
    at.position =
        player.position + ahead * fx_bits(std::bit_cast<std::uint32_t>(options.dummy_distance));
    at.heading = options.dummy_away ? player.heading : player.heading + angle_half;
    // no team, no pad: the roster entry an AI would have, the fighter run by nobody
    RosterEntry& entry = battle_roster_entry(world, slot);
    entry.player = -1;
    entry.pilot.team = -1;
    Fighter& dummy = world.fighters[static_cast<std::size_t>(slot)];
    fighter_spawn(dummy, *world.battle, slot, -2, options.dummy, at);
    std::printf("dummy: slot %d build %d at %.2f %.2f %.2f durability %d\n", slot, options.dummy,
                fx_float(dummy.position.x), fx_float(dummy.position.y), fx_float(dummy.position.z),
                dummy.combat.durability);
}

void viewer_open_gate(World& world) {
    gate_open(world.stage, 0);
}

void viewer_combat_trace(const World& world, int every) {
    for (int i = 0; i < world.combat.event_count; ++i) {
        const CombatEvent& e = world.combat.events[static_cast<std::size_t>(i)];
        if (e.kind == event_muzzle) {
            // where the shot left: the world point, its offset in the shooter's upper-body frame
            // (right, up, forward of heading + aim_yaw) and the new shot's direction
            const Fighter& f = world.fighters[static_cast<std::size_t>(e.slot)];
            const float yaw = fx_float(angle_radians(f.heading + f.aim_yaw));
            const Vec3f d = floats(e.position - f.position);
            const float right = d.x * std::cos(yaw) - d.z * std::sin(yaw);
            const float ahead = d.x * std::sin(yaw) + d.z * std::cos(yaw);
            Vec3f dir{};
            for (const Shot& shot : world.combat.shots) {
                if (shot.state != 0 && shot.weapon == weapon_ref(e.slot, e.value) &&
                    shot.prev_pos == e.position) {
                    dir = floats(normalized(shot.basis.z));
                }
            }
            std::printf("step %lld muzzle slot %d mount %d at %.3f %.3f %.3f body %.3f %.3f %.3f "
                        "dir %.3f %.3f %.3f\n",
                        static_cast<long long>(world.step), e.slot, e.value, fx_float(e.position.x),
                        fx_float(e.position.y), fx_float(e.position.z), right, d.y, ahead, dir.x,
                        dir.y, dir.z);
            continue;
        }
        std::printf("step %lld %s slot %d value %d effect %d at %.3f %.3f %.3f\n",
                    static_cast<long long>(world.step), event_name(e.kind), e.slot, e.value,
                    e.effect, fx_float(e.position.x), fx_float(e.position.y),
                    fx_float(e.position.z));
    }
    // the crates taken this step (the stage's GetItem events)
    for (const StageEvent& e : stage_events(world.stage)) {
        if (e.kind == StageEventKind::GetItem) {
            std::printf("step %lld get-item slot %d kind %d at %.3f %.3f %.3f\n",
                        static_cast<long long>(world.step), e.a, e.b, fx_float(e.at.x),
                        fx_float(e.at.y), fx_float(e.at.z));
        }
    }
    if (every <= 0 || world.step % every != 0) {
        return;
    }
    int live = 0;
    for (const Shot& shot : world.combat.shots) {
        live += shot.state != 0;
    }
    std::printf("step %lld combat: shots %d rng %d", static_cast<long long>(world.step), live,
                world.random.index);
    for (const Fighter& f : world.fighters) {
        if (!f.present) {
            continue;
        }
        const FighterCombat& c = f.combat;
        std::printf(" | slot %d dur %d/%d %s score %d raise %d/%d", f.slot, c.durability,
                    c.durability_max, c.alive ? "alive" : "dead", c.score, c.triggers[0].raise,
                    c.triggers[1].raise);
        for (int m = 0; m < 4; ++m) {
            const Weapon& w = c.weapons[m];
            if (w.present) {
                std::printf(" w%d r%.1f rl%d", m, fx_float(w.resource), w.reload_left);
            }
        }
        std::printf(" track %.2f %.2f %.2f", fx_float(c.reticle_track_world.x),
                    fx_float(c.reticle_track_world.y), fx_float(c.reticle_track_world.z));
        const FighterHud& hud = world.huds[static_cast<std::size_t>(f.slot)];
        for (int i = 0; i < hud.visible_count; ++i) {
            const int slot = hud.visible[static_cast<std::size_t>(i)];
            const HudSense& s = hud.sense[static_cast<std::size_t>(slot)];
            std::printf(" sees %d at %.1f%s%s", slot, fx_float(s.distance),
                        s.on_screen ? "" : " off-screen", s.occluded ? " occluded" : "");
        }
        // a cloak in use (states 1..10)
        if (f.camo.state != camo_ready && f.camo.state != camo_none) {
            std::printf(" camo %d left %.0f recharge %.1f", f.camo.state, fx_float(f.camo.left),
                        fx_float(f.camo.recharge));
        }
        if (c.hud.melee_locked) {
            std::printf(" melee-locked");
        }
        for (int t = 0; t < combat_slots; ++t) {
            if (c.hud.locked_on[t]) {
                std::printf(" locked-on %d", t);
            }
        }
    }
    std::printf("\n");
}

void viewer_debug_hurt(World& world) {
    static const char* hurt = std::getenv("PHANTOMCRASH_HURT");
    if (!hurt || !world.battle) {
        return;
    }
    for (const char* at = hurt; at && *at;) {
        long long step = -1;
        int slot = -1, damage = 0;
        if (std::sscanf(at, "%lld:%d:%d", &step, &slot, &damage) == 3 && step == world.step + 1 &&
            slot >= 0 && slot < fighter_slots) {
            const int from = slot == 0 ? 1 : 0;
            Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
            f.combat.damage.pending[from] += damage;
            std::printf("hurt step %lld slot %d damage %d controller %d phase %d\n", step, slot,
                        damage, f.controller, world.battle_phase);
        }
        at = std::strchr(at, ',');
        at = at ? at + 1 : nullptr;
    }
}
