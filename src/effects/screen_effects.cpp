// The 2D over the battle (decomp src/hand/u_06eda0.c): the hit static's scratches and flickers
// (screen_scratch_add, screen_scratches_draw, screen_flickers_draw) and the
// lens flare (lens_flare_draw, lens_flare_occlusion_test), and the PC's FLASHES
// dimming (flash_colour). Each builds its quads once an update, as retail draws them from the
// update; they show until the next one.
#include "effects/effects.hpp"
#include "world/world.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr int scene_slot = 0x223;
constexpr int flicker_slot = 0x140;
constexpr int flare_slot = 0x125;

// sprite_draw_texture_rect / _uv as the overlay places them (screens/ui/ui.cpp place): a texel
// rectangle of a sheet at a point, scaled, centred with flag 8, turned about its middle
Quad2D sprite(int slot, float x, float y, float sx, float sy, const float rect[4], float sheet_w,
              float sheet_h, int layer, std::uint32_t color, float alpha, float turn,
              std::uint32_t flags) {
    Quad2D q;
    q.slot = slot;
    q.u0 = std::trunc(rect[0]);
    q.v0 = std::trunc(rect[1]);
    q.u1 = std::trunc(rect[2]);
    q.v1 = std::trunc(rect[3]);
    q.sheet_w = sheet_w;
    q.sheet_h = sheet_h;
    const float half_w = (rect[2] - rect[0]) * sx * 0.5f, half_h = (rect[3] - rect[1]) * sy * 0.5f;
    q.x = x;
    q.y = y;
    if (flags & 8) {
        q.x -= half_w;
        q.y -= half_h;
    }
    q.w = (q.u1 - q.u0) * sx;
    q.h = (q.v1 - q.v0) * sy;
    q.rot[0] = half_w;
    q.rot[1] = half_h;
    q.rot[2] = turn;
    q.color = color;
    q.alpha = alpha;
    q.layer = layer;
    q.flags = flags | 4;
    q.anchor = anchor_free; // in place over the window's 3D, unclipped (docs/screens.md)
    return q;
}

// where the sun falls, the fade, the pieces. Retail places the sun in its
// 576 x 448 safe area, which its 3D filled, and draws and depth-tests the flare there as screen
// pixels, 32 left of and 16 above the sun (docs/bugs.md keeps that and the missing divide by w).
// The port's 3D fills the window, so the sun is placed over the drawn 3D (Effects::drawn_3d), the
// same 32 and 16 off, and the flare drawn unclipped; its fade and turn still measure the sun's
// place in retail's units, so a sun out at a wide window's side fades as one at retail's edge
// decomp: lens_flare_draw 0x710CE
void lens_flare(Effects& fx, std::vector<Quad2D>& out) {
    LensFlare& f = fx.flare;
    // the sun's direction through view x projection, not divided by w (retail's)
    const Vec3f out_dir =
        transform_normal(vec_normalize({fx.sun[0], fx.sun[1], fx.sun[2]}), fx.view_projection);
    const float half_w = fx.drawn_3d[2], half_h = fx.drawn_3d[3];
    const float cx = fx.drawn_3d[0] - 32.0f, cy = fx.drawn_3d[1] - 16.0f; // retail's offset
    f.sun_x = cx + out_dir.x * half_w;
    f.sun_y = cy - out_dir.y * half_h;
    // the depth test at that point of the 3D stretched over the 640x480 (renderer_depth_probe)
    f.probe_x = 320.0f + (f.sun_x - fx.drawn_3d[0]) / half_w * 320.0f;
    f.probe_y = 240.0f + (f.sun_y - fx.drawn_3d[1]) / half_h * 240.0f;
    // retail's pixel in its safe area: the same vertical field of view, x by the projections'
    // widths (the port's drawn 3D's over retail's 576 x 448)
    const float retail_x = out_dir.x * (half_w / half_h) * (448.0f / 576.0f) * 288.0f + 288.0f;
    const float retail_y = -out_dir.y * 224.0f + 224.0f;
    if (out_dir.z >= 0 && out_dir.z <= 1) {
        f.z = out_dir.z;
        f.center_distance = std::sqrt((retail_x - 288) * (retail_x - 288) +
                                      (retail_y - 224) * (retail_y - 224)) /
                            std::sqrt(133120.0f);
        const float corner = std::sqrt((retail_x - 576) * (retail_x - 576) +
                                       (retail_y - 448) * (retail_y - 448)) /
                             std::sqrt(532480.0f) * 6.2831855f;
        if (!f.state_prev && f.state == 1) {
            f.state = 2;
            f.alpha = 0;
        } else if (f.state_prev == 1 && !f.state) {
            f.state = 3;
            f.alpha = f.alpha_full;
        } else {
            f.state_prev = f.state;
        }
        if (f.state && f.center_distance <= 2.5f) {
            const float d = f.center_distance;
            const float size_a = (5.0f - d) * 2.0f, size_b = 2.0f, size_c = (2.5f - d) * 2.0f;
            const int fade = static_cast<int>(d / 8.0f * 255.0f);
            f.alpha_full =
                static_cast<unsigned>(static_cast<float>(static_cast<unsigned>(0xFF - fade)) * f.z);
            const unsigned fifth =
                static_cast<unsigned>(static_cast<int>(static_cast<float>(f.alpha_full) / 5.0f));
            unsigned alpha = f.alpha_full;
            if (f.state == 2) {
                f.alpha += fifth;
                if (f.alpha > f.alpha_full) {
                    f.alpha = f.alpha_full;
                    f.state = 1;
                }
                alpha = f.alpha;
            } else if (f.state == 3) {
                f.alpha -= fifth;
                if (f.alpha < fifth) {
                    f.alpha = 0;
                    f.state = 0;
                }
                alpha = f.alpha;
            }
            const std::uint32_t colour = (alpha & 0xFF) << 24 | 0xFFFFFF;
            const float(&cells)[12][4] = fx.data.flare_cells;
            out.push_back(sprite(flare_slot, f.sun_x, f.sun_y, size_a, size_a, cells[0], 128, 128,
                                 0x101, colour, 1, corner, 0x19));
            out.push_back(sprite(flare_slot, f.sun_x - 32.0f * size_b, f.sun_y - 32.0f * size_b,
                                 size_b, size_b, cells[1], 128, 128, 0x101, colour, 1, -corner,
                                 0x11));
            out.push_back(sprite(flare_slot, f.sun_x - 32.0f * size_c, f.sun_y - 32.0f * size_c,
                                 size_c, size_c, cells[2], 128, 128, 0x101, colour, 1, 0, 0x11));
            // the ghosts along the line through the drawn 3D's middle
            const float span_x = (cx - (f.sun_x - cx)) - f.sun_x;
            const float span_y = (cy - (f.sun_y - cy)) - f.sun_y;
            for (const FlarePiece& piece : fx.data.flare_pieces) {
                if (piece.cell < 0 || piece.cell >= 12) {
                    continue;
                }
                const float* cell = cells[piece.cell];
                const float x = piece.along * span_x + f.sun_x;
                const float y = piece.along * span_y + f.sun_y;
                const float w = cell[2] - cell[0];
                out.push_back(sprite(flare_slot, x - w / 2.0f * piece.scale,
                                     y - w / 2.0f * piece.scale, piece.scale, piece.scale, cell,
                                     128, 128, 0x101, (alpha & 0xFF) << 24 | piece.color, 1, 0,
                                     0x11));
            }
        }
    }
    f.state_prev = f.state;

    // lens_flare_occlusion_test, in the render after: the depth at the sun's pixel (last
    // frame's, from the renderer) must read clear twice running
    if (f.center_distance <= 2.5f) {
        const bool sky = fx.sun_depth >= 1.0f;
        if (sky && f.last_sky) {
            if (f.state != 2) {
                f.state = 1;
            }
        } else if (f.state != 3) {
            f.state = 0;
        }
        f.last_sky = sky;
    } else {
        f.state = 0;
    }
}

} // namespace

// decomp: screen_scratch_add 0x72298
void screen_scratch_add(Effects& fx, float y, int life, float speed, int kind) {
    if (kind < 0 || kind > 1) {
        return;
    }
    for (Scratch& s : fx.scratches[kind]) {
        if (s.life > 0) {
            continue;
        }
        s.y = y + 10.0f;
        s.life = life;
        s.speed = speed;
        return;
    }
}

void screen_effects_seat(Effects& fx, const World& world) {
    fx.hit_seat = world.stage.hit_seat;
    // PHANTOMCRASH_FX=-1,period,distance,scale,seat: the test's static goes to that seat's view
    if (const char* test = std::getenv("PHANTOMCRASH_FX")) {
        int set = 0, period = 0, seat = -1;
        float distance = 0, scale = 0;
        if (std::sscanf(test, "%d,%d,%f,%f,%d", &set, &period, &distance, &scale, &seat) == 5 &&
            set < 0) {
            fx.hit_seat = seat;
        }
    }
}

void screen_effects_update(Effects& fx, const World& world) {
    (void)world;
    fx.screen.clear();
    // the scratches: a 1-2 pixel band of the scene from 5 lower, moving down by its speed
    for (Scratch& s : fx.scratches[0]) {
        if (!s.life) {
            continue;
        }
        const float v0 = 16.0f + s.y + 5.0f;
        const float rect[4] = {32, v0, 608, v0 + rand_float_crt(fx.seed, 1.0f) + 1.0f};
        fx.screen.push_back(sprite(scene_slot, 32, 16.0f + s.y, 1, 1, rect, 640, 480, 0x102,
                                   0xFFFFFFFF, 1, 0, 0x1002));
        s.life -= 1;
        s.y += s.speed;
        if (s.y > 448.0f) {
            s.life = 0;
        }
    }
    // the flickers: a row of the noise texture stretched across, half see-through
    for (int i = 0; i < 10; ++i) {
        Scratch& s = fx.scratches[1][i];
        if (!s.life) {
            continue;
        }
        const int frame = (static_cast<int>(rand_float_crt(fx.seed, 8.0f)) + i) % 8;
        const float rect[4] = {0, static_cast<float>(frame), 128, static_cast<float>(frame + 1)};
        fx.screen.push_back(sprite(flicker_slot, 32, 16.0f + s.y, 5, 1, rect, 128, 8, 0x103,
                                   0x80FFFFFF, 0.8f, 0, 0));
        s.life -= 1;
    }
    fx.screen_static = static_cast<int>(fx.screen.size());
    // the flare: single view, day modes 0 and 2
    if (fx.day_mode % 2 == 0 && fx.day_mode != 4) {
        lens_flare(fx, fx.screen);
    }
}

std::uint32_t flash_colour(const Effects& fx, std::uint32_t colour) {
    if (fx.flash_scale >= 1.0f) {
        return colour;
    }
    const float k = fx.flash_scale < 0.0f ? 0.0f : fx.flash_scale;
    std::uint32_t out = colour & 0xFF000000u;
    for (int shift = 0; shift < 24; shift += 8) {
        const float c = static_cast<float>((colour >> shift) & 0xFFu) * k;
        out |= static_cast<std::uint32_t>(c + 0.5f) << shift;
    }
    return out;
}

void effects_screen(Effects& fx, Draw2D& draw, std::vector<SlotTexture>& slot_textures,
                    const ScreenView* view) {
    const auto put = [&](int slot, int texture, int w, int h) {
        if (static_cast<int>(slot_textures.size()) <= slot) {
            slot_textures.resize(static_cast<std::size_t>(slot) + 1);
        }
        slot_textures[static_cast<std::size_t>(slot)] = {texture, w, h};
    };
    put(scene_slot, texture_scene, 640, 480);
    put(flicker_slot, fx.assets.flicker, 128, 8);
    put(flare_slot, fx.assets.flare, 128, 128);
    // FLASHES dims the flicker and the flare (the scratches are the scene's own pixels)
    const auto dim = [&](Quad2D& q) {
        if (q.slot == flicker_slot || q.slot == flare_slot) {
            q.alpha *= fx.flash_scale;
        }
    };
    // one view: the static over the whole window (quad_full_window), the bands from the screen's
    // left edge across its 640 (retail's span the 576 of the safe area its 3D fills), the
    // scratches' picture the scene's whole width
    if (!view) {
        for (int i = 0; i < static_cast<int>(fx.screen.size()); ++i) {
            Quad2D q = fx.screen[static_cast<std::size_t>(i)];
            dim(q);
            if (i < fx.screen_static) {
                q.flags |= quad_full_window;
                q.x = 0.0f;
                q.w = 640.0f;
                if (q.slot == scene_slot) {
                    q.u0 = 0.0f;
                    q.u1 = 640.0f;
                }
            }
            submit_quad(draw, q);
        }
        return;
    }
    // a split-screen view: the static's band of the 576 x 448 safe area fitted to the view's
    // rectangle (its quads placed from the view's corner, Quad2D::view), the scratches' picture
    // taken from where that view's 3D is drawn; the flare is single view only
    if (!view->static_shown) {
        return;
    }
    const float* r = view->rect;
    const float* d = view->drawn;
    const float sx = r[2] / 576.0f, sy = r[3] / 448.0f;
    for (int i = 0; i < fx.screen_static && i < static_cast<int>(fx.screen.size()); ++i) {
        Quad2D q = fx.screen[static_cast<std::size_t>(i)];
        q.view = view->view;
        dim(q);
        q.y = 16.0f + (q.y - 16.0f) * sy;
        q.w *= sx;
        q.h = q.h * sy < 1.0f ? 1.0f : q.h * sy;
        if (q.slot == scene_slot) {
            const float dx = d[2] / 576.0f, dy = d[3] / 448.0f;
            q.u0 = d[0] + (q.u0 - 32.0f) * dx;
            q.u1 = d[0] + (q.u1 - 32.0f) * dx;
            q.v0 = d[1] + (q.v0 - 16.0f) * dy;
            q.v1 = d[1] + (q.v1 - 16.0f) * dy;
        }
        submit_quad(draw, q);
    }
}
