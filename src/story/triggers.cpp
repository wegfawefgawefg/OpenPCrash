// The trigger table: which conversation plays on a state change (decomp
// docs/port/progression.md "When a conversation plays"; app_transition_resolve,
// event_trigger_find and the predicates in u_0d0e28.c).
#include "story/story.hpp"

namespace {

// a byte field of the row read as a signed char, as the resolver reads it
int as_char(std::uint8_t value) {
    return static_cast<std::int8_t>(value);
}

void event_done_set(Story& story, int row) {
    story.events_done[row / 32] |= 1u << (row % 32);
}

// today's tournament (in the selected slot) is an Area Ranker bout
// decomp: trigger_rule_22 0xD1366
bool today_rule_is(const Story& story, const StoryTables& tables, int rule) {
    return tournament_rule(tables, game_day_tournament(story, tables)) == rule;
}

// decomp: trigger_area_ranker_wins_changed 0xD1379
bool ranker_wins_changed(const Story& story, const StoryTables& tables) {
    return area_ranker_wins(story, tables, day_slot_of_selector(story)) !=
           story.ranker_wins_at_entry;
}

// event_predicates[slot](stage): every predicate ignores the argument
bool predicate_holds(const Story& story, const StoryTables& tables, const TriggerRow& row) {
    switch (row.predicate) {
    case Predicate::None:
    case Predicate::Always:
        return true;
    case Predicate::Never:
        return false;
    case Predicate::Rule1To4: {
        const int rule = tournament_rule(tables, game_day_tournament(story, tables));
        return rule > 0 && rule <= 4;
    }
    case Predicate::AreaRankerToday: {
        // that slot's tournament today, not the selected one (trigger_areaN_ranker_today)
        const int tournament = game_tournament_slot(story, tables, story.day, row.predicate_area);
        if (tournament < 0 || tournament_rule(tables, tournament) != rule_area_ranker) {
            return false;
        }
        return !area_ranker_beaten(story, tables, row.predicate_area);
    }
    case Predicate::RankerBoutWon:
        return today_rule_is(story, tables, rule_area_ranker) && ranker_wins_changed(story, tables);
    case Predicate::RankerBoutUnwon:
        if (area_ranker_beaten(story, tables, day_slot_of_selector(story))) {
            return false;
        }
        return today_rule_is(story, tables, rule_area_ranker) &&
               !ranker_wins_changed(story, tables);
    case Predicate::RankerBoutUnwonAny:
        return today_rule_is(story, tables, rule_area_ranker) &&
               !ranker_wins_changed(story, tables);
    case Predicate::StoryAtLeast:
        return story_progress(story, tables) >= row.predicate_count;
    case Predicate::Rule23:
        return today_rule_is(story, tables, rule_first_ranker);
    case Predicate::ClassWinsChanged:
        return class_wins(story, tables, day_slot_of_selector(story)) != story.class_wins_at_entry;
    case Predicate::SongsAtLeast:
        return music_owned_count(story) >= row.predicate_count;
    case Predicate::ClassWinsAtLeast:
        return class_wins(story, tables, row.predicate_area) >= row.predicate_count;
    }
    return false;
}

} // namespace

// decomp: event_done 0xD140C
bool event_done(const Story& story, int row) {
    if (row < 0 || row >= 160) {
        return false;
    }
    return (story.events_done[row / 32] >> (row % 32) & 1u) != 0;
}

// decomp: event_trigger_find 0xD17C1
int event_trigger_find(const Story& story, const StoryTables& tables, int src, int dst, int& out) {
    int found = trigger_row_count;
    for (int i = 0; i < trigger_row_count; ++i) {
        const TriggerRow& row = tables.triggers[static_cast<std::size_t>(i)];

        // 1. enabled; 2. the pair asked for, both in 0..0x9E
        if (row.enabled == 0) {
            continue;
        }
        if (row.src < 0 || row.src >= no_state || row.src != src || row.dst < 0 ||
            row.dst >= no_state || row.dst != dst) {
            continue;
        }

        // 3. the prerequisite rows first..last all done. Retail swaps them when first < last, the
        // wrong way round, so a real range tests nothing; no shipped row has one (docs/bugs.md)
        int first = row.prereq_first;
        int last = row.prereq_last;
        bool ready = true;
        if (first != 0 && last != 0) {
            if (first > last) {
                const int swap = first;
                first = last;
                last = swap;
            }
            for (int k = first; k <= last; ++k) {
                if (!event_done(story, k)) {
                    ready = false;
                    break;
                }
            }
        }

        // 4. its own done bit clear; 5. the predicate (slot 0..74, else none)
        if (!ready || event_done(story, i)) {
            continue;
        }
        const int slot = as_char(row.predicate_slot);
        if (slot >= 0 && slot < predicate_slot_count && !predicate_holds(story, tables, row)) {
            continue;
        }

        // 6. today's tournament in the selected slot, after the 49 -> 52 swap
        const int tournament = as_char(row.tournament);
        if (tournament < tournament_count && tournament >= 0 &&
            game_day_tournament(story, tables) != tournament) {
            continue;
        }

        // 7. the area slot of the last tournament picked
        const int stage = as_char(row.stage);
        if (stage < 4 && stage >= 0 && stage != story.rumble_selected / 2) {
            continue;
        }

        // 8. that day tournament's rule
        const int rule = as_char(row.rule);
        if (rule < tournament_rule_count && rule >= 0 &&
            tournament_rule(tables, game_day_tournament(story, tables)) != rule) {
            continue;
        }

        // 9. day or night of the last tournament picked
        const int night = as_char(row.night);
        if (night < 2 && night >= 0 && night != story.rumble_selected % 2) {
            continue;
        }

        found = i;
        dst = row.state;
        break;
    }
    out = dst;
    return found;
}

// decomp: state_is_trigger_talk 0xD1650
bool state_is_trigger_talk(const StoryTables& tables, int state) {
    const int row = trigger_row_of_state(tables, state);
    return row < trigger_row_count && tables.triggers[static_cast<std::size_t>(row)].enabled != 0;
}

// decomp: app_transition_resolve 0xA1BCC
int app_transition_resolve(Story& story, const StoryTables& tables, int current, int dst) {
    // from a trigger talk, resume the transition it interrupted: so every row that holds for one
    // transition plays back to back, in table order, each seeing the bits the earlier ones set
    const bool from_talk = state_is_trigger_talk(tables, current);
    int src = current;
    if (from_talk) {
        src = story.talk_src;
        dst = story.talk_dst;
    }
    int talk = dst;
    const int row = event_trigger_find(story, tables, src, dst, talk);
    if (row != trigger_row_count) {
        // the first row of a chain saves the transition it interrupts
        if (!from_talk) {
            story.talk_src = current;
            story.talk_dst = dst;
        }
        event_done_set(story, row);
        return talk;
    }
    if (from_talk) {
        // the chain is over: clear the rows marked repeatable (14 and 38, "you lost to the Area
        // Ranker") so they play again after the next lost bout. Retail meant this but tests
        // dst == 0x9F after dst was already replaced by talk_dst, so it never runs and they
        // play once (docs/bugs.md). No row names 14 or 38 as a prerequisite.
        for (int i = 0; i < trigger_row_count; ++i) {
            if (tables.triggers[static_cast<std::size_t>(i)].repeat == -1) {
                story.events_done[i / 32] &= ~(1u << (i % 32));
            }
        }
    }
    return dst;
}

// decomp: event_done_count 0xD1428
int event_done_count(const Story& story, const StoryTables& tables) {
    int count = 0;
    for (int i = 0; i < trigger_row_count; ++i) {
        if (tables.triggers[static_cast<std::size_t>(i)].repeat != -1 && event_done(story, i)) {
            ++count;
        }
    }
    return count;
}

// decomp: event_once_count 0xD1457
int event_once_count(const StoryTables& tables) {
    int count = 0;
    for (const TriggerRow& row : tables.triggers) {
        if (row.repeat != -1) {
            ++count;
        }
    }
    return count;
}
