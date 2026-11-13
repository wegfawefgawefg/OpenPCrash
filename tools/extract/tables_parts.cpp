// Parts (docs/port/data.md "Parts", src/data/parts.c): bodies, legs, chips, arm and back weapons,
// options, colours and stickers. Part ids in saves, shop stock and builds are these row numbers.
#include "addresses.hpp"
#include "sexp_out.hpp"
#include "tables.hpp"

namespace {

const char* const curve_note =
    "a curve is (curve base at_mid at_max mid negative): at_mid and at_max are added to base, at "
    "|tune| mid and 99, linear between (docs/tables.md); a negative tune scales the addition by "
    "negative";
const char* const units_note =
    "weights in hundredths of a ton, prices in units of 10 New Yen, frames at 60 Hz";

// Scoobee::body_class, named by the makers' prefixes in the part names (HLBody, PHBody, ...)
const char* const body_classes[] = {"hl", "ph", "ae", "tl", "tr", "cg", "dr", "object"};

// Scoobee::leg_class (LegRecord.geometry), named after the shop leg of each class
const char* const leg_classes[] = {
    "hl_2leg",    "hl_4leg",    "hl_2roller", "hl_4roller", "hl_hover", "ph_2leg",  "ph_4leg",
    "ph_6leg",    "ph_3roller", "ph_6roller", "ph_hover",   "ae_2leg",  "ae_4leg",  "ae_2roller",
    "ae_4roller", "ae_6roller", "ae_hover",   "tl_4leg",    "tr_hover", "cg_hover", "dr_3roller"};

// WeaponRecord.behavior picks the controller (weapon_update_callbacks 0x2532F0)
const char* behavior_name(int behavior) {
    if (behavior == 5) {
        return "laser";
    }
    if (behavior == 6) {
        return "melee";
    }
    return behavior >= 0 && behavior < 5 ? "fire" : "unknown";
}

// WeaponRecord.delivery picks the shot update (weapon_delivery_callbacks 0x25330C)
const char* delivery_name(int delivery) {
    static const char* const names[] = {"straight", "rotating", "guided"};
    return delivery >= 0 && delivery < 3 ? names[delivery] : "unknown";
}

const char* maker_name(int maker) {
    return maker >= 0 && maker < 8 ? body_classes[maker] : "unknown";
}

bool write_bodies(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"bodies",
                   "sv_body_table (BodyRecord)",
                   xbe_at::body_records,
                   xbe_at::body_record_count,
                   {curve_note, units_note,
                    "body_class: Scoobee::body_class (hl holy, ph photon, ae aeron, tl, tr, cg, dr "
                    "drones); names with 敵 are opponent-only parts"},
                   {}};
    for (int id = 0; id < xbe_at::body_record_count; ++id) {
        const std::uint32_t r = xbe_at::body_records + static_cast<std::uint32_t>(id) * 0x40;
        const int body_class = static_cast<std::int8_t>(xbe_u8(x, r + 4));
        add_row(file,
                {"body",
                 {field("id", id), field("name", text_at(t, xbe_u32(x, r))),
                  field_enum("body_class", body_class, maker_name(body_class)),
                  field("durability", curve_u(x, r + 0x08)), field("weight", curve_f(x, r + 0x18)),
                  field("sockets", curve_s(x, r + 0x28)), field("price", curve_u(x, r + 0x30))}});
    }
    return write_table(t, file);
}

bool write_legs(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{
        "legs",
        "sv_leg_table (LegRecord)",
        xbe_at::leg_records,
        xbe_at::leg_record_count,
        {curve_note, units_note, "maker (+0x04): in every row the maker's body class",
         "geometry: Scoobee::leg_class, selects the leg assets; speed km/h, jump metres",
         "turn_deg (degrees a tick), dodge_ground and dodge_air (units a tick): the rates "
         "chassis_apply_performance scales; landing: Actor.leg_6c, "
         "landing recovery scaled by (100 - it) / 100",
         "slide_decay: Actor.leg_bc, a float (tables.h types it int), scales the skid "
         "each frame"},
        {}};
    for (int id = 0; id < xbe_at::leg_record_count; ++id) {
        const std::uint32_t r = xbe_at::leg_records + static_cast<std::uint32_t>(id) * 0xC0;
        const int maker = xbe_i32(x, r + 4);
        const int geometry = xbe_i32(x, r + 8);
        add_row(
            file,
            {"leg",
             {field("id", id), field("name", text_at(t, xbe_u32(x, r))),
              field_enum("maker", maker, maker_name(maker)),
              field_enum("geometry", geometry,
                         geometry >= 0 && geometry < 21 ? leg_classes[geometry] : "unknown"),
              field("durability", curve_u(x, r + 0x0C)), field("speed", curve_f(x, r + 0x1C)),
              field("jump", curve_f(x, r + 0x2C)), field("turn_deg", curve_f(x, r + 0x3C)),
              field("weight", curve_f(x, r + 0x4C)), field("capacity", curve_f(x, r + 0x5C)),
              field("landing", curve_f(x, r + 0x6C)),
              field("knockback_resist", curve_f(x, r + 0x7C)), field("price", curve_u(x, r + 0x8C)),
              field("dodge_ground", curve_f(x, r + 0x9C)), field("dodge_air", curve_f(x, r + 0xAC)),
              field("slide_decay", fl(xbe_f32(x, r + 0xBC)))}});
    }
    return write_table(t, file);
}

bool write_chips(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"chips",
                   "sv_chip_table (ChipRecord)",
                   xbe_at::chip_records,
                   96,
                   {curve_note, units_note,
                    "style: menu class (archive); bark_profile picks the battle barks "
                    "(rumble_reaction_profiles row); family: with bark_profile the archive's "
                    "packed family and variant",
                    "lock_*: guesses from the Actor fields they fill (chip_lock_kind_a ...); "
                    "lock_rate reads as /999 * 0.08 + 0.02, lock_hit as a percent",
                    "crit_chance, shield_chance (the Repulsion Shield) and break_resist are "
                    "percent chances"},
                   {}};
    for (int id = 0; id < 96; ++id) {
        const std::uint32_t r = xbe_at::chip_records + static_cast<std::uint32_t>(id) * 0x7C;
        add_row(
            file,
            {"chip",
             {field("id", id), field("name", text_at(t, xbe_u32(x, r))),
              field("style", xbe_i32(x, r + 4)), field("weight", curve_f(x, r + 0x08)),
              field("bark_profile", xbe_i16(x, r + 0x18)), field("family", xbe_i16(x, r + 0x1A)),
              field("lock_kind_a", static_cast<std::int8_t>(xbe_u8(x, r + 0x1C))),
              field("lock_range_a", curve_s(x, r + 0x1E)), field("lock_rate", xbe_i16(x, r + 0x26)),
              field("lock_kind_b", static_cast<std::int8_t>(xbe_u8(x, r + 0x28))),
              field("lock_range_b", curve_s(x, r + 0x2A)), field("lock_time", xbe_i16(x, r + 0x32)),
              field("lock_hit", curve_s(x, r + 0x34)), field("crit_chance", curve_f(x, r + 0x3C)),
              field("shield_chance", curve_f(x, r + 0x4C)),
              field("break_resist", curve_f(x, r + 0x5C)), field("price", curve_u(x, r + 0x6C))}});
    }
    return write_table(t, file);
}

bool write_weapons(Tables& t, const char* name, const char* decomp, std::uint32_t va, int rows) {
    const Xbe& x = *t.xbe;
    TableFile file{name,
                   decomp,
                   va,
                   rows,
                   {curve_note, units_note,
                    "a fitted record is copied to Weapon +0x20; stat+N in tables.h is Weapon "
                    "+0x20+N. maker (+0x04): in every row the maker's body class",
                    "geometry: Scoobee::weapon_class, selects the weapon assets (-1 none). "
                    "behavior: the controller (0..4 fire, 5 laser, 6 melee), 1 takes a lock",
                    "delivery: shot update (straight, rotating, guided); lock_rule: AiWeapon; "
                    "impact/impact_alt: impact profile (0x98E5A)",
                    "muzzle_fx: 0, 1 flash models 0x39E/0x39F, 2 model 0x3F3, 3 and 4 a sound "
                    "only, 5 the laser's, 6 none; shot_model: row of the shot look table 0x2533C0, "
                    "255 none",
                    "impact_fx: row of impact_fx.sexp. *_deg fields hold degrees the game turns "
                    "to radians; shot_speed, beam_length and beam_rate are floats (tables.h types "
                    "them int)",
                    "elevation_deg (+0x24): stat+0x04; blast_radius_end (+0x64): stat+0x2C; "
                    "blast_radius_start (+0x6C): stat+0x30; "
                    "*_min: lower limit of the curve's value"},
                   {}};
    for (int id = 0; id < rows; ++id) {
        const std::uint32_t r = va + static_cast<std::uint32_t>(id) * 0xC8;
        const int maker = xbe_i32(x, r + 4);
        const int behavior = xbe_u8(x, r + 0x1C);
        const int delivery = xbe_u8(x, r + 0x1D);
        add_row(file, {"weapon",
                       {field("id", id),
                        field("name", text_at(t, xbe_u32(x, r))),
                        field_enum("maker", maker, maker_name(maker)),
                        field("geometry", xbe_i32(x, r + 8)),
                        field("weight", curve_f(x, r + 0x0C)),
                        field_enum("behavior", behavior, behavior_name(behavior)),
                        field_enum("delivery", delivery, delivery_name(delivery)),
                        field("lock_rule", xbe_u8(x, r + 0x1E)),
                        field("impact", xbe_u8(x, r + 0x1F)),
                        field("impact_alt", xbe_u8(x, r + 0x20)),
                        field("muzzle_fx", xbe_u8(x, r + 0x21)),
                        field("shot_model", xbe_u8(x, r + 0x22)),
                        field("impact_fx", xbe_u8(x, r + 0x23)),
                        field("elevation_deg", xbe_i16(x, r + 0x24)),
                        field("refire_frames", static_cast<std::uint16_t>(xbe_i16(x, r + 0x26))),
                        field("shot_life", static_cast<std::uint16_t>(xbe_i16(x, r + 0x28))),
                        field("shot_speed", fl(xbe_f32(x, r + 0x2C))),
                        field("beam_length", fl(xbe_f32(x, r + 0x30))),
                        field("beam_rate", fl(xbe_f32(x, r + 0x34))),
                        field("spread_deg", xbe_u8(x, r + 0x38)),
                        field("turn_min_deg", fl(xbe_f32(x, r + 0x3C))),
                        field("turn_deg", curve_f(x, r + 0x40)),
                        field("shots_min", xbe_i16(x, r + 0x50)),
                        field("shots", curve_s(x, r + 0x52)),
                        field("damage_min", xbe_i16(x, r + 0x5A)),
                        field("damage", curve_s(x, r + 0x5C)),
                        field("blast_radius_end", curve_s(x, r + 0x64)),
                        field("blast_radius_start", xbe_i16(x, r + 0x6C)),
                        field("impact_frames", curve_s(x, r + 0x6E)),
                        field("impact_damage", curve_s(x, r + 0x76)),
                        field("resource_min", xbe_i16(x, r + 0x7E)),
                        field("resource", curve_s(x, r + 0x80)),
                        field("recharge_frames", curve_s(x, r + 0x88)),
                        field("stagger", curve_s(x, r + 0x90)),
                        field("knockback", curve_f(x, r + 0x98)),
                        field("break_chance", curve_f(x, r + 0xA8)),
                        field("price", curve_u(x, r + 0xB8))}});
    }
    return write_table(t, file);
}

bool write_options(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"options",
                   "sv_option_table (OptionRecord)",
                   xbe_at::option_records,
                   107,
                   {units_note,
                    "options have plain values, no curves. style: menu class (archive). "
                    "crit_chance, break_resist and laser_reduction add to the fighter's "
                    "(damage_options_apply)",
                    "camo_frames: optical camouflage time (rule \"No O.C.\" sums it); "
                    "camo_recharge: frames. unused_1c (+0x1C), unused_21 (+0x21): no reader read"},
                   {}};
    for (int id = 0; id < 107; ++id) {
        const std::uint32_t r = xbe_at::option_records + static_cast<std::uint32_t>(id) * 0x2C;
        add_row(
            file,
            {"option",
             {field("id", id), field("name", text_at(t, xbe_u32(x, r))),
              field("style", xbe_i32(x, r + 4)), field("durability", xbe_i32(x, r + 8)),
              field("weight", fl(xbe_f32(x, r + 0x0C))), field("sockets", xbe_i32(x, r + 0x10)),
              field("crit_chance", fl(xbe_f32(x, r + 0x14))),
              field("break_resist", fl(xbe_f32(x, r + 0x18))),
              field("unused_1c", xbe_i32(x, r + 0x1C)),
              field("laser_reduction", xbe_u8(x, r + 0x20)),
              field("unused_21", xbe_u8(x, r + 0x21)), field("camo_frames", xbe_i16(x, r + 0x22)),
              field("camo_recharge", xbe_i16(x, r + 0x24)), field("price", xbe_i32(x, r + 0x28))}});
    }
    return write_table(t, file);
}

bool write_paints(Tables& t, const char* name, const char* head, const char* decomp,
                  std::uint32_t va, int rows) {
    const Xbe& x = *t.xbe;
    TableFile file{
        name, decomp, va, rows, {units_note, "selector equals the row in every record (archive)"},
        {}};
    for (int id = 0; id < rows; ++id) {
        const std::uint32_t r = va + static_cast<std::uint32_t>(id) * 0xC;
        add_row(file, {head,
                       {field("id", id), field("name", text_at(t, xbe_u32(x, r))),
                        field("selector", xbe_i32(x, r + 4)), field("price", xbe_i32(x, r + 8))}});
    }
    return write_table(t, file);
}

} // namespace

bool write_part_tables(Tables& tables) {
    return write_bodies(tables) && write_legs(tables) && write_chips(tables) &&
           write_weapons(tables, "arm_weapons", "sv_arm_table (WeaponRecord)", xbe_at::arm_records,
                         xbe_at::arm_record_count) &&
           write_weapons(tables, "back_weapons", "sv_back_table (WeaponRecord)",
                         xbe_at::back_records, xbe_at::back_record_count) &&
           write_options(tables) &&
           write_paints(tables, "colors", "color", "sv_color_table (PaintRecord)",
                        xbe_at::color_records, 57) &&
           write_paints(tables, "stickers", "sticker", "sv_sticker_table (PaintRecord)",
                        xbe_at::sticker_records, 206);
}
