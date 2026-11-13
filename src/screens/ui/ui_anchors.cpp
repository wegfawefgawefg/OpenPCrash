// Widescreen anchors (ui_anchors.hpp): the rules read from assets/ui/anchors.sexp, a quad's anchor
// found from them, and the 2D list moved, extended and backed for the window's shape.
#include "screens/ui/ui_anchors.hpp"

#include "cook/sexp_read.hpp"
#include "render/vk_sprites.hpp"
#include "screens/ui/menu_pointer.hpp"
#include "screens/ui/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace {

bool kind_of(std::string_view text, AnchorKind& out) {
    static const struct {
        const char* name;
        AnchorKind kind;
    } names[] = {{"centre", AnchorKind::centre}, {"left", AnchorKind::left},
                 {"right", AnchorKind::right},   {"free", AnchorKind::free},
                 {"extend", AnchorKind::extend}, {"blur", AnchorKind::blur}};
    for (const auto& n : names) {
        if (text == n.name) {
            out = n.kind;
            return true;
        }
    }
    return false;
}

// (fill U0 V0 U1 V1): a patch of texels
void read_fill(const gsexp::Node& form, float out[4]) {
    for (std::size_t i = 0; i < 4 && i + 1 < form.child_count(); ++i) {
        out[i] = atom_float(form.child_at(i + 1));
    }
}

// (lines ROWS [COLUMNS]): a picture's line periods
void read_lines(const gsexp::Node& form, float out[2]) {
    for (std::size_t i = 0; i < 2 && i + 1 < form.child_count(); ++i) {
        out[i] = atom_float(form.child_at(i + 1));
    }
}

// an extend's [mirror|tile|clamp] [band] from `next` on
void read_mode(const gsexp::Node& form, std::size_t& next, ExtendMode& mode, float& band) {
    if (next < form.child_count() && !form.child_at(next).is_list()) {
        const std::string_view text = form.child_at(next).text();
        if (text == "mirror" || text == "tile" || text == "clamp") {
            mode = text == "tile"    ? ExtendMode::tile
                   : text == "clamp" ? ExtendMode::clamp
                                     : ExtendMode::mirror;
            ++next;
        }
    }
    if (next < form.child_count() && !form.child_at(next).is_list()) {
        band = atom_float(form.child_at(next++));
    }
}

// a rule's kind and its arguments from the node `at` on: extend [mirror|tile|clamp] [band]
// (rows V0 V1 [mode] [band])..., blur [radius] [dim]
bool read_kind(const gsexp::Node& form, std::size_t at, AnchorRule& rule) {
    if (at >= form.child_count() || !kind_of(form.child_at(at).text(), rule.kind)) {
        return false;
    }
    std::size_t next = at + 1;
    if (rule.kind == AnchorKind::extend) {
        read_mode(form, next, rule.mode, rule.band);
        for (; next < form.child_count(); ++next) {
            const gsexp::Node item = form.child_at(next);
            if (item.is_list() && item.head().is_atom("split")) {
                rule.split = atom_float(item.child_at(1));
            }
            if (item.is_list() && item.head().is_atom("inset")) {
                rule.inset = atom_float(item.child_at(1));
            }
            if (item.is_list() && item.head().is_atom("soft")) {
                rule.soft = atom_float(item.child_at(1));
                rule.soft_dim = item.child_count() > 2 ? atom_float(item.child_at(2)) : 1.0f;
            }
            if (item.is_list() && item.head().is_atom("feather")) {
                rule.feather = atom_float(item.child_at(1));
            }
            if (item.is_list() && item.head().is_atom("fill")) {
                read_fill(item, rule.fill);
            }
            if (item.is_list() && item.head().is_atom("lines")) {
                read_lines(item, rule.lines);
            }
            if (!item.is_list() || !item.head().is_atom("rows")) {
                continue;
            }
            AnchorRows r;
            r.v0 = atom_float(item.child_at(1));
            r.v1 = atom_float(item.child_at(2));
            r.mode = rule.mode;
            r.band = rule.band;
            r.feather = rule.feather;
            std::copy(rule.fill, rule.fill + 4, r.fill);
            std::copy(rule.lines, rule.lines + 2, r.lines);
            std::size_t k = 3;
            read_mode(item, k, r.mode, r.band);
            for (; k < item.child_count(); ++k) {
                const gsexp::Node part = item.child_at(k);
                if (part.is_list() && part.head().is_atom("split")) {
                    r.split = atom_float(part.child_at(1));
                }
                if (part.is_list() && part.head().is_atom("soft")) {
                    r.soft = atom_float(part.child_at(1));
                    r.soft_dim = part.child_count() > 2 ? atom_float(part.child_at(2)) : 1.0f;
                }
                if (part.is_list() && part.head().is_atom("feather")) {
                    r.feather = atom_float(part.child_at(1));
                }
                if (part.is_list() && part.head().is_atom("fill")) {
                    read_fill(part, r.fill);
                }
                if (part.is_list() && part.head().is_atom("lines")) {
                    read_lines(part, r.lines);
                }
                if (part.is_list() && part.head().is_atom("side")) {
                    r.side = part.child_at(1).is_atom("right") ? 1 : 0;
                }
            }
            rule.rows.push_back(r);
        }
    } else if (rule.kind == AnchorKind::blur) {
        if (next < form.child_count()) {
            rule.radius = atom_float(form.child_at(next++));
        }
        if (next < form.child_count()) {
            rule.dim = atom_float(form.child_at(next));
        }
    }
    return true;
}

// a sprite id or a range of them ("0x560-0x573"), as [lo, hi]
void read_ids(std::string_view text, std::vector<int>& out) {
    const std::string s(text);
    char* end = nullptr;
    const int lo = static_cast<int>(std::strtol(s.c_str(), &end, 0));
    int hi = lo;
    if (end && *end == '-') {
        hi = static_cast<int>(std::strtol(end + 1, nullptr, 0));
    }
    for (int id = lo; id <= hi && id - lo < 4096; ++id) {
        out.push_back(id);
    }
}

// a region (in X0 Y0 X1 Y1) a sprite rule is limited to
void read_in(const gsexp::Node& in, AnchorRule& rule) {
    rule.region = true;
    for (int i = 0; i < 4; ++i) {
        rule.rect[i] = atom_float(in.child_at(static_cast<std::size_t>(1 + i)));
    }
}

// one rule form: (sprite ID|LO-HI... [(in X0 Y0 X1 Y1)] KIND ...), (slots LO HI KIND ...),
// (region X0 Y0 X1 Y1 KIND ...), (ticker-end X0 Y0 X1 Y1 KIND)
bool read_rule(const gsexp::Node& form, AnchorRule& rule) {
    const gsexp::Node head = form.head();
    if (head.is_atom("sprite")) {
        std::size_t i = 1;
        for (; i < form.child_count(); ++i) {
            const gsexp::Node item = form.child_at(i);
            AnchorKind probe;
            if (item.is_list() && item.head().is_atom("in")) {
                read_in(item, rule);
                continue;
            }
            if (kind_of(item.text(), probe)) {
                break;
            }
            read_ids(item.text(), rule.sprites);
        }
        return read_kind(form, i, rule);
    }
    if (head.is_atom("size")) {
        rule.size[0] = atom_float(form.child_at(1));
        rule.size[1] = atom_float(form.child_at(2));
        std::size_t i = 3;
        if (i < form.child_count() && form.child_at(i).is_list() &&
            form.child_at(i).head().is_atom("in")) {
            read_in(form.child_at(i++), rule);
        }
        return read_kind(form, i, rule);
    }
    if (head.is_atom("slots")) {
        rule.slot_lo = atom_int(form.child_at(1));
        rule.slot_hi = atom_int(form.child_at(2));
        std::size_t i = 3;
        if (i < form.child_count() && form.child_at(i).is_list() &&
            form.child_at(i).head().is_atom("in")) {
            read_in(form.child_at(i++), rule);
        }
        return read_kind(form, i, rule);
    }
    if (head.is_atom("region") || head.is_atom("ticker-end")) {
        rule.ticker_end = head.is_atom("ticker-end");
        rule.region = true;
        for (int i = 0; i < 4; ++i) {
            rule.rect[i] = atom_float(form.child_at(static_cast<std::size_t>(1 + i)));
        }
        return read_kind(form, 5, rule);
    }
    return false;
}

// a screen's or group's body: rules, (use GROUP), (states ...), (backdrop COLOUR)
void read_body(const gsexp::Node& form, std::size_t from,
               const std::map<std::string, std::vector<AnchorRule>>& groups, AnchorScreen& out) {
    for (std::size_t i = from; i < form.child_count(); ++i) {
        const gsexp::Node item = form.child_at(i);
        if (!item.is_list()) {
            continue;
        }
        if (item.head().is_atom("states")) {
            for (std::size_t k = 1; k < item.child_count(); ++k) {
                out.states.push_back(atom_int(item.child_at(k)));
            }
        } else if (item.head().is_atom("shows")) {
            for (std::size_t k = 1; k < item.child_count(); ++k) {
                read_ids(item.child_at(k).text(), out.shows);
            }
        } else if (item.head().is_atom("backdrop")) {
            out.backdrop = atom_hex(item.child_at(1));
        } else if (item.head().is_atom("use")) {
            const auto found = groups.find(std::string(item.child_at(1).text()));
            if (found == groups.end()) {
                std::fprintf(stderr, "anchors: no group %s\n",
                             std::string(item.child_at(1).text()).c_str());
                continue;
            }
            out.rules.insert(out.rules.end(), found->second.begin(), found->second.end());
            out.hud = out.hud || found->first == "hud";
        } else {
            AnchorRule rule;
            if (read_rule(item, rule)) {
                out.rules.push_back(rule);
            } else {
                std::fprintf(stderr, "anchors: a rule in %s doesn't read\n", out.name.c_str());
            }
        }
    }
}

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

bool load_ui_anchors(UiAnchors& anchors, const std::string& path, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(path, parsed, error)) {
        return false;
    }
    std::map<std::string, std::vector<AnchorRule>> groups;
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node form = parsed.root(i);
        if (!form.is_list()) {
            continue;
        }
        AnchorScreen screen;
        screen.name = std::string(form.child_at(1).text());
        read_body(form, 2, groups, screen);
        if (form.head().is_atom("group")) {
            groups[screen.name] = screen.rules;
        } else if (form.head().is_atom("screen")) {
            anchors.screens.push_back(std::move(screen));
        }
    }
    return true;
}

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
                   Draw2D& out) {
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
        const AnchorRule* rule = nullptr;
        const AnchorKind kind =
            resolve(screen, q, view >= 0 ? renderer_sprite_view_rect(renderer, view) : nullptr,
                    &rule);
        if (hud_size != 100 && hud_piece(q, kind)) {
            hud_scale(q, kind, static_cast<float>(hud_size) / 100.0f);
        }
        switch (kind) {
        case AnchorKind::centre:
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
