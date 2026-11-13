// AI tuning tables, 0x205DA8..0x206E18 (docs/port/data.md "Scoobees, opponents, pilots",
// docs/port/gameplay.md "AI", src/data/roster.c).
#include "addresses.hpp"
#include "sexp_out.hpp"
#include "tables.hpp"

namespace {

void add_byte_rows(TableFile& file, const Xbe& x, const char* head, std::uint32_t va, int rows,
                   int columns, const char* values) {
    for (int id = 0; id < rows; ++id) {
        add_row(file,
                {head,
                 {field("id", id),
                  ints_u8(values, x, va + static_cast<std::uint32_t>(id * columns), columns)}});
    }
}

} // namespace

bool write_ai_tables(Tables& tables) {
    const Xbe& x = *tables.xbe;
    TableFile file{
        "ai",
        "ai_transition_probs_normal .. ai_search_move_weights",
        xbe_at::ai_transition_probs_normal,
        0,
        {"AI tuning; head = the decomp's table name, one form per row",
         "transition_probs: chance of each next state per row, each of 5 a threshold "
         "out of 255 (0x5C600); alternate is the alternate battle mode",
         "selection_profiles: OpponentEntry.ai_profile rows; five rows by what happened, "
         "five columns by durability bucket, a cell is a transition_probs row (0x5C08C)",
         "stat_bands: level / 20 picks a row of boost, jump and dodge chances "
         "(ai_stat_bands_apply 0x5CC69)",
         "nav_resource_selectors: ENPT graph asset ids by stage selector; "
         "transition_result_map: AI state per transition column",
         "level_timing_profiles: six state timers in frames by level (signed level + "
         "128 is the row); only levels -99 -40 0 10 50 99 have real rows",
         "search_move_weights: ai_weighted_pick weights for ai_combat_lost_target. "
         "d_*: D_ tables with no reader read"},
        {}};
    add_byte_rows(file, x, "ai_transition_probs_normal", xbe_at::ai_transition_probs_normal, 42, 5,
                  "thresholds");
    add_byte_rows(file, x, "ai_transition_probs_alternate", xbe_at::ai_transition_probs_alternate,
                  42, 5, "thresholds");
    add_byte_rows(file, x, "ai_selection_profiles", xbe_at::ai_selection_profiles, 12, 25, "cells");
    for (int id = 0; id < 20; ++id) {
        const std::uint32_t r = xbe_at::ai_stat_bands + static_cast<std::uint32_t>(id) * 6;
        add_row(file, {"ai_stat_bands",
                       {field("id", id), field("boost", xbe_i16(x, r)),
                        field("jump", xbe_i16(x, r + 2)), field("dodge", xbe_i16(x, r + 4))}});
    }
    add_row(file, {"d_0020611c", {ints_i32("values", x, xbe_at::ai_d_0020611c, 12)}});
    add_row(file, {"ai_nav_resource_selectors",
                   {ints_i32("assets", x, xbe_at::ai_nav_resource_selectors, 8)}});
    for (int id = 0; id < 5; ++id) {
        add_row(file, {"d_00206170",
                       {field("id", id),
                        floats("values", x,
                               xbe_at::ai_d_00206170 + static_cast<std::uint32_t>(id) * 16, 4)}});
    }
    add_row(file, {"ai_transition_result_map",
                   {ints_i32("states", x, xbe_at::ai_transition_result_map, 5)}});
    add_row(file, {"d_002061d4", {ints_i32("values", x, xbe_at::ai_d_002061d4, 12)}});
    for (int id = 0; id < 256; ++id) {
        add_row(file, {"ai_level_timing_profiles",
                       {field("level", id - 128), ints_i16("timers", x,
                                                           xbe_at::ai_level_timing_profiles +
                                                               static_cast<std::uint32_t>(id) * 12,
                                                           6)}});
    }
    add_row(file, {"ai_search_move_weights",
                   {ints_i16("weights", x, xbe_at::ai_search_move_weights, 8)}});
    file.rows = static_cast<int>(file.forms.size());
    return write_table(tables, file);
}
