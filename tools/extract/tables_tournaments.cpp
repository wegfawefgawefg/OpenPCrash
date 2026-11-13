// Tournaments, rules, opponent pools, the 60-day schedule and the shops' stock (docs/port/data.md
// "Tournaments, days and shops", src/data/tournaments.c). Row counts here are save format.
#include "addresses.hpp"
#include "sexp_out.hpp"
#include "tables.hpp"

namespace {

constexpr int day_count = 60;
constexpr int tournament_count = 56;
constexpr int rule_count = 28;
constexpr int pool_count = 272;
constexpr int dealer_stock_count = 65;
constexpr int chip_stock_count = 24;
constexpr int parts_stock_count = 135;
constexpr int scoobee_stock_count = 24;

// what each of Tournament.pickup_weights draws (pickup_apply 0x66DD0): kinds 0..3 score,
// 4..6 repair a percentage, 7..9 refill one weapon by a percentage
const char* const pickup_kinds[10] = {"score_100", "score_500", "score_1000", "score_10000",
                                      "repair_10", "repair_30", "repair_50",  "refill_30",
                                      "refill_50", "refill_100"};

bool write_tournaments(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{
        "tournaments",
        "tournaments (Tournament)",
        xbe_at::tournaments,
        tournament_count,
        {"rule: row of tournament_rules.sexp; night: 0 or 1, the stage selector is stage "
         "* 2 + night",
         "level_min/max: an opponent outside can't enter; regular_quota: draws from the "
         "regular queues while fewer are out",
         "max_opponents: plus 1, capped at 6, is the roster size; reserve_pool and pools: "
         "rows of opponent_pools.sexp, -1 none",
         "fixed_opponent: row of opponents.sexp, -1 none, gets role 2; pickup_weights: "
         "chance of each supply crate kind (pickup_apply)",
         "47 test drive, 51 trial version, 55 tutorial, 52..54 the post-game Burning "
         "Chrome set (rumble_tournaments in symbols.txt is the old off-by-four 0x24B2A4)"},
        {}};
    for (int id = 0; id < tournament_count; ++id) {
        const std::uint32_t r = xbe_at::tournaments + static_cast<std::uint32_t>(id) * 0x6C;
        std::string weights = "(pickup_weights";
        for (int k = 0; k < 10; ++k) {
            weights += " (" + std::string(pickup_kinds[k]) + " " +
                       fl(xbe_f32(x, r + 0x44 + static_cast<std::uint32_t>(k) * 4)) + ")";
        }
        weights += ")";
        add_row(
            file,
            {"tournament",
             {field("id", id), field("name", text_at(t, xbe_u32(x, r))),
              field("rule", xbe_i32(x, r + 4)), field("night", xbe_i32(x, r + 8)),
              field("level_min", xbe_i32(x, r + 0x0C)), field("level_max", xbe_i32(x, r + 0x10)),
              field("regular_quota", xbe_i32(x, r + 0x14)),
              field("max_opponents", xbe_i32(x, r + 0x18)),
              field("reserve_pool", xbe_i32(x, r + 0x1C)), ints_i32("pools", x, r + 0x20, 8),
              field("fixed_opponent", xbe_i32(x, r + 0x40)), weights}});
    }
    return write_table(t, file);
}

bool write_rules(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"tournament_rules",
                   "tournament_rules (TournamentRule)",
                   xbe_at::tournament_rules,
                   rule_count,
                   {"name and subtitle: the class label; entry_fee (likely), units of 10 New Yen",
                    "weight_min/max: hundredths of a ton, -1 no limit; body_class: 7 any, else the "
                    "Scoobee's must equal it",
                    "leg_classes: 8 or -1 skip, else the Scoobee's leg class must be one of them",
                    "extra: 1 no optical camouflage; 0 only on rule 25 \"Rental Use Only\" (a "
                    "guess); -1 none"},
                   {}};
    for (int id = 0; id < rule_count; ++id) {
        const std::uint32_t r = xbe_at::tournament_rules + static_cast<std::uint32_t>(id) * 0x2C;
        add_row(
            file,
            {"rule",
             {field("id", id), field("name", text_at(t, xbe_u32(x, r))),
              field("subtitle", text_at(t, xbe_u32(x, r + 4))),
              field("entry_fee", xbe_i32(x, r + 8)), field("weight_min", fl(xbe_f32(x, r + 0x0C))),
              field("weight_max", fl(xbe_f32(x, r + 0x10))),
              field("body_class", xbe_i32(x, r + 0x14)), ints_i32("leg_classes", x, r + 0x18, 4),
              field("extra", xbe_i32(x, r + 0x28))}});
    }
    return write_table(t, file);
}

bool write_pools(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"opponent_pools",
                   "opponent_pools (OpponentPool)",
                   xbe_at::opponent_pools,
                   pool_count,
                   {"opponents a tournament draws from: rows of opponents.sexp, -1 none; the "
                    "primary is tested first",
                    "the save keeps progress per pool row (GameState.pools[0x110])"},
                   {}};
    for (int id = 0; id < pool_count; ++id) {
        const std::uint32_t r = xbe_at::opponent_pools + static_cast<std::uint32_t>(id) * 0x24;
        add_row(file, {"pool",
                       {field("id", id), field("primary", xbe_i32(x, r)),
                        ints_i32("alternates", x, r + 4, 8)}});
    }
    return write_table(t, file);
}

bool write_days(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{
        "day_schedule",
        "day_schedule (DaySchedule)",
        xbe_at::day_schedule,
        day_count,
        {"one day of the 60-day cycle (day % 60)",
         "dealer: dealer_stock rows for Aeron, Photon, Holy; tournament: the four slots' "
         "tournaments, [3] is also the chip shop's chip_stock row",
         "body leg arm back option color sticker: parts_stock rows; scoobee: "
         "scoobee_stock row (shop_stock.sexp)"},
        {}};
    static const char* const kinds[] = {"body",   "leg",   "arm",     "back",
                                        "option", "color", "sticker", "scoobee"};
    for (int id = 0; id < day_count; ++id) {
        const std::uint32_t r = xbe_at::day_schedule + static_cast<std::uint32_t>(id) * 0x3C;
        Row row{
            "day",
            {field("id", id), ints_i32("dealer", x, r, 3), ints_i32("tournament", x, r + 0x0C, 4)},
            ""};
        for (int k = 0; k < 8; ++k) {
            row.fields.push_back(
                field(kinds[k], xbe_i32(x, r + 0x1C + static_cast<std::uint32_t>(k) * 4)));
        }
        add_row(file, row);
    }
    return write_table(t, file);
}

// a stock row's items as (item tune) pairs, up to the id of -1 that ends it
std::string stock_items(const Xbe& x, std::uint32_t va, int slots) {
    std::string out = "(items";
    for (int i = 0; i < slots; ++i) {
        const int item = xbe_i32(x, va + static_cast<std::uint32_t>(i) * 8);
        if (item == -1) {
            break;
        }
        out += " (" + sx_int(item) + " " +
               sx_int(xbe_i32(x, va + static_cast<std::uint32_t>(i) * 8 + 4)) + ")";
    }
    return out + ")";
}

bool write_stock(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"shop_stock",
                   "dealer_stock, chip_stock, parts_stock, scoobee_stock",
                   xbe_at::dealer_stock,
                   dealer_stock_count + chip_stock_count + parts_stock_count + scoobee_stock_count,
                   {"what the shops sell, picked per day by day_schedule.sexp",
                    "dealer (DealerStock 0x24FB30, 65): three (offer loadout display), loadout a "
                    "row of scoobees.sexp, display a selector (0x6D2xx)",
                    "chip (ItemStock 0x250148, 24) and parts (ItemStock 0x250808, 135): (items "
                    "(id tune) ...), ids of the kind the day's field names; the list ends at an "
                    "id of -1",
                    "scoobee (ScoobeeStock 0x252E00, 24): (items (loadout tune) ...), loadouts are "
                    "rows of scoobees.sexp"},
                   {}};
    for (int id = 0; id < dealer_stock_count; ++id) {
        const std::uint32_t r = xbe_at::dealer_stock + static_cast<std::uint32_t>(id) * 0x18;
        Row row{"dealer", {field("id", id)}, ""};
        for (int o = 0; o < 3; ++o) {
            row.fields.push_back(
                "(offer " + sx_int(xbe_i32(x, r + static_cast<std::uint32_t>(o) * 8)) + " " +
                sx_int(xbe_i32(x, r + static_cast<std::uint32_t>(o) * 8 + 4)) + ")");
        }
        add_row(file, row);
    }
    for (int id = 0; id < chip_stock_count; ++id) {
        add_row(file,
                {"chip",
                 {field("id", id),
                  stock_items(x, xbe_at::chip_stock + static_cast<std::uint32_t>(id) * 0x48, 9)}});
    }
    // which part kind each parts row holds, from the day fields that name it (for the note)
    static const char* const kinds[] = {"body", "leg", "arm", "back", "option", "color", "sticker"};
    std::vector<std::string> row_kinds(parts_stock_count);
    for (int day = 0; day < day_count; ++day) {
        for (int k = 0; k < 7; ++k) {
            const int row =
                xbe_i32(x, xbe_at::day_schedule + static_cast<std::uint32_t>(day) * 0x3C + 0x1C +
                               static_cast<std::uint32_t>(k) * 4);
            if (row >= 0 && row < parts_stock_count &&
                row_kinds[row].find(kinds[k]) == std::string::npos) {
                row_kinds[row] += row_kinds[row].empty() ? kinds[k] : std::string(" ") + kinds[k];
            }
        }
    }
    for (int id = 0; id < parts_stock_count; ++id) {
        add_row(file,
                {"parts",
                 {field("id", id),
                  stock_items(x, xbe_at::parts_stock + static_cast<std::uint32_t>(id) * 0x48, 9)},
                 row_kinds[id].empty() ? "no day names it" : row_kinds[id]});
    }
    for (int id = 0; id < scoobee_stock_count; ++id) {
        add_row(
            file,
            {"scoobee",
             {field("id", id),
              stock_items(x, xbe_at::scoobee_stock + static_cast<std::uint32_t>(id) * 0x30, 6)}});
    }
    return write_table(t, file);
}

bool write_groups(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"tournament_groups",
                   "class_tournaments, area_ranker_tournaments, first_ranker_tournaments",
                   xbe_at::class_tournaments,
                   5,
                   {"tournament ids (tournaments.sexp) by stage: the eight class tournaments of "
                    "each (class_wins 0x96DC3), each stage's Area Ranker, the First Ranker"},
                   {}};
    for (int stage = 0; stage < 3; ++stage) {
        add_row(file, {"class_tournaments",
                       {field("stage", stage),
                        ints_i32("tournaments", x,
                                 xbe_at::class_tournaments + static_cast<std::uint32_t>(stage) * 32,
                                 8)}});
    }
    add_row(file, {"area_ranker_tournaments",
                   {ints_i32("tournaments", x, xbe_at::area_ranker_tournaments, 3)}});
    add_row(file, {"first_ranker_tournaments",
                   {ints_i32("tournaments", x, xbe_at::first_ranker_tournaments, 1)}});
    return write_table(t, file);
}

} // namespace

bool write_tournament_tables(Tables& tables) {
    return write_tournaments(tables) && write_rules(tables) && write_pools(tables) &&
           write_days(tables) && write_stock(tables) && write_groups(tables);
}
