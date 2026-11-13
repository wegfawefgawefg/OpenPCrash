// The part tables for the screens (tables/bodies.sexp ... shop_stock.sexp, scoobees.sexp), and the
// curve readers of decomp src/hand/curve.c.
#include "screens/garage/part_tables.hpp"

#include "cook/sexp_read.hpp"
#include "screens/ui/ui_data.hpp"

#include <cmath>

namespace {

// the font's Shift-JIS code per glyph (text/font.sexp), for the names' sort keys
std::vector<std::uint16_t> font_sjis(UiData& data) {
    std::vector<std::uint16_t> out;
    gsexp::ParseResult parsed;
    std::string error;
    if (!ui_sexp(data, "text/font.sexp", parsed, error)) {
        return out;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("glyph")) {
            continue;
        }
        const int glyph = atom_int(form.arg(0));
        if (glyph < 0 || glyph > 0x4000) {
            continue;
        }
        if (static_cast<int>(out.size()) <= glyph) {
            out.resize(static_cast<std::size_t>(glyph) + 1, 0);
        }
        out[static_cast<std::size_t>(glyph)] =
            static_cast<std::uint16_t>(atom_hex(form.find_arg("sjis", 0)));
    }
    return out;
}

PartName read_name(UiData& data, const std::vector<std::uint16_t>& sjis,
                   const gsexp::FormView& form) {
    PartName name;
    const gsexp::Node text = form.find_arg("name", 0);
    if (!text.valid() || !text.is_string()) {
        return name;
    }
    name.glyphs = utf8_to_glyphs(data, std::string(text.text()));
    for (const std::int16_t glyph : name.glyphs) {
        const std::uint16_t code =
            glyph >= 0 && glyph < static_cast<int>(sjis.size()) ? sjis[static_cast<std::size_t>(glyph)]
                                                                : 0x8140;
        if (code > 0xff) {
            name.sjis.push_back(static_cast<char>(code >> 8));
        }
        name.sjis.push_back(static_cast<char>(code & 0xff));
    }
    name.glyphs.push_back(-1);
    return name;
}

Curve read_curve(const gsexp::FormView& form, const char* field, CurveKind kind) {
    Curve curve;
    curve.kind = kind;
    const gsexp::Node node = form.find(field);
    if (!node.valid() || node.child_count() < 2) {
        return curve;
    }
    const gsexp::Node c = node.child_at(1);
    if (!c.is_list() || c.child_count() < 6) {
        return curve;
    }
    const auto num = [](gsexp::Node n) { return std::strtod(std::string(n.text()).c_str(), nullptr); };
    curve.base = num(c.child_at(1));
    curve.at_mid = num(c.child_at(2));
    curve.at_max = num(c.child_at(3));
    curve.mid = atom_int(c.child_at(4));
    curve.negative = atom_int(c.child_at(5));
    // the record's own types: CurveF holds floats
    if (kind == CurveKind::f) {
        curve.base = static_cast<float>(curve.base);
        curve.at_mid = static_cast<float>(curve.at_mid);
        curve.at_max = static_cast<float>(curve.at_max);
    }
    return curve;
}

std::vector<StockItem> read_items(const gsexp::FormView& form) {
    std::vector<StockItem> items;
    for (gsexp::Node pair : form.find("items").children()) {
        if (pair.is_list() && pair.child_count() >= 2) {
            items.push_back({atom_int(pair.child_at(0)), atom_int(pair.child_at(1))});
        }
    }
    return items;
}

template <class T> T* row_at(std::vector<T>& rows, int id) {
    if (id < 0) {
        return nullptr;
    }
    if (static_cast<int>(rows.size()) <= id) {
        rows.resize(static_cast<std::size_t>(id) + 1);
    }
    return &rows[static_cast<std::size_t>(id)];
}

template <class T> const T* lookup(const std::vector<T>& rows, int id) {
    if (id < 0 || rows.empty()) {
        return nullptr;
    }
    return &rows[static_cast<std::size_t>(id < static_cast<int>(rows.size()) ? id : 0)];
}

void read_pair(const gsexp::FormView& form, const char* field, int& id, int& tune) {
    const gsexp::Node node = form.find(field);
    if (node.valid() && node.child_count() >= 3) {
        id = atom_int(node.child_at(1));
        tune = atom_int(node.child_at(2));
    }
}

} // namespace

// decomp: float_curve_value 0x9247D, rumble_curve_value 0x92291, Curve::value_at 0x920E0
float curve_value(const Curve& c, int tune) {
    // the original's x87 runs at double precision and stores the float result
    const double t = std::fabs(static_cast<double>(tune));
    const double scale = tune < 0 ? static_cast<double>(c.negative) : 1.0;
    double res = 0;
    if (c.kind == CurveKind::u) {
        // rumble_curve_value: unsigned words, at_max - at_mid taken unsigned
        const double base = static_cast<double>(static_cast<std::uint32_t>(c.base));
        const double at_mid = static_cast<double>(static_cast<std::uint32_t>(c.at_mid));
        const double at_max = static_cast<double>(static_cast<std::uint32_t>(c.at_max));
        const double span = static_cast<double>(static_cast<std::uint32_t>(
            static_cast<std::uint32_t>(c.at_max) - static_cast<std::uint32_t>(c.at_mid)));
        if (c.mid == 0) {
            res = base + t / 99.0 * at_max * scale;
        } else if (c.mid == 99) {
            res = base + t / 99.0 * at_mid * scale;
        } else if (static_cast<double>(c.mid) > t) {
            res = base + t / c.mid * at_mid * scale;
        } else {
            res = base + (at_mid + (t - c.mid) / (99 - c.mid) * span) * scale;
        }
    } else if (c.kind == CurveKind::f) {
        if (c.mid == 0) {
            res = t / 99.0 * c.at_max * scale + c.base;
        } else if (c.mid == 99) {
            res = t / 99.0 * c.at_mid * scale + c.base;
        } else if (static_cast<double>(c.mid) > t) {
            res = t / c.mid * c.at_mid * scale + c.base;
        } else {
            res = ((t - c.mid) / (99 - c.mid) * (c.at_max - c.at_mid) + c.at_mid) * scale + c.base;
        }
    } else {
        if (c.mid == 0) {
            res = c.base + t / 99.0 * c.at_max * scale;
        } else if (c.mid == 99) {
            res = c.base + t / 99.0 * c.at_mid * scale;
        } else if (static_cast<double>(c.mid) > t) {
            res = c.base + t / c.mid * c.at_mid * scale;
        } else {
            res = c.base + (c.at_mid + (t - c.mid) / (99 - c.mid) * (c.at_max - c.at_mid)) * scale;
        }
    }
    const float out = static_cast<float>(res);
    return out < 0.0f ? 0.0f : out > 100000000.0f ? 100000000.0f : out;
}

bool load_part_tables(PartTables& t, UiData& data, std::string& error) {
    const std::vector<std::uint16_t> sjis = font_sjis(data);
    const auto each = [&](const char* file, const char* head, auto&& fn) {
        gsexp::ParseResult parsed;
        if (!ui_sexp(data, std::string("tables/") + file, parsed, error)) {
            return false;
        }
        for (std::size_t i = 0; i < parsed.root_count(); ++i) {
            const gsexp::FormView form(parsed.root(i));
            if (form.head().is_atom(head)) {
                fn(form, form.get_int("id").value_or(-1));
            }
        }
        return true;
    };
    using F = CurveKind;
    bool ok = each("bodies.sexp", "body", [&](const gsexp::FormView& f, int id) {
        BodyPart* p = row_at(t.bodies, id);
        if (!p) {
            return;
        }
        p->name = read_name(data, sjis, f);
        p->body_class = atom_int(f.find_arg("body_class", 0));
        p->durability = read_curve(f, "durability", F::u);
        p->weight = read_curve(f, "weight", F::f);
        p->sockets = read_curve(f, "sockets", F::s);
        p->price = read_curve(f, "price", F::u);
    });
    ok = ok && each("legs.sexp", "leg", [&](const gsexp::FormView& f, int id) {
        LegPart* p = row_at(t.legs, id);
        if (!p) {
            return;
        }
        p->name = read_name(data, sjis, f);
        p->maker = atom_int(f.find_arg("maker", 0));
        p->geometry = atom_int(f.find_arg("geometry", 0));
        p->durability = read_curve(f, "durability", F::u);
        p->speed = read_curve(f, "speed", F::f);
        p->jump = read_curve(f, "jump", F::f);
        p->weight = read_curve(f, "weight", F::f);
        p->capacity = read_curve(f, "capacity", F::f);
        p->price = read_curve(f, "price", F::u);
    });
    for (int kind = 2; kind <= 3 && ok; ++kind) {
        std::vector<WeaponPart>& rows = kind == 2 ? t.arms : t.backs;
        ok = each(kind == 2 ? "arm_weapons.sexp" : "back_weapons.sexp", "weapon",
                  [&](const gsexp::FormView& f, int id) {
                      WeaponPart* p = row_at(rows, id);
                      if (!p) {
                          return;
                      }
                      p->name = read_name(data, sjis, f);
                      p->maker = atom_int(f.find_arg("maker", 0));
                      p->geometry = f.get_int("geometry").value_or(-1);
                      p->weight = read_curve(f, "weight", F::f);
                      p->shots = read_curve(f, "shots", F::s);
                      p->damage = read_curve(f, "damage", F::s);
                      p->resource = read_curve(f, "resource", F::s);
                      p->recharge = read_curve(f, "recharge_frames", F::s);
                      p->price = read_curve(f, "price", F::u);
                      p->shots_min = f.get_int("shots_min").value_or(0);
                      p->damage_min = f.get_int("damage_min").value_or(0);
                      p->resource_min = f.get_int("resource_min").value_or(0);
                  });
    }
    ok = ok && each("chips.sexp", "chip", [&](const gsexp::FormView& f, int id) {
        ChipPart* p = row_at(t.chips, id);
        if (!p) {
            return;
        }
        p->name = read_name(data, sjis, f);
        p->style = f.get_int("style").value_or(7);
        p->identity = f.get_int("family").value_or(0);
        p->reticle_kind_a = f.get_int("lock_kind_a").value_or(0);
        p->reticle_kind_b = f.get_int("lock_kind_b").value_or(0);
        p->weight = read_curve(f, "weight", F::f);
        p->reticle_size_a = read_curve(f, "lock_range_a", F::s);
        p->reticle_size_b = read_curve(f, "lock_range_b", F::s);
        p->crit = read_curve(f, "crit_chance", F::f);
        p->shield = read_curve(f, "shield_chance", F::f);
        p->price = read_curve(f, "price", F::u);
    });
    ok = ok && each("options.sexp", "option", [&](const gsexp::FormView& f, int id) {
        OptionPart* p = row_at(t.options, id);
        if (!p) {
            return;
        }
        p->name = read_name(data, sjis, f);
        p->style = f.get_int("style").value_or(7);
        p->durability = f.get_int("durability").value_or(0);
        p->weight = f.get_float("weight").value_or(0);
        p->sockets = f.get_int("sockets").value_or(0) & 0xff;
        p->laser_reduction = f.get_int("laser_reduction").value_or(0) & 0xff;
        p->camo_frames = f.get_int("camo_frames").value_or(0);
        p->camo_recharge = f.get_int("camo_recharge").value_or(0);
        p->price = f.get_int("price").value_or(0);
    });
    for (int kind = 6; kind <= 7 && ok; ++kind) {
        std::vector<PaintPart>& rows = kind == 6 ? t.colors : t.stickers;
        ok = each(kind == 6 ? "colors.sexp" : "stickers.sexp", kind == 6 ? "color" : "sticker",
                  [&](const gsexp::FormView& f, int id) {
                      PaintPart* p = row_at(rows, id);
                      if (!p) {
                          return;
                      }
                      p->name = read_name(data, sjis, f);
                      p->selector = f.get_int("selector").value_or(id);
                      p->price = f.get_int("price").value_or(0);
                  });
    }
    ok = ok && each("identities.sexp", "identity", [&](const gsexp::FormView& f, int id) {
        int* sprite = row_at(t.identity_sprites, id);
        if (sprite) {
            *sprite = f.get_int("sprite").value_or(-1);
        }
    });
    ok = ok && each("shop_stock.sexp", "parts", [&](const gsexp::FormView& f, int id) {
        if (auto* row = row_at(t.parts_stock, id)) {
            *row = read_items(f);
        }
    });
    ok = ok && each("shop_stock.sexp", "chip", [&](const gsexp::FormView& f, int id) {
        if (auto* row = row_at(t.chip_stock, id)) {
            *row = read_items(f);
        }
    });
    ok = ok && each("shop_stock.sexp", "scoobee", [&](const gsexp::FormView& f, int id) {
        if (auto* row = row_at(t.scoobee_stock, id)) {
            *row = read_items(f);
        }
    });
    ok = ok && each("scoobees.sexp", "scoobee", [&](const gsexp::FormView& f, int id) {
        ShopBuild* b = row_at(t.builds, id);
        if (!b) {
            return;
        }
        b->name = read_name(data, sjis, f);
        read_pair(f, "body", b->body, b->body_tune);
        read_pair(f, "leg", b->leg, b->leg_tune);
        read_pair(f, "chip", b->chip, b->chip_tune);
        const gsexp::FormView weapons(f.find("weapon"));
        const char* mounts[4] = {"arm_a", "arm_b", "back_a", "back_b"};
        for (int m = 0; m < 4; ++m) {
            read_pair(weapons, mounts[m], b->weapon[m], b->weapon_tune[m]);
        }
        b->color = f.get_int("color").value_or(0);
        b->sticker = f.get_int("sticker").value_or(205);
        const gsexp::Node options = f.find("option");
        for (int o = 0; o < 10 && options.valid() && o + 1 < static_cast<int>(options.child_count());
             ++o) {
            b->option[o] = atom_int(options.child_at(static_cast<std::size_t>(o) + 1));
        }
    });
    t.loaded = ok;
    return ok;
}

const BodyPart* body_part(const PartTables& t, int id) {
    return lookup(t.bodies, id);
}
const LegPart* leg_part(const PartTables& t, int id) {
    return lookup(t.legs, id);
}
const WeaponPart* weapon_part(const PartTables& t, int kind, int id) {
    return lookup(kind == 2 ? t.arms : t.backs, id);
}
const ChipPart* chip_part(const PartTables& t, int id) {
    return lookup(t.chips, id);
}
const OptionPart* option_part(const PartTables& t, int id) {
    return lookup(t.options, id);
}
const PaintPart* paint_part(const PartTables& t, int kind, int id) {
    return lookup(kind == 6 ? t.colors : t.stickers, id);
}

// decomp: part_name 0x930FA
const PartName* part_name(const PartTables& t, int kind, int id) {
    switch (kind) {
    case 0:
        return body_part(t, id) ? &body_part(t, id)->name : nullptr;
    case 1:
        return leg_part(t, id) ? &leg_part(t, id)->name : nullptr;
    case 2:
    case 3:
        return weapon_part(t, kind, id) ? &weapon_part(t, kind, id)->name : nullptr;
    case 4:
        return chip_part(t, id) ? &chip_part(t, id)->name : nullptr;
    case 5:
        return option_part(t, id) ? &option_part(t, id)->name : nullptr;
    case 6:
    case 7:
        return paint_part(t, kind, id) ? &paint_part(t, kind, id)->name : nullptr;
    default:
        return nullptr;
    }
}

int identity_sprite(const PartTables& t, int identity) {
    return identity >= 0 && identity < static_cast<int>(t.identity_sprites.size())
               ? t.identity_sprites[static_cast<std::size_t>(identity)]
               : -1;
}
