// Widescreen anchors' shapes (ui_anchors.hpp): a background or bar continued past the 4:3 box with
// its own edge band (mirrored, tiled or clamped; cut into bands of rows, or opened at a column for
// the edge-anchored pieces), and the blurred, dimmed copy behind a picture.
#include "screens/ui/ui_anchors.hpp"
#include "talk/draw2d.hpp"

#include <algorithm>
#include <cmath>

namespace {

// a copy of q over [a, b) along x (axis 0) or y (1) showing texels t0 at a to t1 at b
Quad2D piece(const Quad2D& q, int axis, float a, float b, float t0, float t1) {
    Quad2D p = q;
    if (axis == 0) {
        p.x = a;
        p.w = b - a;
        p.u0 = t0;
        p.u1 = t1;
        p.flags &= ~0x400u;
        p.rot[0] = p.w * 0.5f;
    } else {
        p.y = a;
        p.h = b - a;
        p.v0 = t0;
        p.v1 = t1;
        p.flags &= ~0x800u;
        p.rot[1] = p.h * 0.5f;
    }
    return p;
}

// a continuation piece softened: blurred by the rule's radius and dimmed (a picture's sides, so
// nothing near its edges shows again sharp, mirrored)
Quad2D soften(Quad2D p, const AnchorRule& rule) {
    if (rule.soft > 0) {
        p.blur = rule.soft;
        if (rule.lines[0] > 0 || rule.lines[1] > 0) {
            // the picture's own lines kept: whole periods, sampled as the picture is
            p.blur_period[0] = rule.lines[0];
            p.blur_period[1] = rule.lines[1];
        } else {
            p.flags |= sprite_smooth; // filtered
        }
    }
    if (rule.soft_dim < 1.0f) {
        const auto dim = [&](int shift) {
            const float c = static_cast<float>((p.color >> shift) & 0xff) * rule.soft_dim;
            return static_cast<std::uint32_t>(std::clamp(c, 0.0f, 255.0f)) << shift;
        };
        p.color = (p.color & 0xff000000u) | dim(16) | dim(8) | dim(0);
    }
    return p;
}

// the screen's x [a, b) beside q, over q's rows, covered with the rule's patch of q's picture
// mirrored back and forth both ways from the side touching q (toward_low: the patch's copies run
// left from b), softened: art beside a picture whose own edge holds marks that mustn't repeat.
// All at the picture's own scale
void fill_side(const Quad2D& q, float a, float b, bool toward_low, const AnchorRule& rule,
               std::vector<Quad2D>& out) {
    if (q.w <= 0 || q.h <= 0 || q.u1 == q.u0 || q.v1 == q.v0) {
        return;
    }
    // a picture with scanlines: the patch's rows start in the same phase as q's top row and run
    // whole periods, so the lines run on in step
    const float period = rule.lines[0];
    float v_top = rule.fill[1], v_bottom = rule.fill[3];
    if (period > 0) {
        v_top += std::fmod(std::fmod(q.v0 - v_top, period) + period, period);
        v_bottom = v_top + std::floor((v_bottom - v_top) / period) * period;
    }
    const float tw = (rule.fill[2] - rule.fill[0]) * q.w / std::fabs(q.u1 - q.u0);
    if (tw < 1 || v_bottom - v_top < 2) {
        return;
    }
    for (int i = 0; i < 64; ++i) {
        // a column of copies: its screen x and the patch's texels at its two ends
        const float c0 = toward_low ? b - tw * static_cast<float>(i + 1)
                                    : a + tw * static_cast<float>(i);
        const float c1 = c0 + tw;
        if (c1 <= a || c0 >= b) {
            break;
        }
        float ua = rule.fill[0], ub = rule.fill[2];
        if (i & 1) {
            std::swap(ua, ub); // mirrored, so neighbouring copies meet edge to edge
        }
        // the column cut to [a, b)
        const float x0 = std::max(c0, a), x1 = std::min(c1, b);
        const float u0 = ua + (ub - ua) * (x0 - c0) / tw, u1 = ua + (ub - ua) * (x1 - c0) / tw;
        // the patch's rows down the side: reflected about the middle of its first and last rows
        // (the edge row not doubled, so a scanline every 2 rows stays in step), or with longer
        // line periods repeated whole
        const float ky = q.h / std::fabs(q.v1 - q.v0); // screen pixels a texel row
        const bool reflect = period <= 2;
        float y0 = q.y;
        for (int j = 0; j < 128 && y0 < q.y + q.h; ++j) {
            float va = v_top, vb = v_bottom; // texel rows at the run's top and bottom edges
            if (reflect && j > 0) {
                va = (j & 1) ? v_bottom - 1 : v_top + 1;
                vb = (j & 1) ? v_top : v_bottom;
            }
            const float rows = std::fabs(vb - va);
            if (rows < 1) {
                break;
            }
            const float y1 = std::min(y0 + rows * ky, q.y + q.h);
            const float vend = va + (vb - va) * (y1 - y0) / (rows * ky);
            Quad2D p = piece(piece(q, 0, x0, x1, u0, u1), 1, y0, y1, va, vend);
            out.push_back(soften(p, rule));
            y0 = y1;
        }
    }
}

// q continued from its low (side 0) or high (side 1) edge along an axis to `to` with copies of its
// edge band: mirrored back and forth (seamless at every join), tiled (a pattern of that period),
// or its last texel stretched (clamp)
void extend_axis(const Quad2D& q, int axis, int side, float to, const AnchorRule& rule,
                 std::vector<Quad2D>& out) {
    // whole pixels outward: an unfiltered quad's left edge is floored when drawn, so a piece
    // starting between pixels would stop short of the picture and leave a gap a pixel wide
    to = side == 0 ? std::floor(to) : std::ceil(to);
    const float pos = axis == 0 ? q.x : q.y;
    const float size = axis == 0 ? q.w : q.h;
    const bool flipped = (q.flags & (axis == 0 ? 0x400u : 0x800u)) != 0;
    float t_low = axis == 0 ? q.u0 : q.v0, t_high = axis == 0 ? q.u1 : q.v1;
    if (flipped) {
        std::swap(t_low, t_high);
    }
    if (size <= 0 || t_low == t_high) {
        return;
    }
    const float k = (t_high - t_low) / size; // texels a screen pixel, signed
    const float edge = side == 0 ? pos : pos + size;
    const float t_edge = side == 0 ? t_low : t_high;
    const float inward = side == 0 ? k : -k; // texel step going into the quad
    const float span = side == 0 ? edge - to : to - edge;
    if (span <= 0) {
        return;
    }
    if (rule.mode == ExtendMode::clamp) {
        const float t = t_edge + (inward > 0 ? 0.5f : -0.5f); // the edge texel's middle
        out.push_back(soften(side == 0 ? piece(q, axis, to, edge, t, t)
                                       : piece(q, axis, edge, to, t, t),
                             rule));
        return;
    }
    // the band in whole screen pixels, so the copies meet without gaps
    const float width = std::max(1.0f, std::round(std::min(rule.band / std::fabs(k), size)));
    const float t_band = t_edge + inward * width; // the band's inner end
    int count = std::min(512, static_cast<int>(std::ceil(span / width)));
    if (axis == 0 && rule.fill[2] > rule.fill[0]) {
        // a patch of the picture behind, and one reflection of the band fading out over it
        fill_side(q, side == 0 ? to : edge, side == 0 ? edge : to, side == 0, rule, out);
        Quad2D p = side == 0 ? piece(q, 0, edge - width, edge, t_band, t_edge)
                             : piece(q, 0, edge, edge + width, t_edge, t_band);
        p.fade[side == 0 ? 0 : 1] = 0;
        out.push_back(soften(p, rule));
        return;
    }
    for (int i = 0; i < count; ++i) {
        const float near = side == 0 ? edge - width * static_cast<float>(i)
                                     : edge + width * static_cast<float>(i);
        const float far = side == 0 ? near - width : near + width;
        // texels at the copy's near and far ends: mirrored, the edge meets the edge and the band's
        // inner end the next copy's; tiled, every copy repeats the band as a period
        float t_near = t_edge, t_far = t_band;
        if (rule.mode == ExtendMode::tile || (i & 1)) {
            t_near = t_band, t_far = t_edge;
        }
        out.push_back(soften(side == 0 ? piece(q, axis, far, near, t_far, t_near)
                                       : piece(q, axis, near, far, t_near, t_far),
                             rule));
    }
}

// q with its outer `feather` pixels on the extended sides faded from its soft continuation (a
// blurred, dimmed copy under them) into the sharp picture, so the soft reflection beyond meets it
// without a seam; low and high: the left and right sides' rules, null where nothing feathers
void feather_edges(const Quad2D& q, const AnchorRule* low, const AnchorRule* high,
                   std::vector<Quad2D>& out) {
    const bool flipped = (q.flags & sprite_mirror_u) != 0;
    float ul = q.u0, uh = q.u1; // texels at the left and right edges
    if (flipped) {
        std::swap(ul, uh);
    }
    const float k = (uh - ul) / q.w;
    // a strip of q over screen x [a, b) with the texels there
    const auto strip = [&](float a, float b) {
        return piece(q, 0, a, b, ul + (a - q.x) * k, ul + (b - q.x) * k);
    };
    const float a = low ? q.x + std::min(low->feather, q.w * 0.25f) : q.x;
    const float b = high ? q.x + q.w - std::min(high->feather, q.w * 0.25f) : q.x + q.w;
    if (low) {
        out.push_back(soften(strip(q.x, a), *low));
    }
    if (high) {
        out.push_back(soften(strip(b, q.x + q.w), *high));
    }
    out.push_back(strip(a, b));
    if (low) {
        Quad2D p = strip(q.x, a);
        p.fade[0] = 0;
        out.push_back(p);
    }
    if (high) {
        Quad2D p = strip(b, q.x + q.w);
        p.fade[1] = 0;
        out.push_back(p);
    }
}

// a rule with one band of rows' treatment
AnchorRule band_rule(const AnchorRule& rule, const AnchorRows& r) {
    AnchorRule one = rule;
    one.mode = r.mode;
    one.band = r.band;
    one.split = r.split;
    one.soft = r.soft;
    one.soft_dim = r.soft_dim;
    one.feather = r.feather;
    std::copy(r.fill, r.fill + 4, one.fill);
    std::copy(r.lines, r.lines + 2, one.lines);
    return one;
}

// q continued to the window's left by `low` and right by `high` (null: not on that side)
void extend_slice(const Quad2D& q, const SpriteFrame& f, const AnchorRule* low,
                  const AnchorRule* high, std::vector<Quad2D>& out) {
    // a bar split at a texel column: the part left of it moves out with the left-anchored 2D,
    // the part right of it with the right, the column between stretched over the gap
    const AnchorRule* any = low ? low : high;
    const float split = any ? any->split : -1.0f;
    const float cut = q.u1 != q.u0 ? q.x + (split - q.u0) * q.w / (q.u1 - q.u0) : -1.0f;
    if (split >= 0 && f.edge > 0 && q.rot[2] == 0 && !(q.flags & sprite_mirror_u) && cut > q.x &&
        cut < q.x + q.w) {
        AnchorRule l = low ? *low : AnchorRule{}, h = high ? *high : AnchorRule{};
        l.split = h.split = -1;
        AnchorRule clamp = *any;
        clamp.split = -1;
        clamp.mode = ExtendMode::clamp;
        Quad2D left = piece(q, 0, q.x - f.edge, cut - f.edge, q.u0, split);
        Quad2D right = piece(q, 0, cut + f.edge, q.x + q.w + f.edge, split, q.u1);
        // the gap a pixel wider each way and drawn first, so the pieces' floored edges never
        // leave a seam between them
        Quad2D gap = piece(q, 0, cut - f.edge - 1, cut + f.edge + 1, split + 0.5f, split + 0.5f);
        extend_slice(gap, f, &clamp, &clamp, out);
        extend_slice(left, f, low ? &l : nullptr, nullptr, out);
        extend_slice(right, f, nullptr, high ? &h : nullptr, out);
        return;
    }
    const float reach = sprite_frame_reach(f) + 1.0f, reach_y = sprite_frame_reach_y(f) + 1.0f;
    std::vector<Quad2D> row;
    const bool on_low = low && q.rot[2] == 0 && q.x <= 34.0f;
    const bool on_high = high && q.rot[2] == 0 && q.x + q.w >= 606.0f;
    if (on_low) {
        extend_axis(q, 0, 0, 32.0f - reach, *low, row);
    }
    if (on_high) {
        extend_axis(q, 0, 1, 608.0f + reach, *high, row);
    }
    const auto feathers = [&](bool on, const AnchorRule* r) {
        return on && f.spread > 0 && r->feather > 0 && r->soft > 0 ? r : nullptr;
    };
    const AnchorRule* fl = feathers(on_low, low);
    const AnchorRule* fh = feathers(on_high, high);
    if (fl || fh) {
        feather_edges(q, fl, fh, row);
    } else {
        row.push_back(q);
    }
    // on to the window's top and bottom with the rule of the side each piece is on
    for (const Quad2D& r : row) {
        const AnchorRule* by = r.x + r.w <= q.x + 0.5f    ? low
                               : r.x >= q.x + q.w - 0.5f ? high
                                                          : any;
        if (by && q.rot[2] == 0 && f.spread > 0) {
            if (r.y <= 17.0f) {
                extend_axis(r, 1, 0, 16.0f - reach_y, *by, out);
            }
            if (r.y + r.h >= 463.0f) {
                extend_axis(r, 1, 1, 464.0f + reach_y, *by, out);
            }
        }
        out.push_back(r);
    }
}

} // namespace

// a background or bar touching the box's title-safe edges continued to the window's; a picture
// with bands of rows treated apart (a frame's bars over its picture) is cut into them first, each
// side by the bands that name it
void extend_quad(const Quad2D& whole, const SpriteFrame& f, const AnchorRule& rule,
                 std::vector<Quad2D>& out) {
    // what retail's title-safe area always hid (a frame's border art, often unfinished) cut off
    // first in a wide window: the screen's rectangle `inset` in from the sides, half that from the
    // top and bottom (32 is the safe area)
    Quad2D q = whole;
    if (rule.inset > 0 && f.spread > 0 && q.rot[2] == 0 && q.w > 0 && q.h > 0 &&
        !(q.flags & (sprite_mirror_u | sprite_mirror_v))) {
        const float ku = (q.u1 - q.u0) / q.w, kv = (q.v1 - q.v0) / q.h;
        const float x0 = std::max(q.x, rule.inset), x1 = std::min(q.x + q.w, 640.0f - rule.inset);
        const float y0 = std::max(q.y, rule.inset * 0.5f);
        const float y1 = std::min(q.y + q.h, 480.0f - rule.inset * 0.5f);
        if (x1 <= x0 || y1 <= y0) {
            return;
        }
        Quad2D cut = piece(q, 0, x0, x1, q.u0 + (x0 - q.x) * ku, q.u0 + (x1 - q.x) * ku);
        q = piece(cut, 1, y0, y1, q.v0 + (y0 - q.y) * kv, q.v0 + (y1 - q.y) * kv);
    }
    if (rule.rows.empty() || q.rot[2] != 0 || q.v1 == q.v0 || (q.flags & sprite_mirror_v)) {
        extend_slice(q, f, &rule, &rule, out);
        return;
    }
    // the picture cut at every band's ends; each slice extended left by the first band for the
    // left side holding it, right by the first for the right
    std::vector<float> cuts = {q.v0, q.v1};
    for (const AnchorRows& r : rule.rows) {
        for (const float v : {r.v0, r.v1}) {
            if (v > q.v0 && v < q.v1) {
                cuts.push_back(v);
            }
        }
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    const float k = q.h / (q.v1 - q.v0); // screen pixels a texel row
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        const float v0 = cuts[i], v1 = cuts[i + 1];
        const AnchorRows* band[2] = {nullptr, nullptr};
        for (const AnchorRows& r : rule.rows) {
            for (int side = 0; side < 2; ++side) {
                if (!band[side] && r.v0 <= v0 && r.v1 >= v1 && (r.side < 0 || r.side == side)) {
                    band[side] = &r;
                }
            }
        }
        Quad2D slice = q;
        slice.y = q.y + (v0 - q.v0) * k;
        slice.h = (v1 - v0) * k;
        slice.v0 = v0;
        slice.v1 = v1;
        slice.rot[1] = slice.h * 0.5f;
        AnchorRule low, high;
        if (band[0]) {
            low = band_rule(rule, *band[0]);
        }
        if (band[1]) {
            high = band_rule(rule, *band[1]);
        }
        extend_slice(slice, f, band[0] ? &low : nullptr, band[1] ? &high : nullptr, out);
    }
}

// a blurred, dimmed copy of q covering the window behind it, then q
void blur_quad(const Quad2D& q, const SpriteFrame& f, const AnchorRule& rule,
               std::vector<Quad2D>& out) {
    if (f.spread > 0 && q.w > 0 && q.h > 0) {
        const float reach = sprite_frame_reach(f) + 1.0f, reach_y = sprite_frame_reach_y(f) + 1.0f;
        const float w = 576.0f + reach * 2.0f, h = 448.0f + reach_y * 2.0f;
        const float scale = std::max(w / q.w, h / q.h);
        Quad2D b = q;
        b.w = q.w * scale;
        b.h = q.h * scale;
        b.x = 320.0f - b.w * 0.5f;
        b.y = 240.0f - b.h * 0.5f;
        b.rot[0] = b.w * 0.5f;
        b.rot[1] = b.h * 0.5f;
        b.flags |= sprite_smooth; // filtered
        b.anchor = anchor_free;
        b.blur = rule.radius;
        const auto dim = [&](int shift) {
            const float c = static_cast<float>((q.color >> shift) & 0xff) * rule.dim;
            return static_cast<std::uint32_t>(std::clamp(c, 0.0f, 255.0f)) << shift;
        };
        b.color = (q.color & 0xff000000u) | dim(16) | dim(8) | dim(0);
        out.push_back(b);
    }
    out.push_back(q);
}
