// The shield bubble (decomp src/hand/u_06eda0.c immunity_bubble_draw, actor_draw's
// glow_alpha, shader_constants_rainbow; docs/port/effects.md "Shield bubble and
// camouflage"): model 0x41B through the rainbow program around a fighter while its immunity runs
// (a Repulsion Shield's 30 ticks). Only the bubble: the camouflage hull is drawn elsewhere.
// Retail moves glow_alpha, the spin and the rainbow's two phases in its draw code, the spin once
// a bubble drawn and the phases once a draw call; here all of them once an update, and the
// bubble sits on the fighter's aim point between the last two steps.
#include "ai/ai_links.hpp"
#include "effects/effects.hpp"
#include "render/mesh_flags.hpp"
#include "world/world.hpp"

#include <cmath>
#include <cstdlib>

namespace {

constexpr int bubble_slot = 0x41B;
constexpr std::uint32_t bubble_colour = 0x64FFFFFF; // model_render_submit_color's
constexpr float two_pi = 6.2831855f;

// actor_draw's test: an AI that isn't a tournament's fixed opponent, or anyone outside a scene
bool bubble_shown(const World& world, const Fighter& f) {
    const BattleSlot& slot = world.battle_state.slots[static_cast<std::size_t>(f.slot)];
    if (slot.scene.state == 0) {
        return true;
    }
    return f.controller == -1 && battle_roster_entry(world, f.slot).ticket.progress.role != 2;
}

} // namespace

void shield_bubble_step(Effects& fx, const World& world) {
    for (int slot = 0; slot < fighter_slots; ++slot) {
        ShieldBubble& b = fx.bubbles[slot];
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        b.before = b.at;
        b.at = f.present ? original_of(f.combat.aim_point) : Vec3f{};
        b.heading = -fx_float(angle_radians(f.heading)); // the original's sense
        b.radius = fx_float(f.radius) * 3.2f;
    }
}

void shield_bubble_update(Effects& fx, const World& world) {
    if (!world.battle) {
        return;
    }
    bool drawn = false;
    for (int slot = 0; slot < fighter_slots; ++slot) {
        ShieldBubble& b = fx.bubbles[slot];
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        b.drawn = false;
        if (!f.present || !bubble_shown(world, f)) {
            continue;
        }
        // glow_alpha: up 0.015 while the immunity has more than 10 ticks left and the fighter
        // has durability, else down 0.03, within 0..0.3
        // PHANTOMCRASH_DEV_BUBBLE (captures): every fighter's bubble as if it were immune
        static const bool always = std::getenv("PHANTOMCRASH_DEV_BUBBLE") != nullptr;
        if ((f.combat.damage.immune_frames > ::fx(10) || always) && f.combat.durability > 0) {
            b.glow += 0.015f;
        } else {
            b.glow -= 0.03f;
        }
        b.glow = b.glow < 0.0f ? 0.0f : b.glow > 0.3f ? 0.3f : b.glow;
        if (b.glow <= 0.0f) {
            continue;
        }
        b.drawn = true;
        b.spin = fx.bubble_spin;
        b.scroll = fx.rainbow_scroll;
        b.phase = fx.rainbow_phase;
        drawn = true;
    }
    // the shared spin (+0.02) and the rainbow's scroll and band phase step once an update while a
    // bubble shows: retail steps the spin a bubble drawn and the phases a draw call (a view, a
    // mesh), so they ran faster with more bubbles and in split screen (docs/bugs.md)
    if (drawn) {
        fx.bubble_spin += 0.02f;
        if (fx.bubble_spin > two_pi) {
            fx.bubble_spin -= two_pi;
        }
        fx.rainbow_scroll -= 0.01f;
        if (fx.rainbow_scroll < 0.0f) {
            fx.rainbow_scroll += 1.0f;
        }
        fx.rainbow_phase -= 0.03f;
        if (fx.rainbow_phase < -1.0f) {
            fx.rainbow_phase = 1.0f;
        }
    }
}

void shield_bubble_queue(const Effects& fx, float alpha, const Mat4& view,
                         std::vector<PassItem>& items) {
    for (const ShieldBubble& b : fx.bubbles) {
        if (!b.drawn) {
            continue;
        }
        const Vec3f at = vec_add(b.before, vec_scale(vec_sub(b.at, b.before), alpha));
        Mat4 m = mat_multiply(mat_scaling(b.radius, b.radius, b.radius),
                              mat_rotation_y(b.spin + b.heading));
        m = mat_multiply(m, mat_translation(at.x, at.y, at.z));
        const std::size_t first = items.size();
        effects_queue_slot(fx, bubble_slot, m, 9, mesh_rainbow | mesh_factor, bubble_colour, view,
                           items);
        for (std::size_t i = first; i < items.size(); ++i) {
            // rainbow_strength_set: retail keeps one strength for every bubble, the last one set
            // (docs/bugs.md); each bubble its own here
            items[i].item.effect[0] = b.glow;
            items[i].item.effect[1] = b.scroll;
            items[i].item.effect[2] = b.phase;
        }
    }
}
