// Sprite effects and scorch marks (decomp src/od/u_0657e0.c, docs/port/effects.md "Sprite effects
// and scorch marks"): sprite_fx_spawn, sprite_fx_update with the kinds' updates,
// scorch_mark_add. Frames are updates.
#include "effects/effects.hpp"

#include <cmath>

namespace {

constexpr float pi = 3.1415927f;

// decomp: frand_range 0x6661A
float frand_range(std::uint32_t& seed, float low, float high) {
    return static_cast<float>(crt_rand(seed)) / 32767.0f * (high - low) + low;
}

// z down the top's second row, x = up x z, y = z x x, at its place
// decomp: matrix_surface_basis 0x664CA
Mat4 surface_basis(const Mat4& top) {
    const Vec3f b{-top.m[1][0], -top.m[1][1], -top.m[1][2]};
    const Vec3f x = vec_normalize(vec_cross({0, 1, 0}, b));
    const Vec3f y = vec_normalize(vec_cross(b, vec_cross({0, 1, 0}, b)));
    const Vec3f z = vec_normalize(b);
    Mat4 m;
    m.m[0][0] = x.x, m.m[0][1] = x.y, m.m[0][2] = x.z;
    m.m[1][0] = y.x, m.m[1][1] = y.y, m.m[1][2] = y.z;
    m.m[2][0] = z.x, m.m[2][1] = z.y, m.m[2][2] = z.z;
    m.m[3][0] = top.m[3][0], m.m[3][1] = top.m[3][1], m.m[3][2] = top.m[3][2];
    return m;
}

std::uint32_t alpha_word(int alpha) {
    return static_cast<std::uint32_t>(alpha) << 24 | 0xFFFFFF;
}

} // namespace

// decomp: sprite_fx_spawn 0x65965
void sprite_fx_spawn(Effects& fx, int kind, float scale, const Vec3f& at) {
    if (kind < 0 || kind >= static_cast<int>(fx.data.sprite_kinds.size())) {
        return;
    }
    for (SpriteFx& s : fx.sprites) {
        if (s.active) {
            continue;
        }
        s = SpriteFx{};
        s.active = true;
        s.kind = kind;
        s.frame = 0;
        s.scale = scale;
        s.factor = 0xFFFFFFFF;
        s.matrix = mat_translation(at.x, at.y, at.z);
        s.before = s.matrix;
        s.seed = static_cast<std::uint32_t>(crt_rand(fx.seed)) << 15 ^
                 static_cast<std::uint32_t>(crt_rand(fx.seed));
        return;
    }
}

// decomp: sprite_fx_update 0x65A5E
void sprite_fx_update(Effects& fx) {
    for (SpriteFx& s : fx.sprites) {
        s.visible = false;
        if (!s.active) {
            continue;
        }
        const SpriteKind& kind = fx.data.sprite_kinds[static_cast<std::size_t>(s.kind)];
        s.before = s.matrix;
        switch (s.kind) {
        case 0: // sprite_fx_fade_out: opaque, then down 17 an update over the last 15
            s.factor = alpha_word(
                s.frame > kind.frames - 15 ? 255 - (s.frame - (kind.frames - 15)) * 17 : 255);
            break;
        case 1: { // sprite_fx_smoke_rise: drifts up, the scale compounding into the rise
            const float dx = frand_range(s.seed, -0.06f, 0.06f);
            const float dz = frand_range(s.seed, -0.06f, 0.06f);
            const float grow =
                static_cast<float>(s.frame) / (static_cast<float>(kind.frames) * 2.0f) + 1.0f;
            s.matrix = mat_multiply(mat_scaling(grow, grow, grow),
                                    mat_multiply(mat_translation(dx, 0.12f, dz), s.matrix));
            s.factor =
                alpha_word(0x70 - static_cast<int>(static_cast<float>(s.frame) /
                                                   static_cast<float>(kind.frames) * 112.0f));
            break;
        }
        case 2: // sprite_fx_pulse: the shared phase steps once per live pulse (retail)
            s.scale = std::sin(fx.pulse_phase) * 0.5f + 1.0f;
            s.factor -= 0x80000000u;
            fx.pulse_phase += 0.2f;
            if (fx.pulse_phase > pi) {
                fx.pulse_phase -= pi;
            }
            break;
        default:
            break;
        }
        // drawn this update at this frame, then on to the next
        s.visible = kind.model != -1;
        s.draw_frame = s.frame;
        s.frame += kind.step;
        if (s.frame >= kind.frames) {
            s.active = false;
        }
    }
}

// decomp: scorch_mark_add 0x66352
void scorch_mark_add(Effects& fx, int kind, float scale, const Mat4& top) {
    ScorchMark& mark = fx.marks[fx.mark_next];
    mark.matrix =
        mat_multiply(mat_multiply(mat_rotation_z(fx.mark_angle), mat_scaling(scale, scale, scale)),
                     surface_basis(top));
    mark.kind = kind;
    fx.mark_next = (fx.mark_next + 1) % 100;
    // turned a radian each so neighbours don't line up
    fx.mark_angle += 1.0f;
    if (fx.mark_angle >= 6.2831855f) {
        fx.mark_angle -= 6.2831855f;
    } else if (fx.mark_angle <= -6.2831855f) {
        fx.mark_angle += 6.2831855f;
    }
    if (fx.mark_count < 100) {
        fx.mark_count += 1;
    }
}

void sprite_fx_queue(Effects& fx, float t, const Mat4& view, const Vec3f& eye,
                     std::vector<PassItem>& items) {
    for (const SpriteFx& s : fx.sprites) {
        if (!s.visible) {
            continue;
        }
        const SpriteKind& kind = fx.data.sprite_kinds[static_cast<std::size_t>(s.kind)];
        const Vec3f before{s.before.m[3][0], s.before.m[3][1], s.before.m[3][2]};
        const Vec3f after{s.matrix.m[3][0], s.matrix.m[3][1], s.matrix.m[3][2]};
        const Vec3f at = vec_add(before, vec_scale(vec_sub(after, before), t));
        const int base = kind.model + s.draw_frame;
        if (!kind.no_billboard) {
            const Mat4 m = mat_multiply(mat_scaling(s.scale, s.scale, s.scale),
                                        mat_translation(at.x, at.y, at.z));
            effects_queue_quad2(fx, base, m, kind.flags, s.factor, 9, view, eye, items);
        } else {
            // the smoke: billboarded at its point; the compounded scale is thrown away
            Mat4 m = billboard_matrix(at, eye, 0, 0, 0);
            if (kind.scale_from_kind) {
                m = mat_multiply(mat_scaling(s.scale, s.scale, s.scale), m);
            }
            effects_queue_quad(fx, base, m, kind.flags, s.factor, 9, view, items);
        }
    }
    // the scorch marks: pass 8, fogged, unsorted
    for (int i = 0; i < fx.mark_count; ++i) {
        const ScorchMark& mark = fx.marks[i];
        effects_queue_quad(fx, mark.kind == 0 ? 0xDB : 0x104, mark.matrix, 0x10400, 0xFFFFFFFF, 8,
                           view, items);
    }
}
