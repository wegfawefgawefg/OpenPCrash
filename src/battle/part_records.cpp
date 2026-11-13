#include "battle/part_records.hpp"

#include "assets/pack_sexp.hpp"
#include "battle/sexp_fx.hpp"

namespace {

StatCurve read_curve(gsexp::Node form) {
    // (name (curve base at_mid at_max mid negative))
    StatCurve curve;
    const gsexp::Node node = form.child_at(1);
    if (!node.valid() || !node.is_list() || !node.head().is_atom("curve")) {
        return curve;
    }
    curve.base = atom_fx(node.child_at(1));
    curve.at_mid = atom_fx(node.child_at(2));
    curve.at_max = atom_fx(node.child_at(3));
    curve.mid = atom_int(node.child_at(4));
    curve.negative = atom_int(node.child_at(5));
    return curve;
}

// the field under its decomp name, or the name an older extract wrote it under
gsexp::Node field(const gsexp::FormView& form, const char* name, const char* old_name = nullptr) {
    const gsexp::Node node = form.find(name);
    return node.valid() || !old_name ? node : form.find(old_name);
}

// every root form of a file with the given head, in file order (rows are in id order)
template <typename Read>
bool read_rows(const Pack& core, const std::string& name, const char* head, std::string& error,
               Read read) {
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, "tables/" + name, parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (form.head().is_atom(head)) {
            read(form);
        }
    }
    return true;
}

// sv_*_record: -1 none, a row out of range reads row 0 (docs/port/parts.md)
template <typename T> const T* record_at(const std::vector<T>& rows, int row) {
    if (row == -1 || rows.empty()) {
        return nullptr;
    }
    return row < 0 || row >= static_cast<int>(rows.size()) ? &rows[0] : &rows[row];
}

} // namespace

// decomp: Curve::value_at 0x920E0, float_curve_value 0x9247D
Fx curve_value(const StatCurve& curve, int tune) {
    const int a = tune < 0 ? -tune : tune;
    const int s = tune < 0 ? curve.negative : 1;
    // the offset at |tune|, one rounding each
    std::int64_t offset = 0;
    if (curve.mid == 0) {
        offset = div_round(static_cast<std::int64_t>(curve.at_max.raw) * a, 99);
    } else if (curve.mid == 99) {
        offset = div_round(static_cast<std::int64_t>(curve.at_mid.raw) * a, 99);
    } else if (a < curve.mid) {
        offset = div_round(static_cast<std::int64_t>(curve.at_mid.raw) * a, curve.mid);
    } else {
        offset = curve.at_mid.raw +
                 div_round(static_cast<std::int64_t>((curve.at_max - curve.at_mid).raw) *
                               (a - curve.mid),
                           99 - curve.mid);
    }
    const Fx value = fx_saturate(curve.base.raw + offset * s);
    // clamped to 0..1e8; nothing the world reads comes near the top
    return value.raw < 0 ? Fx{} : value;
}

bool load_part_records(PartRecords& records, const Pack& core, std::string& error) {
    const auto weight_row = [](std::vector<WeightRecord>& rows) {
        return [&rows](const gsexp::FormView& form) {
            rows.push_back({read_curve(form.find("weight"))});
        };
    };
    if (!read_rows(core, "bodies.sexp", "body", error,
                   [&](const gsexp::FormView& form) {
                       BodyRecord body;
                       body.body_class = atom_int(form.find("body_class").child_at(1));
                       body.weight = read_curve(form.find("weight"));
                       records.bodies.push_back(body);
                   }) ||
        !read_rows(core, "legs.sexp", "leg", error,
                   [&](const gsexp::FormView& form) {
                       // turn_deg, dodge_ground and dodge_air were written as response_a, _b
                       // and _c by the first tables extract (LegRecord +0x3C, +0x9C, +0xAC)
                       LegRecord leg;
                       leg.geometry = atom_int(form.find("geometry").child_at(1));
                       leg.speed = read_curve(form.find("speed"));
                       leg.jump = read_curve(form.find("jump"));
                       leg.turn_deg = read_curve(field(form, "turn_deg", "response_a"));
                       leg.weight = read_curve(form.find("weight"));
                       leg.capacity = read_curve(form.find("capacity"));
                       leg.landing = read_curve(form.find("landing"));
                       leg.knockback_resist = read_curve(form.find("knockback_resist"));
                       leg.dodge_ground = read_curve(field(form, "dodge_ground", "response_b"));
                       leg.dodge_air = read_curve(field(form, "dodge_air", "response_c"));
                       leg.slide_decay = atom_fx(form.find("slide_decay").child_at(1));
                       records.legs.push_back(leg);
                   }) ||
        !read_rows(core, "arm_weapons.sexp", "weapon", error, weight_row(records.arms)) ||
        !read_rows(core, "back_weapons.sexp", "weapon", error, weight_row(records.backs)) ||
        !read_rows(core, "chips.sexp", "chip", error, weight_row(records.chips)) ||
        !read_rows(core, "options.sexp", "option", error,
                   [&](const gsexp::FormView& form) {
                       records.options.push_back({atom_fx(form.find("weight").child_at(1))});
                   }) ||
        !read_rows(core, "scoobees.sexp", "scoobee", error, [&](const gsexp::FormView& form) {
            BuildRecord build;
            const auto part = [&](const char* name, int& row, int& tune) {
                const gsexp::Node node = form.find(name);
                row = node.valid() ? atom_int(node.child_at(1)) : -1;
                tune = atom_int(node.child_at(2));
            };
            part("body", build.body, build.body_tune);
            part("leg", build.leg, build.leg_tune);
            part("chip", build.chip, build.chip_tune);
            const gsexp::FormView weapons(form.find("weapon"));
            const char* mounts[4] = {"arm_a", "arm_b", "back_a", "back_b"};
            for (int w = 0; w < 4; ++w) {
                const gsexp::Node node = weapons.find(mounts[w]);
                build.weapon[w] = node.valid() ? atom_int(node.child_at(1)) : -1;
                build.weapon_tune[w] = atom_int(node.child_at(2));
            }
            build.colour = atom_int(form.find("color").child_at(1));
            build.sticker = atom_int(form.find("sticker").child_at(1));
            const gsexp::Node options = form.find("option");
            for (int o = 0; o < 10; ++o) {
                build.option[o] = options.valid() ? atom_int(options.child_at(1 + o)) : -1;
            }
            records.builds.push_back(build);
        })) {
        return false;
    }
    if (records.bodies.empty() || records.legs.empty() || records.builds.empty()) {
        error = "part tables empty in the core pack";
        return false;
    }
    return true;
}

// decomp: chassis_loadout_weight 0x75F13
Fx build_weight(const PartRecords& records, const BuildRecord& build) {
    Fx weight;
    if (const BodyRecord* body = record_at(records.bodies, build.body)) {
        weight += curve_value(body->weight, build.body_tune);
    }
    if (const LegRecord* leg = record_at(records.legs, build.leg)) {
        weight += curve_value(leg->weight, build.leg_tune);
    }
    if (const WeightRecord* chip = record_at(records.chips, build.chip)) {
        weight += curve_value(chip->weight, build.chip_tune);
    }
    for (int w = 0; w < 4; ++w) {
        const std::vector<WeightRecord>& table = w < 2 ? records.arms : records.backs;
        if (const WeightRecord* weapon = record_at(table, build.weapon[w])) {
            weight += curve_value(weapon->weight, build.weapon_tune[w]);
        }
    }
    for (int option : build.option) {
        if (const OptionRecord* record = record_at(records.options, option)) {
            weight += record->weight;
        }
    }
    return weight;
}

// decomp: roster_base_parts_weight 0x7C05F
Fx build_base_weight(const PartRecords& records, const BuildRecord& build) {
    const BodyRecord* body = record_at(records.bodies, build.body);
    const LegRecord* leg = record_at(records.legs, build.leg);
    if (!body || !leg) {
        return Fx{};
    }
    return curve_value(body->weight, 0) + curve_value(leg->weight, 0);
}
