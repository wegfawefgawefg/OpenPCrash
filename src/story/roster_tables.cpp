#include "story/roster_tables.hpp"

#include "cook/sexp_read.hpp"

#include <cmath>
#include <cstring>

namespace {

// the n-th child after a field's head: (pools 152 153 ...) -> child n + 1
gsexp::Node field_at(const gsexp::FormView& form, const char* head, int n = 0) {
    const gsexp::Node field = form.find(head);
    return field.valid() ? field.child_at(static_cast<std::size_t>(n) + 1) : gsexp::Node{};
}
int field_int(const gsexp::FormView& form, const char* head, int n = 0) {
    return atom_int(field_at(form, head, n));
}
std::string field_text(const gsexp::FormView& form, const char* head) {
    const gsexp::Node node = field_at(form, head);
    if (!node.valid() || node.is_atom("nil")) {
        return {};
    }
    return std::string(node.text());
}

// (name (curve base at_mid at_max mid negative))
CurveRow read_curve(const gsexp::FormView& form, const char* head) {
    CurveRow curve;
    const gsexp::Node field = form.find(head);
    if (!field.valid()) {
        return curve;
    }
    const gsexp::Node c = field.child_at(1);
    if (!c.valid() || !c.is_list()) {
        // a flat value (options)
        curve.base = atom_float(c);
        curve.present = c.valid();
        return curve;
    }
    curve.base = atom_float(c.child_at(1));
    curve.at_mid = atom_float(c.child_at(2));
    curve.at_max = atom_float(c.child_at(3));
    curve.mid = atom_int(c.child_at(4));
    curve.negative = atom_int(c.child_at(5));
    curve.present = true;
    return curve;
}

template <class Fn>
bool read_rows(const std::string& path, const char* head, std::size_t count_hint, Fn fn,
               std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(path, parsed, error)) {
        return false;
    }
    (void)count_hint;
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom(head)) {
            continue;
        }
        const int id = form.get_int("id").value_or(-1);
        if (id >= -1) {
            fn(id, form);
        }
    }
    return true;
}

template <class T> void put(std::vector<T>& rows, int id, const T& row) {
    if (id < 0) {
        return;
    }
    if (static_cast<std::size_t>(id) >= rows.size()) {
        rows.resize(static_cast<std::size_t>(id) + 1);
    }
    rows[static_cast<std::size_t>(id)] = row;
}

bool load_parts(RosterTables& t, const std::string& dir, std::string& error) {
    const auto part = [](const gsexp::FormView& form, const char* class_field) {
        PartRow row;
        row.part_class = class_field ? field_int(form, class_field) : -1;
        row.weight = read_curve(form, "weight");
        row.price = read_curve(form, "price");
        row.durability = read_curve(form, "durability");
        row.capacity = read_curve(form, "capacity");
        return row;
    };
    return read_rows(dir + "/bodies.sexp", "body", 16,
                     [&](int id, const gsexp::FormView& f) { put(t.bodies, id, part(f, "body_class")); },
                     error) &&
           read_rows(dir + "/legs.sexp", "leg", 43,
                     [&](int id, const gsexp::FormView& f) { put(t.legs, id, part(f, "geometry")); },
                     error) &&
           read_rows(dir + "/arm_weapons.sexp", "weapon", 76,
                     [&](int id, const gsexp::FormView& f) { put(t.arms, id, part(f, "geometry")); },
                     error) &&
           read_rows(dir + "/back_weapons.sexp", "weapon", 0,
                     [&](int id, const gsexp::FormView& f) { put(t.backs, id, part(f, "geometry")); },
                     error) &&
           read_rows(dir + "/chips.sexp", "chip", 96,
                     [&](int id, const gsexp::FormView& f) { put(t.chips, id, part(f, nullptr)); },
                     error) &&
           read_rows(dir + "/options.sexp", "option", 107,
                     [&](int id, const gsexp::FormView& f) {
                         PartRow row;
                         row.flat_weight = field_int(f, "weight");
                         row.flat_price = field_int(f, "price");
                         row.camo_frames = field_int(f, "camo_frames");
                         put(t.options, id, row);
                     },
                     error);
}

RosterBuild read_build(const gsexp::FormView& form) {
    RosterBuild b;
    b.unused_00 = static_cast<std::int16_t>(field_int(form, "unused_00"));
    b.body = static_cast<std::int16_t>(field_int(form, "body", 0));
    b.body_tune = static_cast<std::int8_t>(field_int(form, "body", 1));
    b.leg = static_cast<std::int16_t>(field_int(form, "leg", 0));
    b.leg_tune = static_cast<std::int8_t>(field_int(form, "leg", 1));
    b.chip = static_cast<std::int16_t>(field_int(form, "chip", 0));
    b.chip_tune = static_cast<std::int8_t>(field_int(form, "chip", 1));
    b.color = static_cast<std::int16_t>(field_int(form, "color"));
    b.sticker = static_cast<std::int16_t>(field_int(form, "sticker"));
    const gsexp::Node weapons = form.find("weapon");
    for (int w = 0; w < 4; ++w) {
        const gsexp::Node mount = weapons.valid() ? weapons.child_at(static_cast<std::size_t>(w) + 1)
                                                  : gsexp::Node{};
        b.weapon[w].id = static_cast<std::int16_t>(mount.valid() ? atom_int(mount.child_at(1)) : -1);
        b.weapon[w].tune = static_cast<std::int8_t>(mount.valid() ? atom_int(mount.child_at(2)) : 0);
    }
    for (int k = 0; k < 10; ++k) {
        b.option[k] = static_cast<std::int16_t>(field_int(form, "option", k));
    }
    return b;
}

RosterPilot read_pilot(const gsexp::FormView& form) {
    RosterPilot p;
    p.identity = static_cast<std::int16_t>(field_int(form, "identity"));
    p.scoobee_identity = static_cast<std::int16_t>(field_int(form, "scoobee_identity"));
    p.team = static_cast<std::int16_t>(field_int(form, "team"));
    p.unused_06 = static_cast<std::int16_t>(field_int(form, "unused_06"));
    const gsexp::Node curve = field_at(form, "destruction_value");
    p.value_base = atom_int(curve.child_at(1));
    p.value_at_mid = atom_int(curve.child_at(2));
    p.value_at_max = atom_int(curve.child_at(3));
    p.value_mid = static_cast<std::int8_t>(atom_int(curve.child_at(4)));
    p.value_negative = static_cast<std::int8_t>(atom_int(curve.child_at(5)));
    p.reaction = static_cast<std::int16_t>(field_int(form, "reaction"));
    p.unused_1a = static_cast<std::int16_t>(field_int(form, "unused_1a"));
    return p;
}

const PartRow* part_at(const std::vector<PartRow>& rows, int id) {
    if (id < 0 || rows.empty()) {
        return nullptr;
    }
    return &rows[static_cast<std::size_t>(id) < rows.size() ? static_cast<std::size_t>(id) : 0];
}

template <class T> const T& row_or_first(const std::vector<T>& rows, int id) {
    static const T none{};
    if (rows.empty()) {
        return none;
    }
    return rows[id >= 0 && static_cast<std::size_t>(id) < rows.size() ? static_cast<std::size_t>(id)
                                                                       : 0];
}

} // namespace

// decomp: float_curve_value 0x9247D
float curve_float(const CurveRow& c, int tune) {
    const float a = std::fabs(static_cast<float>(tune));
    const float scale = tune < 0 ? static_cast<float>(c.negative) : 1.0f;
    float v;
    if (c.mid == 0) {
        v = a / 99.0f * c.at_max * scale + c.base;
    } else if (c.mid == 99) {
        v = a / 99.0f * c.at_mid * scale + c.base;
    } else if (static_cast<float>(c.mid) > a) {
        v = a / static_cast<float>(c.mid) * c.at_mid * scale + c.base;
    } else {
        v = (c.at_mid + (a - static_cast<float>(c.mid)) / static_cast<float>(99 - c.mid) *
                            (c.at_max - c.at_mid)) *
                scale +
            c.base;
    }
    return v;
}

// decomp: rumble_curve_value 0x92291
int curve_unsigned(const CurveRow& c, int tune) {
    const auto u = [](float x) { return static_cast<float>(static_cast<unsigned>(x)); };
    const float a = std::fabs(static_cast<float>(static_cast<std::int8_t>(tune)));
    const float scale = tune < 0 ? static_cast<float>(c.negative) : 1.0f;
    float v;
    if (c.mid == 0) {
        v = u(c.base) + a / 99.0f * u(c.at_max) * scale;
    } else if (c.mid == 99) {
        v = u(c.base) + a / 99.0f * u(c.at_mid) * scale;
    } else if (static_cast<float>(c.mid) > a) {
        v = u(c.base) + a / static_cast<float>(c.mid) * u(c.at_mid) * scale;
    } else {
        const unsigned span = static_cast<unsigned>(c.at_max) - static_cast<unsigned>(c.at_mid);
        v = u(c.base) + (u(c.at_mid) + (a - static_cast<float>(c.mid)) /
                                           static_cast<float>(99 - c.mid) *
                                           static_cast<float>(span)) *
                            scale;
    }
    if (v < 0.0f) {
        return 0;
    }
    if (v > 100000000.0f) {
        v = 100000000.0f;
    }
    return static_cast<int>(v);
}

bool load_roster_tables(RosterTables& t, const std::string& data_dir, std::string& error) {
    const std::string dir = data_dir + "/tables";
    const bool ok =
        read_rows(dir + "/opponents.sexp", "opponent", 1187,
                  [&](int id, const gsexp::FormView& f) {
                      OpponentEntry e;
                      e.pilot = field_int(f, "pilot");
                      e.loadout = field_int(f, "loadout");
                      e.ai_profile = field_int(f, "ai_profile");
                      e.level = field_int(f, "level");
                      put(t.opponents, id, e);
                  },
                  error) &&
        read_rows(dir + "/pilots.sexp", "pilot", 1060,
                  [&](int id, const gsexp::FormView& f) { put(t.pilots, id, read_pilot(f)); },
                  error) &&
        read_rows(dir + "/scoobees.sexp", "scoobee", 1190,
                  [&](int id, const gsexp::FormView& f) {
                      put(t.builds, id, read_build(f));
                      put(t.build_names, id, field_text(f, "name"));
                  },
                  error) &&
        read_rows(dir + "/identities.sexp", "identity", 990,
                  [&](int id, const gsexp::FormView& f) {
                      std::string name = field_text(f, "name_en");
                      if (name.empty()) {
                          name = field_text(f, "name");
                      }
                      put(t.identity_names, id, name);
                      put(t.identity_sprites, id, field_int(f, "sprite"));
                  },
                  error) &&
        read_rows(dir + "/teams.sexp", "team", 84,
                  [&](int id, const gsexp::FormView& f) {
                      put(t.team_names, id + 1, field_text(f, "name"));
                  },
                  error) &&
        read_rows(dir + "/tournaments.sexp", "tournament", 56,
                  [&](int id, const gsexp::FormView& f) {
                      put(t.tournament_names, id, field_text(f, "name"));
                  },
                  error) &&
        read_rows(dir + "/tournament_rules.sexp", "rule", 28,
                  [&](int id, const gsexp::FormView& f) {
                      put(t.rule_names, id, field_text(f, "name"));
                      put(t.rule_subtitles, id, field_text(f, "subtitle"));
                  },
                  error) &&
        load_parts(t, dir, error);
    if (ok && (t.opponents.empty() || t.builds.empty() || t.pilots.empty())) {
        error = "roster tables empty (pc_extract --only tables)";
        return false;
    }
    return ok;
}

const OpponentEntry& opponent_entry(const RosterTables& t, int id) {
    return row_or_first(t.opponents, id);
}
const RosterBuild& roster_build_row(const RosterTables& t, int id) {
    return row_or_first(t.builds, id);
}
const RosterPilot& pilot_row(const RosterTables& t, int id) {
    return row_or_first(t.pilots, id);
}
const PartRow* body_row(const RosterTables& t, int id) {
    return part_at(t.bodies, id);
}
const PartRow* leg_row(const RosterTables& t, int id) {
    return part_at(t.legs, id);
}
const PartRow* weapon_row(const RosterTables& t, int mount, int id) {
    return part_at(mount < 2 ? t.arms : t.backs, id);
}
const PartRow* option_row(const RosterTables& t, int id) {
    return part_at(t.options, id);
}

// decomp: Scoobee::body_class 0x5F6D0
int build_body_class(const RosterTables& t, const RosterBuild& b) {
    const PartRow* body = body_row(t, b.body);
    return body ? body->part_class : -1;
}
int build_leg_class(const RosterTables& t, const RosterBuild& b) {
    const PartRow* leg = leg_row(t, b.leg);
    return leg ? leg->part_class : -1;
}
int build_body_class_or_7(const RosterTables& t, const RosterBuild& b) {
    const PartRow* body = body_row(t, b.body);
    return body ? body->part_class : 7;
}
int build_leg_kind_or_8(const RosterTables& t, const RosterBuild& b) {
    const PartRow* leg = leg_row(t, b.leg);
    return leg ? leg_kind_group_of(leg->part_class) : 8;
}

// decomp: leg_kind_group 0x8234B
int leg_kind_group_of(int c) {
    switch (c) {
    case 0: case 5: case 0xb:
        return 0;
    case 1: case 6: case 0xc: case 0x11:
        return 1;
    case 7:
        return 2;
    case 2: case 0xd:
        return 3;
    case 8: case 0x14:
        return 4;
    case 3: case 0xe:
        return 5;
    case 9: case 0xf:
        return 6;
    case 4: case 0xa: case 0x10: case 0x12: case 0x13:
        return 7;
    default:
        return 0;
    }
}

// decomp: chassis_loadout_weight 0x75F13
float build_weight_float(const RosterTables& t, const RosterBuild& b) {
    float weight = 0.0f;
    if (const PartRow* body = body_row(t, b.body)) {
        weight += curve_float(body->weight, b.body_tune);
    }
    if (const PartRow* leg = leg_row(t, b.leg)) {
        weight += curve_float(leg->weight, b.leg_tune);
    }
    if (const PartRow* chip = part_at(t.chips, b.chip)) {
        weight += curve_float(chip->weight, b.chip_tune);
    }
    for (int w = 0; w < 4; ++w) {
        if (const PartRow* weapon = weapon_row(t, w, b.weapon[w].id)) {
            weight += curve_float(weapon->weight, b.weapon[w].tune);
        }
    }
    for (const std::int16_t option : b.option) {
        if (const PartRow* row = option_row(t, option)) {
            weight += static_cast<float>(row->flat_weight);
        }
    }
    return weight;
}

int build_camo_frames(const RosterTables& t, const RosterBuild& b) {
    int frames = 0;
    for (const std::int16_t option : b.option) {
        if (const PartRow* row = option != -1 ? option_row(t, option) : nullptr) {
            frames += row->camo_frames;
        }
    }
    return frames;
}

// decomp: shop_item_price 0x93247
int part_shop_price(const RosterTables& t, int kind, int item, int tune) {
    const PartRow* row = nullptr;
    switch (kind) {
    case 0:
        row = body_row(t, item);
        break;
    case 1:
        row = leg_row(t, item);
        break;
    case 2:
        row = part_at(t.arms, item);
        break;
    case 3:
        row = part_at(t.backs, item);
        break;
    case 4:
        row = part_at(t.chips, item);
        break;
    case 5:
        row = option_row(t, item);
        return row ? row->flat_price : 0;
    default:
        return 0;
    }
    return row ? curve_unsigned(row->price, tune) : 0;
}
