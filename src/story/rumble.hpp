#pragma once
// Today's opponents (decomp src/hand/u_0451f0.c, RumbleContext; docs/port/progression.md "Who you
// fight", battle.md "Replacements"): each area slot's three queues of opponent tickets
// (fill_opponent_queues), the five drawn into its roster (tournaments_refill), the draw and the
// hand back as fighters come and go (choose_opponent, materialize_actor, release_opponent), and
// the fixed opponent's late queueing. The queues and rosters are the world's rumble[4]
// (world/game_state.hpp), so a save keeps them; each slot's tournament is story.lineup[slot].
//
// The gameplay RNG (rand_float) draws as retail's do. Retail's CRT rand draws (the Burning Chrome
// pick) come from the world's generator too: the port has one (docs/bugs.md).
#include "world/game_state.hpp"

struct World;
struct WorldRandom;
struct RosterTables;

// rumble_context_by_selector: selector / 2 (anything else is rumble[0])
int rumble_slot_of_selector(int selector);

// the progress byte a ticket names (opponent_ticket_flag): 0 not beaten, 1 beaten, -1 no
// opponent there, or none for a cleared ticket
int ticket_flag(const Story& story, const OpponentTicket& ticket);
// flag_at != 0: a cleared ticket and pool 0's primary (GameState offset 0) both fail it
bool ticket_live(const RumbleTicket& ticket);

// OpponentQueue::clear, append and insert_at_ready
void queue_clear(OpponentQueue& queue);
void queue_append(OpponentQueue& queue, const RumbleTicket& ticket);
void queue_insert_at_ready(OpponentQueue& queue, const RumbleTicket& ticket);

// opponent_fits_tournament: level range, the rule's body class and leg kinds, its weight
// limits, no camouflage under No O.C.
bool opponent_fits_tournament(const StoryTables& tables, const RosterTables& roster, int entry,
                              int tournament);

// RumbleContext::fill_opponent_queues for area slot `slot`: the tournament settled
// (Burning Chrome's roll, the Aqualine ranker gone once won) into story.lineup[slot], the queues
// filled and every roster entry emptied
void fill_opponent_queues(World& world, const StoryTables& tables, const RosterTables& roster,
                          int slot, int tournament);
// rumble_pools_done
bool rumble_pools_done(const World& world, const StoryTables& tables, int slot);
// choose_opponent 0x459E8: a ticket from one of the three queues (rand_float draws), its queue + 1
bool choose_opponent(World& world, int slot, RumbleTicket& ticket, int& queue);
// materialize_actor 0x45637: the drawn opponent's build, pilot, level, pay into roster[index],
// with New Game+'s level bonus
void materialize_actor(World& world, const RosterTables& roster, int slot, int index,
                       const RumbleTicket& ticket, int queue);
// release_opponent 0x45D24: a fighter leaving hands its ticket back (to the beaten queue when the
// player shot it down), primaries whose alternates are done join, and with `fixed` the fixed
// opponent is queued
void release_opponent(World& world, const StoryTables& tables, const RosterTables& roster,
                      int slot, const RosterEntry& entry, bool killed_by_player, bool fixed);
// rumble_fixed_opponent_queue
void rumble_fixed_opponent_queue(World& world, const StoryTables& tables, int slot);

// tournaments_refill: the three area slots refilled for a day, five opponents drawn
void tournaments_refill(World& world, const StoryTables& tables, const RosterTables& roster,
                        int day);
// rumble_contexts_reset (game_state_task, after game_state_new): every context on
// tournament 0 with opponent entries 0..5 as its roster
void rumble_contexts_reset(World& world, const StoryTables& tables, const RosterTables& roster);
// a slot's roster filled with drawn AIs from index `first` on, -2 where none is left (the result
// screen's demo battle and tournaments_refill)
void rumble_roster_draw(World& world, const RosterTables& roster, int slot, int first);
// rumble_roster_put: the player's machine as slot `slot`'s roster entry 0 (player 0, pilot row 0,
// no pay, no ticket, queue 0), as the area select enters a tournament
void rumble_roster_put(World& world, const RosterTables& roster, int slot, const RosterBuild& build);
// PHANTOMCRASH_DEV_ENTRY=N (headless runs of a tournament fight and its replay, the game's and
// pc_sim's --state 6): loadout row N entered as the player's machine in the selected context
void rumble_dev_entry(World& world, const RosterTables& roster);
