// The lamps at night (decomp src/hand/mech_part_assets.c actor_decals_step,
// src/hand/attachment_lights.c actor_decals_update and actor_decals_release;
// docs/port/effects.md "A fighter's effects"): each of a fighter's lamps (attachments 0..3)
// leaves a short red streak, a 4-segment trail pushed every update, and wears a glow model that
// pulses on a 2 second triangle wave. On night stages, and by day on Shibuya (rumble 2) in its
// zones 2..4 (actor_effects_update's shibuya_zone_at test at the fighter's position).
#include "effects/effects.hpp"
#include "render/mesh_flags.hpp"
#include "stage/stage_animate.hpp"
#include "view/cockpit_view.hpp"
#include "world/world.hpp"

namespace {

constexpr int lamp_segments = 4;
constexpr std::uint32_t lamp_red = 0xFFFF0000;
constexpr int state_dead = 10; // motion state 0xA

Mat4 floats_of(const FxMat4& m) {
    Mat4 out;
    const FxVec3 rows[4] = {m.x, m.y, m.z, m.w};
    for (int r = 0; r < 4; ++r) {
        out.m[r][0] = fx_float(rows[r].x);
        out.m[r][1] = fx_float(rows[r].y);
        out.m[r][2] = fx_float(rows[r].z);
    }
    return out;
}

// actor_decals_release: the trails let go, to fade out
void lamps_release(Effects& fx, FighterEffects& e) {
    for (LampTrail& lamp : e.lamps) {
        if (lamp.trail >= 0) {
            trail_release(fx, lamp.trail);
        }
        lamp.trail = -1;
        lamp.glow = false;
    }
}

} // namespace

void lamp_trails_update(Effects& fx, const World& world) {
    const bool night = fx.day_mode % 2 == 1; // stage_is_night: the odd selectors
    const bool shibuya_day = fx.day_mode == 2;
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        FighterEffects& e = fx.fighters[slot];
        for (LampTrail& lamp : e.lamps) {
            lamp.glow = false;
        }
        if (!world.battle || (!night && !shibuya_day)) {
            continue;
        }
        if (!f.present) {
            lamps_release(fx, e);
            continue;
        }
        // by day on Shibuya only in the zones 2..4, let go elsewhere
        if (!night) {
            const float at[3] = {fx_float(f.position.x), fx_float(f.position.y),
                                 fx_float(f.position.z)};
            const int zone = stage_zone_at(fx.day_mode, at);
            if (zone != 2 && zone != 3 && zone != 4) {
                lamps_release(fx, e);
                continue;
            }
        }
        // actor_decals_step: let go for a dead fighter, for a human in a replay's view 0
        // (replay_view), and for a human in the cockpit view, or switching to it, outside a scene
        // (src/view/cockpit_view.hpp)
        const bool human = f.controller != -1;
        if (f.motion_state == state_dead || (human && fx.replay_view == 0) ||
            own_lamps_hidden(world, slot)) {
            lamps_release(fx, e);
            continue;
        }
        const int body = f.body_class < 0 || f.body_class > 6 ? 0 : f.body_class;
        for (int n = 0; n < 4; ++n) {
            const FighterAttachment& at = f.pose.attachments[n];
            LampTrail& lamp = e.lamps[n];
            if (at.kind < 0 || at.part < 0 || at.matrix < 0) {
                continue;
            }
            const Vec3f pos = original_of(at.position);
            if (lamp.trail < 0) {
                lamp.trail = trail_start(fx, pos, lamp_segments, -2 - (slot * 4 + n));
                continue;
            }
            // a red streak, 0.5 wide at the lamp to 0, alpha 150 to 0
            trail_push(fx, lamp.trail, pos, -1, 2, lamp_red, 0.5f, 0.0f, 150.0f, 0.0f);
            // the glow on the lamp's node: alpha 255 x a triangle wave of age_frames % 60 / 60
            float phase = static_cast<float>(f.age_frames % 60);
            if (phase > 30.0f) {
                phase = 60.0f - phase;
            }
            const int alpha = static_cast<int>(255.0f * (phase / 60.0f));
            lamp.colour = static_cast<std::uint32_t>(alpha) << 24 | 0xFFFFFF;
            lamp.model = fx.data.lamp_glow_models[body][fx.data.lamp_glow_columns[n] & 1];
            lamp.before = lamp.glow_seen ? lamp.matrix : port_matrix(floats_of(
                                                             f.pose.palette[at.part][at.matrix]));
            lamp.matrix = port_matrix(floats_of(f.pose.palette[at.part][at.matrix]));
            lamp.glow = lamp.glow_seen = true;
        }
    }
}

void lamp_glows_queue(const Effects& fx, float t, const Mat4& view, std::vector<PassItem>& items) {
    for (const FighterEffects& e : fx.fighters) {
        for (const LampTrail& lamp : e.lamps) {
            if (!lamp.glow) {
                continue;
            }
            Mat4 m = lamp.matrix;
            for (int c = 0; c < 3; ++c) {
                m.m[3][c] = lamp.before.m[3][c] + (lamp.matrix.m[3][c] - lamp.before.m[3][c]) * t;
            }
            // model_render_submit_color_bit25: the colour as the factor, fog off, pass 8
            effects_queue_slot(fx, lamp.model, m, 8, mesh_factor | mesh_no_fog, lamp.colour, view,
                               items);
        }
    }
}
