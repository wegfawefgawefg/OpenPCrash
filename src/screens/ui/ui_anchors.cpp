// Widescreen anchors (ui_anchors.hpp): a quad's anchor found from the rules (read in
// ui_anchor_rules.cpp), and the 2D list moved, extended and backed for the window's shape.
#include "screens/ui/ui_anchors.hpp"

#include "render/vk_sprites.hpp"
#include "screens/ui/menu_pointer.hpp"
#include "screens/ui/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
// a quad's middle where its turn puts it (a wheel's items turn about a far pivot)
void quad_middle(const Quad2D& q, float& cx, float& cy) {
    cx = q.x + q.w * 0.5f, cy = q.y + q.h * 0.5f;
    if (q.rot[2] != 0) {
        const float px = q.x + q.rot[0], py = q.y + q.rot[1];
        const float c = std::cos(q.rot[2]), s = std::sin(q.rot[2]);
        const float dx = cx - px, dy = cy - py;
        cx = px + dx * c - dy * s;
        cy = py + dx * s + dy * c;
    }
}

// HUD SIZE (RenderOptions::hud_size): the pieces of the fight's HUD it scales. The HUD's own layers
// (hud.md: 0x101..0x103, 0x108; not the VS marks' 0x105, nor the barks' and scene text's 0x104
// panels and 0x113 pictures, whose words are the font's and stay), anchored left or right, or in
// the middle of the bottom (the durability and overcharge gauges); the reticle, boxes, marks and
// tags are free
bool hud_piece(const Quad2D& q, AnchorKind kind) {
    const int layer = q.layer;
    if (layer < 0x101 || layer > 0x108 || (layer >= 0x104 && layer <= 0x107)) {
        return false;
    }
    if (kind == AnchorKind::left || kind == AnchorKind::right) {
        return true;
    }
    float cx = 0, cy = 0;
    quad_middle(q, cx, cy);
    return kind == AnchorKind::centre && cy >= 360.0f;
}

// a HUD piece grown or shrunk about its place on the title-safe area's edge: across, the side its
// anchor keeps it to (32 or 608; the bottom's middle 320); down, the top (16) or bottom (464) for
// the pieces in the top or bottom fifth, else the middle (240), so the weapon readouts stacked
// down the sides spread apart or close up evenly instead of running into each other
void hud_scale(Quad2D& q, AnchorKind kind, float scale) {
    float cx = 0, cy = 0;
    quad_middle(q, cx, cy);
    const float ax = kind == AnchorKind::left ? 32.0f : kind == AnchorKind::right ? 608.0f : 320.0f;
    const float ay = cy < 96.0f ? 16.0f : cy > 384.0f ? 464.0f : 240.0f;
    q.x = ax + (q.x - ax) * scale;
    q.y = ay + (q.y - ay) * scale;
    q.w *= scale;
    q.h *= scale;
    q.rot[0] *= scale;
    q.rot[1] *= scale;
}

AnchorKind resolve(const AnchorScreen* screen, const Quad2D& q, const float* view_rect,
                   const AnchorRule** rule) {
    const int id = q.slot - sprite_slot_base;
    float cx = 0, cy = 0;
    quad_middle(q, cx, cy);
    const float rect[4] = {cx - q.w * 0.5f, cy - q.h * 0.5f, q.w, q.h};
    return anchor_kind_of(screen, id >= 0 ? id : -1, q.slot, rect, view_rect, rule);
}

int dump_at() {
    static const int at = [] {
        const char* env = std::getenv("PHANTOMCRASH_DEV_ANCHORS");
        return env ? std::atoi(env) : -1;
    }();
    return at;
}

const char* kind_name(AnchorKind kind) {
    static const char* names[] = {"centre", "left", "right", "free", "extend", "blur"};
    return names[static_cast<int>(kind)];
}

} // namespace

const AnchorScreen* ui_anchor_screen(const UiAnchors& anchors, int state, const Draw2D* draw) {
    const AnchorScreen* plain = nullptr;
    for (const AnchorScreen& screen : anchors.screens) {
        if (std::find(screen.states.begin(), screen.states.end(), state) == screen.states.end()) {
            continue;
        }
        if (screen.shows.empty()) {
            plain = plain ? plain : &screen;
            continue;
        }
        if (draw) {
            for (const Quad2D& q : draw->quads) {
                const int id = q.slot - sprite_slot_base;
                if (std::find(screen.shows.begin(), screen.shows.end(), id) != screen.shows.end()) {
                    return &screen;
                }
            }
        }
    }
    return plain;
}

AnchorKind anchor_kind_of(const AnchorScreen* screen, int sprite, int slot, const float rect[4],
                          const float* view_rect, const AnchorRule** rule) {
    if (!screen) {
        return AnchorKind::centre;
    }
    float x = rect[0] + rect[2] * 0.5f, y = rect[1] + rect[3] * 0.5f;
    const float w = std::fabs(rect[2]), h = std::fabs(rect[3]);
    // a split view's layout is retail's one-view layout fitted to its rectangle: back to that
    if (view_rect && view_rect[2] > 0 && view_rect[3] > 0) {
        x = 32.0f + (x - 32.0f) * 576.0f / view_rect[2];
        y = 16.0f + (y - 16.0f) * 448.0f / view_rect[3];
    }
    for (const AnchorRule& r : screen->rules) {
        if (r.ticker_end != (slot == anchor_slot_ticker_end)) {
            continue;
        }
        bool match = true;
        if (r.region) {
            match = x >= r.rect[0] && x < r.rect[2] && y >= r.rect[1] && y < r.rect[3];
        }
        if (r.slot_lo >= 0) {
            match = match && slot >= r.slot_lo && slot <= r.slot_hi;
        }
        if (!r.sprites.empty()) {
            match = match && sprite >= 0 &&
                    std::find(r.sprites.begin(), r.sprites.end(), sprite) != r.sprites.end();
        }
        if (r.size[0] >= 0) {
            match = match && std::fabs(w - r.size[0]) < 0.5f && std::fabs(h - r.size[1]) < 0.5f;
        }
        if (match) {
            if (rule) {
                *rule = &r;
            }
            return r.kind;
        }
    }
    return AnchorKind::centre;
}

float anchor_ticker_end_shift(const AnchorScreen* screen, const SpriteFrame& frame, float x,
                              float y) {
    const float point[4] = {x, y, 0, 0};
    const AnchorKind kind =
        anchor_kind_of(screen, -1, anchor_slot_ticker_end, point, nullptr);
    return kind == AnchorKind::left ? -frame.edge : kind == AnchorKind::right ? frame.edge : 0.0f;
}

PointerRect anchor_rect(const AnchorScreen* screen, const SpriteFrame& frame,
                        const PointerRect& rect) {
    const float box[4] = {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    const AnchorKind kind = anchor_kind_of(screen, rect.sprite, -1, box, nullptr);
    const float dx = kind == AnchorKind::left ? -frame.edge
                     : kind == AnchorKind::right ? frame.edge
                                                 : 0.0f;
    PointerRect out = rect;
    out.left += dx;
    out.right += dx;
    return out;
}

void anchors_apply(const AnchorScreen* screen, const Renderer& renderer, const Draw2D& in,
                   Draw2D& out, const AnchorScreen* panel) {
    out.quads.clear();
    out.quads.reserve(in.quads.size() + 64);
    SpriteFrame frames[5];
    for (int v = -1; v < 4; ++v) {
        frames[v + 1] = renderer_sprite_frame(renderer, v);
    }
    const int views = renderer_sprite_view_count(renderer);
    // HUD SIZE, in one view (split views keep the original's smaller tables as they are)
    const int hud_size =
        screen && screen->hud && views <= 1 ? std::clamp(renderer.options.hud_size, 50, 200) : 100;
    // the backdrop: a flat colour over the whole window under everything
    if (screen && screen->backdrop != 0) {
        Quad2D back;
        back.x = 0;
        back.y = 0;
        back.w = 640;
        back.h = 480;
        back.color = screen->backdrop;
        back.layer = 0;
        back.flags = quad_full_window;
        out.quads.push_back(back);
    }
    for (const Quad2D& quad : in.quads) {
        Quad2D q = quad;
        if ((q.flags & quad_full_window) || q.anchor != anchor_auto) {
            out.quads.push_back(q);
            continue;
        }
        const int view = q.view >= 0 && q.view < views ? q.view : -1;
        const SpriteFrame& f = frames[view + 1];
        // a panel over the screen goes by its own rules (ui.hpp, Ui::panel)
        const bool on_panel = (q.flags & quad_panel) != 0;
        const AnchorRule* rule = nullptr;
        const AnchorKind kind =
            resolve(on_panel ? panel : screen, q,
                    view >= 0 ? renderer_sprite_view_rect(renderer, view) : nullptr, &rule);
        if (hud_size != 100 && !on_panel && hud_piece(q, kind)) {
            hud_scale(q, kind, static_cast<float>(hud_size) / 100.0f);
        }
        switch (kind) {
        case AnchorKind::centre:
            // what lies wholly outside retail's title-safe area never showed; in a wide window
            // the stretched viewport (spritemat's w - 1) would let its edge pixel through beside
            // the box (the records' label at x 0..32 as a stray tick at 32)
            if (view < 0 && f.spread > 0 && q.rot[2] == 0 &&
                (q.x + q.w <= 32.0f || q.x >= 608.0f || q.y + q.h <= 16.0f || q.y >= 464.0f)) {
                continue;
            }
            q.anchor = anchor_centre;
            break;
        case AnchorKind::left:
            q.x -= f.edge;
            q.anchor = anchor_left;
            break;
        case AnchorKind::right:
            q.x += f.edge;
            q.anchor = anchor_right;
            break;
        case AnchorKind::free:
            q.anchor = anchor_free;
            break;
        case AnchorKind::extend:
            q.anchor = anchor_free;
            extend_quad(q, f, *rule, out.quads);
            continue;
        case AnchorKind::blur:
            q.anchor = anchor_free;
            blur_quad(q, f, *rule, out.quads);
            continue;
        }
        out.quads.push_back(q);
    }
    // the list's order is the draw order for equal layers
    for (std::size_t i = 0; i < out.quads.size(); ++i) {
        out.quads[i].sequence = static_cast<int>(i);
    }
}

void anchors_dev_dump(const AnchorScreen* screen, const Renderer& renderer, const Draw2D& draw) {
    static int presents = 0;
    if (dump_at() < 0 || presents++ != dump_at()) {
        return;
    }
    const int views = renderer_sprite_view_count(renderer);
    std::fprintf(stderr, "anchors: screen %s, %zu quads\n", screen ? screen->name.c_str() : "-",
                 draw.quads.size());
    for (const Quad2D& q : draw.quads) {
        const int view = q.view >= 0 && q.view < views ? q.view : -1;
        const int id = q.slot - sprite_slot_base;
        const AnchorKind kind =
            resolve(screen, q, view >= 0 ? renderer_sprite_view_rect(renderer, view) : nullptr,
                    nullptr);
        float cx = 0, cy = 0;
        quad_middle(q, cx, cy);
        std::fprintf(stderr,
                     "anchors: id %d slot %d at %.0f %.0f size %.0f %.0f layer 0x%x view %d %s%s"
                     " mid %.0f %.0f\n",
                     id >= 0 ? id : -1, q.slot, q.x, q.y, q.w, q.h, q.layer, q.view,
                     kind_name(kind), (q.flags & quad_full_window) ? " full" : "", cx, cy);
    }
}
