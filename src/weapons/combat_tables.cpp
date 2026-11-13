#include "weapons/combat_tables.hpp"

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

int int_field(const gsexp::FormView& form, const char* name) {
    return atom_int(form.find(name).child_at(1));
}
Fx fx_field(const gsexp::FormView& form, const char* name) {
    return atom_fx(form.find(name).child_at(1));
}

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

WeaponRecord read_weapon(const gsexp::FormView& form) {
    WeaponRecord w;
    w.geometry = int_field(form, "geometry");
    w.weight = read_curve(form.find("weight"));
    w.behavior = int_field(form, "behavior");
    w.delivery = int_field(form, "delivery");
    w.lock_rule = int_field(form, "lock_rule");
    w.impact = int_field(form, "impact");
    w.impact_alt = int_field(form, "impact_alt");
    w.impact_fx = int_field(form, "impact_fx");
    w.muzzle_fx = int_field(form, "muzzle_fx");
    w.shot_model = int_field(form, "shot_model");
    w.elevation_deg = int_field(form, "elevation_deg");
    w.reload_frames = int_field(form, "refire_frames");
    w.shot_life = int_field(form, "shot_life");
    w.speed = fx_field(form, "shot_speed");
    w.speed_max = fx_field(form, "beam_length");
    w.accel = fx_field(form, "beam_rate");
    w.pellet_spread_deg = int_field(form, "spread_deg");
    w.turn_min_deg = fx_field(form, "turn_min_deg");
    w.turn_deg = read_curve(form.find("turn_deg"));
    w.shots_min = int_field(form, "shots_min");
    w.shots = read_curve(form.find("shots"));
    w.damage_min = int_field(form, "damage_min");
    w.damage = read_curve(form.find("damage"));
    w.blast_radius_end = read_curve(form.find("blast_radius_end"));
    w.blast_radius_start = int_field(form, "blast_radius_start");
    w.impact_frames = read_curve(form.find("impact_frames"));
    w.impact_damage = read_curve(form.find("impact_damage"));
    w.resource_min = int_field(form, "resource_min");
    w.resource = read_curve(form.find("resource"));
    w.recharge_frames = read_curve(form.find("recharge_frames"));
    w.stagger = read_curve(form.find("stagger"));
    w.knockback = read_curve(form.find("knockback"));
    w.break_chance = read_curve(form.find("break_chance"));
    return w;
}

} // namespace

// decomp: Curve::value_at 0x920E0
int curve_int(const StatCurve& curve, int tune) {
    const int a = tune < 0 ? -tune : tune;
    const int s = tune < 0 ? curve.negative : 1;
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
    std::int64_t value = curve.base.raw + offset * s;
    // clamp(v, 0, 1e8), then the (int) cast truncates
    value = value < 0 ? 0 : value;
    value = value >> fx_shift;
    return static_cast<int>(value > 100000000 ? 100000000 : value);
}

// decomp: mount_weapon_record 0x92762
const WeaponRecord* mount_weapon_record(const CombatTables& tables, int mount, int row) {
    const std::vector<WeaponRecord>& rows = mount < 2 ? tables.arms : tables.backs;
    if (row == -1 || rows.empty()) {
        return nullptr;
    }
    return row < 0 || row >= static_cast<int>(rows.size()) ? &rows[0] : &rows[row];
}

bool load_combat_tables(CombatTables& tables, const Pack& core, std::string& error) {
    if (!read_rows(
            core, "arm_weapons.sexp", "weapon", error,
            [&](const gsexp::FormView& form) { tables.arms.push_back(read_weapon(form)); }) ||
        !read_rows(
            core, "back_weapons.sexp", "weapon", error,
            [&](const gsexp::FormView& form) { tables.backs.push_back(read_weapon(form)); }) ||
        !read_rows(core, "chips.sexp", "chip", error,
                   [&](const gsexp::FormView& form) {
                       // the extract's lock_* names are the decomp's reticle_* and lock_frames
                       ChipRecord chip;
                       chip.reticle_kind_a = int_field(form, "lock_kind_a");
                       chip.reticle_size_a = read_curve(form.find("lock_range_a"));
                       chip.reticle_speed = int_field(form, "lock_rate");
                       chip.reticle_kind_b = int_field(form, "lock_kind_b");
                       chip.reticle_size_b = read_curve(form.find("lock_range_b"));
                       chip.lock_frames = int_field(form, "lock_time");
                       // the extract's "lock_hit" is +0x34 camo_distortion (parts.md)
                       chip.camo_distortion = read_curve(form.find("lock_hit"));
                       chip.crit_chance = read_curve(form.find("crit_chance"));
                       chip.shield_chance = read_curve(form.find("shield_chance"));
                       chip.break_resist = read_curve(form.find("break_resist"));
                       tables.chips.push_back(chip);
                   }) ||
        !read_rows(core, "options.sexp", "option", error,
                   [&](const gsexp::FormView& form) {
                       OptionCombat option;
                       option.durability = int_field(form, "durability");
                       option.crit_chance = fx_field(form, "crit_chance");
                       option.break_resist = fx_field(form, "break_resist");
                       option.laser_reduction = int_field(form, "laser_reduction");
                       option.camo_frames = int_field(form, "camo_frames");
                       option.camo_recharge = int_field(form, "camo_recharge");
                       tables.options.push_back(option);
                   }) ||
        !read_rows(core, "bodies.sexp", "body", error,
                   [&](const gsexp::FormView& form) {
                       tables.body_durability.push_back(read_curve(form.find("durability")));
                   }) ||
        !read_rows(core, "legs.sexp", "leg", error, [&](const gsexp::FormView& form) {
            tables.leg_durability.push_back(read_curve(form.find("durability")));
        })) {
        return false;
    }
    if (tables.arms.empty() || tables.backs.empty()) {
        error = "weapon tables missing in the core pack";
        return false;
    }
    return true;
}
