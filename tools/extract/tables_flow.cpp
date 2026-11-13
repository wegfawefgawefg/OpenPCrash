// Flow: the per-state tables, the conversation trigger table, movies, the ending's subtitles, the
// help menu and the impact effects (docs/port/data.md "Flow tables" and "Effects";
// src/data/state_tables.c, event_triggers.c, misc_k.c, movies.c, pepper_talk.c, misc_b.c).
#include "addresses.hpp"
#include "sexp_out.hpp"
#include "tables.hpp"

#include <cstdio>

namespace {

constexpr int state_count = 160; // 159 states and 159 (0x9F), "none"
constexpr int trigger_count = 110;
constexpr int predicate_count = 75;
constexpr int movie_count = 11;
constexpr int pepper_talk_count = 35;
constexpr int impact_fx_count = 11;

// application states by number, the decomp's entry function names without state_ and _enter
// (app_state_entry_table); 0x30..0x9D are the conversations
const char* const named_states[] = {
    "adjust",        "attract_demo", "attract_movie", "area_select",    "assemble",
    "replay",        "battle",       "unused_battle", "club_wired",     "records_check",
    "ending_data",   "ending",       "secret_movies", "garage",         "hangar",
    "wild_machines", "key_config",   "save_load",     "logo",           "pepper_talk",
    "sonic_amp",     "title",        "name_entry",    "net_lobby",      "day_advance",
    "opening_movie", "options",      "plus_tech",     "new_game",       "record",
    "result",        "save_menu",    "schedule",      "media_playlist", "dealer",
    "sound_config",  "spec_scoobee", "credits",       "stock_module",   "system",
    "title_return",  "town_map",     "free_play",     "keep_sv",        "vs_area_select",
    "dolby_logo",    "save_files",   "test_drive"};
constexpr int named_state_count = sizeof named_states / sizeof named_states[0];

std::string state_name(int state) {
    if (state >= 0 && state < named_state_count) {
        return named_states[state];
    }
    if (state >= 0x30 && state <= 0x9D) {
        char text[16];
        std::snprintf(text, sizeof text, "talk_%02x", state);
        return text;
    }
    if (state == 0x9E) {
        return "phantagram_logo";
    }
    return state == 0x9F ? "none" : "unknown";
}

// event_predicates by slot, the decomp's trigger_* names; 75 is "none"
std::string predicate_name(int slot) {
    if (slot == 0) {
        return "rule_1_to_4";
    }
    if (slot >= 9 && slot <= 11) {
        return "area" + std::to_string(slot - 9) + "_ranker_today";
    }
    if (slot == 12) {
        return "ranker_bout_won";
    }
    if (slot == 13) {
        return "ranker_bout_unwon";
    }
    if (slot == 14) {
        return "ranker_bout_unwon_any";
    }
    if (slot >= 19 && slot <= 28) {
        static const int steps[] = {1, 3, 6, 9, 12, 15, 18, 21, 24, 27};
        return "story_ge_" + std::to_string(steps[slot - 19]);
    }
    if (slot == 29) {
        return "rule_23";
    }
    if (slot == 30) {
        return "class_wins_changed";
    }
    if (slot == 31) {
        return "songs_ge_78";
    }
    if (slot == 50) {
        return "never";
    }
    // class 0, 2 and 1 wins at least 1..8, in that order
    if (slot >= 51 && slot < predicate_count) {
        static const int classes[] = {0, 2, 1};
        return "class" + std::to_string(classes[(slot - 51) / 8]) + "_wins_ge_" +
               std::to_string((slot - 51) % 8 + 1);
    }
    return slot == predicate_count ? "none" : "always";
}

bool write_states(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"states",
                   "frame_cadence_table, state_pause_allowed, app_state_restart_table, "
                   "state_music, state_pad_pause, app_state_entry_table",
                   xbe_at::frame_cadence_table,
                   state_count,
                   {"per application state; state numbers are kept (triggers, music, cadence and "
                    "the help menu key on them)",
                    "cadence: updates a second (60 menus, 30 battle); pause_allowed: 1 lets the "
                    "start button pause; restart: where a hard reset goes",
                    "music: row of music_files.sexp, -1 none; pad_pause (a guess): 0 means a lost "
                    "pad doesn't pause; entry: XBE address of the state's entry function",
                    "boot: the state the game starts in (app_state_boot)"},
                   {}};
    add_row(file, {"boot",
                   {field_enum("state", xbe_i32(x, xbe_at::app_state_boot),
                               state_name(xbe_i32(x, xbe_at::app_state_boot)).c_str())}});
    for (int id = 0; id < state_count; ++id) {
        const std::uint32_t at = static_cast<std::uint32_t>(id) * 4;
        const int restart = xbe_i32(x, xbe_at::app_state_restart_table + at);
        Row row{"state",
                {field("id", id), field("name", state_name(id)),
                 field("cadence", xbe_i32(x, xbe_at::frame_cadence_table + at)),
                 field("pause_allowed", xbe_i32(x, xbe_at::state_pause_allowed + at)),
                 field_enum("restart", restart, state_name(restart).c_str()),
                 field("music", xbe_i32(x, xbe_at::state_music + at))},
                ""};
        // the last two tables stop at 159 rows
        if (id < state_count - 1) {
            row.fields.push_back(field("pad_pause", xbe_i32(x, xbe_at::state_pad_pause + at)));
            row.fields.push_back(
                field("entry", sx_hex(xbe_u32(x, xbe_at::app_state_entry_table + at))));
        }
        add_row(file, row);
    }
    return write_table(t, file);
}

bool write_triggers(Tables& t) {
    const Xbe& x = *t.xbe;
    // check the predicate names against the table: equal names must be equal functions
    for (int a = 0; a < predicate_count; ++a) {
        for (int b = a + 1; b < predicate_count; ++b) {
            const bool same_code =
                xbe_u32(x, xbe_at::event_predicates + static_cast<std::uint32_t>(a) * 4) ==
                xbe_u32(x, xbe_at::event_predicates + static_cast<std::uint32_t>(b) * 4);
            if (same_code != (predicate_name(a) == predicate_name(b))) {
                std::fprintf(stderr, "tables: predicate names %d and %d don't match the code\n", a,
                             b);
            }
        }
    }
    TableFile file{
        "event_triggers",
        "event_policy_table (EventTrigger)",
        xbe_at::event_policy_table,
        trigger_count,
        {"leaving state src for dst plays conversation state instead when everything "
         "holds; rows are found by state, not by index",
         "prereq_first/last: trigger row that must be done first, both 0 none (only "
         "first == last is ever tested); repeat: -1 replays, 1 once",
         "predicate: slot of event_predicates (75 none); tournament 56+, stage 4+, rule "
         "28+, night 2+ mean any; stage is the selector / 2 and the predicate's argument",
         "enabled: 0 on the rows only the help menu plays; background: 0..5 location, "
         "6 none; cast: portrait actors, -1 padded",
         "music: row of music_files.sexp, -1 none, 202 a dynamic track; volume 0..1. "
         "pad_15, pad_1d: zero in every row"},
        {}};
    for (int id = 0; id < trigger_count; ++id) {
        const std::uint32_t r = xbe_at::event_policy_table + static_cast<std::uint32_t>(id) * 0x24;
        const int predicate = xbe_u8(x, r + 0x0F);
        add_row(file, {"trigger",
                       {field("id", id),
                        field_enum("state", xbe_i32(x, r), state_name(xbe_i32(x, r)).c_str()),
                        field_enum("src", xbe_i32(x, r + 4), state_name(xbe_i32(x, r + 4)).c_str()),
                        field_enum("dst", xbe_i32(x, r + 8), state_name(xbe_i32(x, r + 8)).c_str()),
                        field("prereq_first", static_cast<std::int8_t>(xbe_u8(x, r + 0x0C))),
                        field("prereq_last", static_cast<std::int8_t>(xbe_u8(x, r + 0x0D))),
                        field("repeat", static_cast<std::int8_t>(xbe_u8(x, r + 0x0E))),
                        field_enum("predicate", predicate, predicate_name(predicate).c_str()),
                        field("tournament", xbe_u8(x, r + 0x10)),
                        field("stage", xbe_u8(x, r + 0x11)), field("rule", xbe_u8(x, r + 0x12)),
                        field("night", xbe_u8(x, r + 0x13)), field("enabled", xbe_u8(x, r + 0x14)),
                        field("background", static_cast<std::int8_t>(xbe_u8(x, r + 0x16))),
                        ints_i8("cast", x, r + 0x17, 6), field("music", xbe_i16(x, r + 0x1E)),
                        field("volume", fl(xbe_f32(x, r + 0x20)))}});
    }
    return write_table(t, file);
}

bool write_movies(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile movies{"movies",
                     "movie_files (char *)",
                     xbe_at::movie_files,
                     movie_count,
                     {"the movies by the id state code plays (movie_play 0xAEB76)"},
                     {}};
    for (int id = 0; id < movie_count; ++id) {
        add_row(movies,
                {"movie",
                 {field("id", id),
                  field("file", text_at(t, xbe_u32(x, xbe_at::movie_files +
                                                          static_cast<std::uint32_t>(id) * 4)))}});
    }
    // the subtitles run to a row of -1, -1
    TableFile subtitles{"ending_subtitles",
                        "ending_subtitles (Subtitle)",
                        xbe_at::ending_subtitles,
                        0,
                        {"lines drawn over ending.sfd (movie 2) from start to end, in movie frames "
                         "at 30 a second (ending_movie_subtitles_draw)"},
                        {}};
    for (int id = 0; id < 64; ++id) {
        const std::uint32_t r = xbe_at::ending_subtitles + static_cast<std::uint32_t>(id) * 8;
        if (xbe_i16(x, r) == -1) {
            break;
        }
        add_row(subtitles,
                {"subtitle",
                 {field("id", id), field("start", xbe_i16(x, r)), field("end", xbe_i16(x, r + 2)),
                  field("text", text_at(t, xbe_u32(x, r + 4)))}});
        ++subtitles.rows;
    }
    return write_table(t, movies) && write_table(t, subtitles);
}

bool write_pepper_talk(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"pepper_talk",
                   "pepper_talk_items (PepperTalkItem)",
                   xbe_at::pepper_talk_items,
                   pepper_talk_count,
                   {"the help menu (\"Pepper's talk\"): parent is the row holding this one (35 top "
                    "level), state the state the row opens (159 a folder)",
                    "once the game is beaten 0x80 and 0x9D replace 0x7F and 0x70 "
                    "(pepper_talk_menu_update); pad_08 is zero in every row"},
                   {}};
    for (int id = 0; id < pepper_talk_count; ++id) {
        const std::uint32_t r = xbe_at::pepper_talk_items + static_cast<std::uint32_t>(id) * 0xC;
        const int state = xbe_i16(x, r + 2);
        add_row(file, {"item",
                       {field("id", id), field("parent", xbe_i16(x, r)),
                        field_enum("state", state, state_name(state).c_str()),
                        field("label", text_at(t, xbe_u32(x, r + 4)))}});
    }
    return write_table(t, file);
}

bool write_impact_fx(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"impact_fx",
                   "impact_fx.rows (ImpactFx)",
                   xbe_at::impact_fx_rows,
                   impact_fx_count,
                   {"what a shot leaves where it hits, by WeaponRecord.impact_fx: the effect set "
                    "(effect_set_play row), its scale, and mark 1 for a scorch mark too",
                    "the shot code at 0x97660 hands the three to impact_effect_spawn 0x65EA8"},
                   {}};
    for (int id = 0; id < impact_fx_count; ++id) {
        const std::uint32_t r = xbe_at::impact_fx_rows + static_cast<std::uint32_t>(id) * 0xC;
        add_row(file, {"impact_fx",
                       {field("id", id), field("scale", fl(xbe_f32(x, r))),
                        field("effect_set", xbe_i32(x, r + 4)), field("mark", xbe_u8(x, r + 8))}});
    }
    return write_table(t, file);
}

} // namespace

bool write_flow_tables(Tables& tables) {
    return write_states(tables) && write_triggers(tables) && write_movies(tables) &&
           write_pepper_talk(tables) && write_impact_fx(tables);
}
