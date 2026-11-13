#pragma once
// The progress the story reads and writes, as the world holds it (decomp docs/port/progression.md):
// tournaments won, the opponent pools' bytes, the day, money, songs owned, the trigger rows' done
// bits, today's line-up, and the few unsaved fields the trigger resolver reads. Plain integers, so
// the world stays a copyable deterministic value. Everything else in GameState (garages,
// inventory, the roster's queues and drawn opponents) belongs to the domains that use it.
//
// The story is not scripted: conversations only draw (src/talk/). Progress is written by game code
// through the functions here, and the trigger table turns state changes plus progress into
// conversations (src/story/triggers.cpp).
#include "fx/fx.hpp"
#include "story/story_tables.hpp"

#include <cstdint>

// GameState.pools[272]: per opponent pool the primary's byte and eight alternates' bytes,
// 0 not beaten, 1 beaten, -1 no opponent there (pool_progress_new)
struct PoolProgress {
    std::int8_t primary = -1;
    std::int8_t alternates[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
};

inline constexpr int music_owned_count_max = 210; // GameState.music_owned, 0..77 are songs
inline constexpr int all_songs = 78;

// the player's machine as the last boss's defeat left it, for the ending's shots
// (GameState.ending_carry +0x10EAC, ending_durability, ending_weapons, ending_parts; not saved):
// set when scene 0x17 ends (scene_boss_defeat_exit), read by the ending's spawn and shots
struct EndingCarry {
    bool carry = false;
    int durability = 0;
    Fx resource[4];             // Weapon.resource by mount
    bool fitted[4] = {};        // Weapon.fitted
    bool broken[4] = {};        // Actor.weapon_fitted's broken words (FighterCombat.weapon_broken)
};

struct Story {
    // saved (the first 0x60C4 bytes of GameState)
    PoolProgress pools[pool_count];
    std::int8_t tournament_won[tournament_count] = {}; // 0, 1, or -1 without a fixed opponent
    int day = 0;                                       // 0..59, GameState +0x9CC bits 20..29
    int money = 0;            // 31 bits in units of 10 New Yen, GameState +0x9D0
    bool game_beaten = false; // the money word's top bit
    std::uint8_t music_owned[music_owned_count_max] = {}; // 0 no, 1 bought, 2 heard
    std::uint32_t events_done[5] = {};                    // a bit per trigger row
    int lineup[4] = {-1, -1, -1, -1};                     // rumble[0..3].tournament, -1 none today
    int completed_cycles = 0;                             // post-game loops of the 60 days
    int days_played = 0;                                  // records, up to 9999
    // not saved in retail: past +0x60C4 (map_spin_money, rumble_selected, the at-entry counts) or in
    // .data; the port's save adds map_spin_money and rumble_selected (docs/bugs.md)
    // (the talk pair); src/save keeps the world's values on a load (docs/saves.md)
    int map_spin_money = 0;       // town map stick spins paid today, up to 100 (+0x113C8)
    int rumble_selected = 0;      // area * 2 + night of the last tournament picked
    int class_wins_at_entry = 0;  // written as a tournament is entered
    int ranker_wins_at_entry = 0; // the same for the area's ranker
    int talk_src = no_state;      // the transition a chain of conversations interrupted
    int talk_dst = no_state;
    EndingCarry ending_carry;
};

// a beaten opponent's progress byte, as the fight's ticket names it: role 0 an alternate of a
// pool, 1 a pool's primary, 2 a tournament's fixed opponent (RumbleContext tickets)
struct OpponentTicket {
    int role = 0;
    int pool = -1;       // roles 0 and 1
    int alternate = -1;  // role 0, 0..7
    int tournament = -1; // role 2
};

// area select answers (area_entry_status), in the order the code tests them
enum class EntryStatus : std::uint8_t {
    Shop = 0,              // slot 3, not a tournament
    NoTournament = 1,      // a beep
    Allowed = 5,           // the fee is covered
    NoFee = 2,             // message 0
    RankerLocked = 3,      // message 5: the area's eight class tournaments aren't all won
    FirstRankerLocked = 4, // message 6: the three Area Rankers aren't all beaten
};

// a new game (src/story/progress.cpp)
// game_state_new, the story's part: events, pools, tournaments, songs, beaten, cycles
void story_boot(Story& story, const StoryTables& tables);
// new_game_reset: day 0, 20000 money keeping the beaten bit, today's line-up
void story_new_game(Story& story, const StoryTables& tables, int burning_chrome_roll);

// counters the predicates and gates read (u_0933f0.c, u_06c3e0.c)
bool tournament_won(const Story& story, int tournament);
int class_wins(const Story& story, const StoryTables& tables, int area);
int area_ranker_wins(const Story& story, const StoryTables& tables, int area);
bool area_ranker_beaten(const Story& story, const StoryTables& tables, int area);
int area_rankers_beaten(const Story& story, const StoryTables& tables);
int story_progress(const Story& story, const StoryTables& tables);
int music_owned_count(const Story& story);
// 0 a plain tournament, 1 the area's ranker bout, 2 the First Ranker (tournament_kind)
int tournament_kind(const StoryTables& tables, int area, int tournament);
// the schedule's word for a slot, 49 turned into 52 once the game is beaten (game_tournament_slot)
int game_tournament_slot(const Story& story, const StoryTables& tables, int day, int slot);
// the area slot of rumble_selected, and today's tournament in it (day_slot_of_selector,
// game_day_tournament)
int day_slot_of_selector(const Story& story);
int game_day_tournament(const Story& story, const StoryTables& tables);
// a tournament's rule, -1 for an id outside the table (retail reads past it, see story.md)
int tournament_rule(const StoryTables& tables, int tournament);

// days and line-ups (src/story/days.cpp)
// tournaments_refill, the tournament each slot holds: 48 drops out once won, and 52 is
// one of the Burning Chrome set by roll (0..2) once the game is beaten. Retail draws CRT rand
// only when it fills a 52; the caller draws the roll from the world's generator when
// lineup_wants_roll says so. Opponent queues and drawn rosters are the roster's (deferred).
bool lineup_wants_roll(const Story& story, const StoryTables& tables, int day);
void lineup_refill(Story& story, const StoryTables& tables, int day, int burning_chrome_roll);
// day_advance_end: the night's work after its screen
void day_advance(Story& story, const StoryTables& tables, int burning_chrome_roll);
// the shops' rows for a day (shop_daily_record takes day % 60)
const DayRow& day_row(const StoryTables& tables, int day);

// the area select (src/story/days.cpp)
// job_area_open: picking an area slot records area * 2 + its tournament's night
void area_pick(Story& story, const StoryTables& tables, int area);
EntryStatus area_entry_status(const Story& story, const StoryTables& tables, int area);
// entering: pays the fee and writes the at-entry counts the first-win talks compare with
void tournament_enter(Story& story, const StoryTables& tables, int area);
// money_add: amount and result held to 0..99,999,999
void money_add(Story& story, std::int64_t amount);
// a kill by a human player writes 1 into the victim's ticket byte (actor_settle_damage)
void opponent_beaten(Story& story, const OpponentTicket& ticket);
// entering the credits sets the beaten bit (game_beaten_set)
void game_beaten_set(Story& story);

// the trigger table (src/story/triggers.cpp)
bool event_done(const Story& story, int row);
// event_trigger_find: the first row for src -> dst whose nine checks hold, or
// trigger_row_count; out gets its conversation state, or dst
int event_trigger_find(const Story& story, const StoryTables& tables, int src, int dst, int& out);
bool state_is_trigger_talk(const StoryTables& tables, int state);
// app_transition_resolve: every state change goes through this before the new state is
// entered; returns the state to enter (a conversation, or dst)
int app_transition_resolve(Story& story, const StoryTables& tables, int current, int dst);
// the records screen's numbers: done rows not marked repeatable, and how many such rows exist
int event_done_count(const Story& story, const StoryTables& tables);
int event_once_count(const StoryTables& tables);
