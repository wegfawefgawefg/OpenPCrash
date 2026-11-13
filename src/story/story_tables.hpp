#pragma once
// The constant tables the story reads (decomp docs/port/progression.md): the 110 trigger rows,
// the tournaments with their rules, the 60-day schedule, the opponent pools and the tournament
// groups, loaded from data/tables/ (docs/tables.md). Loaded once and passed to the story code;
// they are not part of the world, which only holds progress (src/story/story.hpp).
#include <cstdint>
#include <string>
#include <vector>

inline constexpr int trigger_row_count = 110;    // event_policy_table
inline constexpr int tournament_count = 56;      // tournaments
inline constexpr int tournament_rule_count = 28; // tournament_rules
inline constexpr int pool_count = 272;           // opponent_pools
inline constexpr int day_count = 60;             // day_schedule
inline constexpr int predicate_slot_count = 75;  // event_predicates; 75 and up is none
inline constexpr int no_state = 0x9F;            // the "no state" a conversation asks for to end

// TournamentRow::rule values the code tests (tournament_rules)
inline constexpr int rule_area_ranker = 0x16;  // 22: an Area Ranker's bout
inline constexpr int rule_first_ranker = 0x17; // 23: the First Ranker's
inline constexpr int rule_rental_only = 0x19;  // 25: rentals only

// what an event_predicates slot tests (misc_k.c), taken from the function name the tables
// family writes beside the slot
enum class Predicate : std::uint8_t {
    None,               // slot 75 or more: no test
    Always,             // trigger_always
    Never,              // trigger_never
    Rule1To4,           // trigger_rule_1_to_4
    AreaRankerToday,    // trigger_areaN_ranker_today, arg the area slot
    RankerBoutWon,      // trigger_ranker_bout_won
    RankerBoutUnwon,    // trigger_ranker_bout_unwon
    RankerBoutUnwonAny, // trigger_ranker_bout_unwon_any
    StoryAtLeast,       // trigger_story_ge_N, arg N
    Rule23,             // trigger_rule_23
    ClassWinsChanged,   // trigger_class_wins_changed
    SongsAtLeast,       // trigger_songs_ge_78, arg 78
    ClassWinsAtLeast,   // trigger_classA_wins_ge_N, area and N
};

// one EventTrigger row; the byte fields keep the raw values, read as signed chars where the
// resolver does (event_trigger_find)
struct TriggerRow {
    int state = no_state; // the conversation state
    int src = -1;
    int dst = -1;
    std::int8_t prereq_first = 0;
    std::int8_t prereq_last = 0;
    std::int8_t repeat = 1; // -1 "repeatable": cleared when a chain ends (retail never clears it)
    std::uint8_t predicate_slot = 75;
    Predicate predicate = Predicate::None;
    int predicate_area = 0;       // AreaRankerToday, ClassWinsAtLeast
    int predicate_count = 0;      // StoryAtLeast, SongsAtLeast, ClassWinsAtLeast
    std::uint8_t tournament = 56; // 56 or more (or negative as a char): any
    std::uint8_t stage = 4;       // area slot, 4 or more: any; also the predicate's argument
    std::uint8_t rule = 28;       // 28 or more: any
    std::uint8_t night = 2;       // 2 or more: any
    std::uint8_t enabled = 1;     // 0 on the rows only the help menu plays
    // the policy the conversation task applies (talk_policy_apply); presentation only
    std::int8_t background = 6; // 0..5 location, 6 none
    std::int8_t cast[6] = {-1, -1, -1, -1, -1, -1};
    int music = -1;           // -1 keep, 202 the shuffled catalogue, else a track
    int volume_percent = 100; // the row's float volume, in percent (0.4 -> 40)
};

struct TournamentRow {
    int rule = 0;
    int night = 0;
    int level_min = 0, level_max = 99; // an opponent outside can't enter
    int regular_quota = 0;             // draws from the regular queues while fewer are out
    int max_opponents = 0;             // plus 1, capped at 6, is the roster size
    int fixed_opponent = -1;
    int reserve_pool = -1;
    int pools[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
};

struct RuleRow {
    int entry_fee = 0; // units of 10 NY
    float weight_min = -1, weight_max = -1; // hundredths of a ton, -1 none
    int body_class = 7;                     // 7 any
    int leg_classes[4] = {8, -1, -1, -1};   // 8 or -1 skip, else the leg kind must be one
    int extra = -1; // 1 no optical camouflage (No O.C.), 0 rentals only (rule 25), -1 none
};

// a day_schedule row: today's tournament per area slot (-1 none) and the chip shop's row as
// word 3, the dealers' rows, and the parts and used Scoobee shops' rows
struct DayRow {
    int tournament[4] = {-1, -1, -1, 0};
    int dealer[3] = {};
    int body = 0, leg = 0, arm = 0, back = 0, option = 0, color = 0, sticker = 0, scoobee = 0;
};

struct PoolRow {
    int primary = -1;
    int alternates[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
};

struct StoryTables {
    std::vector<TriggerRow> triggers;              // 110, in table order (order is semantics)
    std::vector<TournamentRow> tournaments;        // 56
    std::vector<RuleRow> rules;                    // 28
    std::vector<DayRow> days;                      // 60
    std::vector<PoolRow> pools;                    // 272
    int class_tournaments[3][8] = {};              // class_tournaments, by area
    int area_ranker_tournaments[3] = {14, 31, 48}; // area_ranker_tournaments
    int first_ranker_tournament = 49;              // first_ranker_tournaments
    // burning_chrome_tournaments (decomp src/data/ui_b2.c), no extractor writes it yet
    int burning_chrome_tournaments[3] = {52, 53, 54};
};

bool load_story_tables(StoryTables& tables, const std::string& data_dir, std::string& error);
// the row playing a conversation state, or trigger_row_count (event_policy_row_of_state)
int trigger_row_of_state(const StoryTables& tables, int state);
