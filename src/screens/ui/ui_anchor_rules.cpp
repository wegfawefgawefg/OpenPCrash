// Widescreen anchors' rules (ui_anchors.hpp), read from assets/ui/anchors.sexp: each screen's
// rules by sprite, slot or region, the kinds and extend modes, and the groups screens take in.
#include "screens/ui/ui_anchors.hpp"

#include "cook/sexp_read.hpp"

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
