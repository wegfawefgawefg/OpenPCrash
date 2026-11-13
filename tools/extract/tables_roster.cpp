// Scoobees, opponents, pilots, identities, teams and battle barks (docs/port/data.md "Scoobees,
// opponents, pilots", src/data/roster.c and identities.c).
#include "addresses.hpp"
#include "sexp_out.hpp"
#include "tables.hpp"

namespace {

constexpr int scoobee_count = 1190;
constexpr int opponent_count = 1187;
constexpr int pilot_count = 1060;
constexpr int identity_count = 990;
constexpr int identity_kind_count = 17;
constexpr int team_count = 84;
constexpr int reaction_count = 67;
constexpr int barks_per_profile = 35;

std::string tuned(const std::string& name, int id, int tune) {
    return "(" + name + " " + sx_int(id) + " " + sx_int(tune) + ")";
}

bool write_scoobees(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"scoobees",
                   "rumble_scoobee_loadouts (BuildRecord)",
                   xbe_at::scoobee_loadouts,
                   scoobee_count,
                   {"a full build; every part is a row of its part table, -1 none; tunes -99..99",
                    "(body id tune) (leg id tune) (chip id tune); weapon: (arm_a id tune) (arm_b "
                    "..) (back_a ..) (back_b ..)",
                    "color and sticker: rows of colors.sexp and stickers.sexp; option: ten rows of "
                    "options.sexp. unused_00 (+0x00): no reader read",
                    "dealer and shop Scoobees and every opponent's build are rows here"},
                   {}};
    for (int id = 0; id < scoobee_count; ++id) {
        const std::uint32_t r = xbe_at::scoobee_loadouts + static_cast<std::uint32_t>(id) * 0x3C;
        std::string weapons = "(weapon";
        static const char* const mounts[] = {"arm_a", "arm_b", "back_a", "back_b"};
        for (int m = 0; m < 4; ++m) {
            const std::uint32_t w = r + 0x0A + static_cast<std::uint32_t>(m) * 4;
            weapons +=
                " " + tuned(mounts[m], xbe_i16(x, w), static_cast<std::int8_t>(xbe_u8(x, w + 2)));
        }
        weapons += ")";
        add_row(
            file,
            {"scoobee",
             {field("id", id), field("name", text_at(t, xbe_u32(x, r + 0x38))),
              field("unused_00", xbe_i16(x, r)),
              tuned("body", xbe_i16(x, r + 2), static_cast<std::int8_t>(xbe_u8(x, r + 4))),
              tuned("leg", xbe_i16(x, r + 6), static_cast<std::int8_t>(xbe_u8(x, r + 8))), weapons,
              tuned("chip", xbe_i16(x, r + 0x1A), static_cast<std::int8_t>(xbe_u8(x, r + 0x1C))),
              field("color", xbe_i16(x, r + 0x1E)), field("sticker", xbe_i16(x, r + 0x20)),
              ints_i16("option", x, r + 0x22, 10)}});
    }
    return write_table(t, file);
}

// a pilot's shown name, through its identity row, for notes
std::string pilot_name(Tables& t, int pilot, bool scoobee) {
    if (pilot < 0 || pilot >= pilot_count) {
        return "";
    }
    const std::uint32_t r = xbe_at::pilot_records + static_cast<std::uint32_t>(pilot) * 0x1C;
    const int identity = xbe_i16(*t.xbe, r + (scoobee ? 2 : 0));
    if (identity < 0 || identity >= identity_count) {
        return "";
    }
    return utf8_at(
        t, xbe_u32(*t.xbe, xbe_at::identities + static_cast<std::uint32_t>(identity) * 0x10));
}

// "pilot / Scoobee" names for a row's note, empty when neither has one
std::string pilot_note(Tables& t, int pilot) {
    const std::string name = pilot_name(t, pilot, false);
    const std::string scoobee = pilot_name(t, pilot, true);
    return name.empty() && scoobee.empty() ? std::string() : name + " / " + scoobee;
}

bool write_opponents(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{
        "opponents",
        "rumble_opponent_entries (OpponentEntry)",
        xbe_at::opponent_entries,
        opponent_count,
        {"who fights: pilot (pilots.sexp), loadout (scoobees.sexp), ai_profile (row of "
         "ai_selection_profiles, ai.sexp), level (the tune applied to every part, -99..99)",
         "the note is the pilot's and the Scoobee's shown names"},
        {}};
    for (int id = 0; id < opponent_count; ++id) {
        const std::uint32_t r = xbe_at::opponent_entries + static_cast<std::uint32_t>(id) * 8;
        const int pilot = xbe_i16(x, r);
        add_row(file, {"opponent",
                       {field("id", id), field("pilot", pilot), field("loadout", xbe_i16(x, r + 2)),
                        field("ai_profile", xbe_i16(x, r + 4)), field("level", xbe_i16(x, r + 6))},
                       pilot_note(t, pilot)});
    }
    return write_table(t, file);
}

bool write_pilots(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"pilots",
                   "rumble_pilot_records (PilotRecord)",
                   xbe_at::pilot_records,
                   pilot_count,
                   {"identity and scoobee_identity: rows of identities.sexp; team: row of "
                    "teams.sexp, -1 none (equal teams can't target each other)",
                    "destruction_value: money for destroying this pilot by level, a curve "
                    "(curve base at_mid at_max mid negative), units of 10 New Yen",
                    "reaction: row of reaction_profiles.sexp (battle barks). unused_06, unused_1a "
                    "(+0x06, +0x1A): no reader "
                    "read"},
                   {}};
    for (int id = 0; id < pilot_count; ++id) {
        const std::uint32_t r = xbe_at::pilot_records + static_cast<std::uint32_t>(id) * 0x1C;
        add_row(
            file,
            {"pilot",
             {field("id", id), field("identity", xbe_i16(x, r)),
              field("scoobee_identity", xbe_i16(x, r + 2)), field("team", xbe_i16(x, r + 4)),
              field("unused_06", xbe_i16(x, r + 6)), field("destruction_value", curve_u(x, r + 8)),
              field("reaction", xbe_i16(x, r + 0x18)), field("unused_1a", xbe_i16(x, r + 0x1A))},
             pilot_note(t, id)});
    }
    return write_table(t, file);
}

bool write_identities(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile kinds{
        "identity_kinds",
        "D_001C98D8 (char *)",
        xbe_at::identity_kinds,
        identity_kind_count,
        {"labels matching IdentityRecord.f_0c's values (kind) (man, woman, CPU, the animals); "
         "no reader read"},
        {}};
    for (int id = 0; id < identity_kind_count; ++id) {
        add_row(kinds,
                {"identity_kind",
                 {field("id", id),
                  field("name", text_at(t, xbe_u32(x, xbe_at::identity_kinds +
                                                          static_cast<std::uint32_t>(id) * 4)))}});
    }
    TableFile file{
        "identities",
        "rumble_identities (IdentityRecord)",
        xbe_at::identities,
        identity_count,
        {"name: shown name; name_en: the Latin spelling in most rows (a guess)",
         "sprite: render registry id of the picture (a pilot's portrait, a Scoobee's, "
         "a chip's art)",
         "unused_0a (+0x0A): no reader read, -1..27 (the range of the 28 portrait actors); "
         "kind (+0x0C): no "
         "reader read, a row of identity_kinds.sexp in every record"},
        {}};
    for (int id = 0; id < identity_count; ++id) {
        const std::uint32_t r = xbe_at::identities + static_cast<std::uint32_t>(id) * 0x10;
        const int kind = xbe_i32(x, r + 0x0C);
        std::string kind_note;
        if (kind >= 0 && kind < identity_kind_count) {
            kind_note = utf8_at(
                t, xbe_u32(x, xbe_at::identity_kinds + static_cast<std::uint32_t>(kind) * 4));
        }
        add_row(file, {"identity",
                       {field("id", id), field("name", text_at(t, xbe_u32(x, r))),
                        field("name_en", text_at(t, xbe_u32(x, r + 4))),
                        field("sprite", xbe_i16(x, r + 8)),
                        field("unused_0a", xbe_i16(x, r + 0x0A)), field("kind", kind)},
                       kind_note});
    }
    return write_table(t, kinds) && write_table(t, file);
}

bool write_teams(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"teams",
                   "rumble_team_names (char *)",
                   xbe_at::team_names,
                   team_count,
                   {"the team a pilot flies for, by PilotRecord.team; 68..83 never fly",
                    "team -1 reads the none name (D_00206E20)"},
                   {}};
    add_row(
        file,
        {"team", {field("id", -1), field("name", text_at(t, xbe_u32(x, xbe_at::team_none_name)))}});
    for (int id = 0; id < team_count; ++id) {
        add_row(file,
                {"team",
                 {field("id", id),
                  field("name", text_at(t, xbe_u32(x, xbe_at::team_names +
                                                          static_cast<std::uint32_t>(id) * 4)))}});
    }
    return write_table(t, file);
}

bool write_reaction_profiles(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"reaction_profiles",
                   "rumble_reaction_profiles (ReactionProfile)",
                   xbe_at::reaction_profiles,
                   reaction_count,
                   {"a pilot's battle barks: 35 event scripts by situation (which slot is which "
                    "isn't read yet), chosen by PilotRecord.reaction or ChipRecord.bark_profile",
                    "each is the XBE address of an EventCommand array (src/data/script_barks.c), "
                    "nil for none; the event text family keys scripts by that address"},
                   {}};
    for (int id = 0; id < reaction_count; ++id) {
        const std::uint32_t r = xbe_at::reaction_profiles + static_cast<std::uint32_t>(id) * 0x8C;
        std::string barks = "(barks";
        for (int b = 0; b < barks_per_profile; ++b) {
            const std::uint32_t script = xbe_u32(x, r + static_cast<std::uint32_t>(b) * 4);
            barks += " " + (script == 0 ? std::string("nil") : sx_hex(script));
        }
        add_row(file, {"reaction_profile", {field("id", id), barks + ")"}});
    }
    return write_table(t, file);
}

} // namespace

bool write_roster_tables(Tables& tables) {
    return write_scoobees(tables) && write_opponents(tables) && write_pilots(tables) &&
           write_identities(tables) && write_teams(tables) && write_reaction_profiles(tables);
}
