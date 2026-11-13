#include "story/story_tables.hpp"

#include "cook/sexp_read.hpp"

#include <cmath>
#include <cstring>

namespace {

// the n-th number after a field's head: (pools 152 153 ...) -> child n + 1
int field_int(const gsexp::FormView& form, const char* head, int n = 0) {
    const gsexp::Node field = form.find(head);
    return field.valid() ? atom_int(field.child_at(static_cast<std::size_t>(n) + 1)) : 0;
}

// the predicate a slot's function name says (tools/extract/tables_flow.cpp predicate_name)
void read_predicate(TriggerRow& row, const std::string& name) {
    auto number_after = [&](const char* prefix) {
        return std::atoi(name.c_str() + std::strlen(prefix));
    };
    if (name == "none") {
        row.predicate = Predicate::None;
    } else if (name == "always") {
        row.predicate = Predicate::Always;
    } else if (name == "never") {
        row.predicate = Predicate::Never;
    } else if (name == "rule_1_to_4") {
        row.predicate = Predicate::Rule1To4;
    } else if (name.rfind("area", 0) == 0 && name.find("_ranker_today") != std::string::npos) {
        row.predicate = Predicate::AreaRankerToday;
        row.predicate_area = number_after("area");
    } else if (name == "ranker_bout_won") {
        row.predicate = Predicate::RankerBoutWon;
    } else if (name == "ranker_bout_unwon") {
        row.predicate = Predicate::RankerBoutUnwon;
    } else if (name == "ranker_bout_unwon_any") {
        row.predicate = Predicate::RankerBoutUnwonAny;
    } else if (name.rfind("story_ge_", 0) == 0) {
        row.predicate = Predicate::StoryAtLeast;
        row.predicate_count = number_after("story_ge_");
    } else if (name == "rule_23") {
        row.predicate = Predicate::Rule23;
    } else if (name == "class_wins_changed") {
        row.predicate = Predicate::ClassWinsChanged;
    } else if (name.rfind("songs_ge_", 0) == 0) {
        row.predicate = Predicate::SongsAtLeast;
        row.predicate_count = number_after("songs_ge_");
    } else if (name.rfind("class", 0) == 0 && name.find("_wins_ge_") != std::string::npos) {
        row.predicate = Predicate::ClassWinsAtLeast;
        row.predicate_area = number_after("class");
        row.predicate_count = std::atoi(name.c_str() + name.find("_wins_ge_") + 9);
    } else {
        row.predicate = Predicate::Never; // a name this port doesn't know never passes
    }
}

bool load_triggers(StoryTables& tables, const std::string& data_dir, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/tables/event_triggers.sexp", parsed, error)) {
        return false;
    }
    tables.triggers.assign(trigger_row_count, TriggerRow{});
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("trigger")) {
            continue;
        }
        const int id = form.get_int("id").value_or(-1);
        if (id < 0 || id >= trigger_row_count) {
            continue;
        }
        TriggerRow& row = tables.triggers[static_cast<std::size_t>(id)];
        row.state = field_int(form, "state");
        row.src = field_int(form, "src");
        row.dst = field_int(form, "dst");
        row.prereq_first = static_cast<std::int8_t>(field_int(form, "prereq_first"));
        row.prereq_last = static_cast<std::int8_t>(field_int(form, "prereq_last"));
        row.repeat = static_cast<std::int8_t>(field_int(form, "repeat"));
        row.predicate_slot = static_cast<std::uint8_t>(field_int(form, "predicate"));
        const gsexp::Node predicate = form.find("predicate");
        read_predicate(row, std::string(predicate.child_at(2).text()));
        row.tournament = static_cast<std::uint8_t>(field_int(form, "tournament"));
        row.stage = static_cast<std::uint8_t>(field_int(form, "stage"));
        row.rule = static_cast<std::uint8_t>(field_int(form, "rule"));
        row.night = static_cast<std::uint8_t>(field_int(form, "night"));
        row.enabled = static_cast<std::uint8_t>(field_int(form, "enabled"));
        row.background = static_cast<std::int8_t>(field_int(form, "background"));
        for (int k = 0; k < 6; ++k) {
            row.cast[k] = static_cast<std::int8_t>(field_int(form, "cast", k));
        }
        row.music = field_int(form, "music");
        row.volume_percent =
            static_cast<int>(std::lround(form.get_float("volume").value_or(1.0f) * 100.0f));
    }
    return true;
}

bool load_tournaments(StoryTables& tables, const std::string& data_dir, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/tables/tournaments.sexp", parsed, error)) {
        return false;
    }
    tables.tournaments.assign(tournament_count, TournamentRow{});
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        const int id = form.get_int("id").value_or(-1);
        if (!form.head().is_atom("tournament") || id < 0 || id >= tournament_count) {
            continue;
        }
        TournamentRow& row = tables.tournaments[static_cast<std::size_t>(id)];
        row.rule = field_int(form, "rule");
        row.night = field_int(form, "night");
        row.level_min = field_int(form, "level_min");
        row.level_max = field_int(form, "level_max");
        row.regular_quota = field_int(form, "regular_quota");
        row.max_opponents = field_int(form, "max_opponents");
        row.fixed_opponent = field_int(form, "fixed_opponent");
        row.reserve_pool = field_int(form, "reserve_pool");
        for (int k = 0; k < 8; ++k) {
            row.pools[k] = field_int(form, "pools", k);
        }
    }
    if (!parse_sexp_file(data_dir + "/tables/tournament_rules.sexp", parsed, error)) {
        return false;
    }
    tables.rules.assign(tournament_rule_count, RuleRow{});
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        const int id = form.get_int("id").value_or(-1);
        if (!form.head().is_atom("rule") || id < 0 || id >= tournament_rule_count) {
            continue;
        }
        RuleRow& rule = tables.rules[static_cast<std::size_t>(id)];
        rule.entry_fee = field_int(form, "entry_fee");
        rule.weight_min = atom_float(form.find("weight_min").child_at(1));
        rule.weight_max = atom_float(form.find("weight_max").child_at(1));
        rule.body_class = field_int(form, "body_class");
        for (int k = 0; k < 4; ++k) {
            rule.leg_classes[k] = field_int(form, "leg_classes", k);
        }
        rule.extra = field_int(form, "extra");
    }
    return true;
}

bool load_days_and_pools(StoryTables& tables, const std::string& data_dir, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/tables/day_schedule.sexp", parsed, error)) {
        return false;
    }
    tables.days.assign(day_count, DayRow{});
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        const int id = form.get_int("id").value_or(-1);
        if (!form.head().is_atom("day") || id < 0 || id >= day_count) {
            continue;
        }
        DayRow& row = tables.days[static_cast<std::size_t>(id)];
        for (int k = 0; k < 4; ++k) {
            row.tournament[k] = field_int(form, "tournament", k);
        }
        for (int k = 0; k < 3; ++k) {
            row.dealer[k] = field_int(form, "dealer", k);
        }
        row.body = field_int(form, "body");
        row.leg = field_int(form, "leg");
        row.arm = field_int(form, "arm");
        row.back = field_int(form, "back");
        row.option = field_int(form, "option");
        row.color = field_int(form, "color");
        row.sticker = field_int(form, "sticker");
        row.scoobee = field_int(form, "scoobee");
    }
    if (!parse_sexp_file(data_dir + "/tables/opponent_pools.sexp", parsed, error)) {
        return false;
    }
    tables.pools.assign(pool_count, PoolRow{});
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        const int id = form.get_int("id").value_or(-1);
        if (!form.head().is_atom("pool") || id < 0 || id >= pool_count) {
            continue;
        }
        PoolRow& row = tables.pools[static_cast<std::size_t>(id)];
        row.primary = field_int(form, "primary");
        for (int k = 0; k < 8; ++k) {
            row.alternates[k] = field_int(form, "alternates", k);
        }
    }
    if (!parse_sexp_file(data_dir + "/tables/tournament_groups.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (form.head().is_atom("class_tournaments")) {
            const int stage = form.get_int("stage").value_or(-1);
            if (stage >= 0 && stage < 3) {
                for (int k = 0; k < 8; ++k) {
                    tables.class_tournaments[stage][k] = field_int(form, "tournaments", k);
                }
            }
        } else if (form.head().is_atom("area_ranker_tournaments")) {
            for (int k = 0; k < 3; ++k) {
                tables.area_ranker_tournaments[k] = field_int(form, "tournaments", k);
            }
        } else if (form.head().is_atom("first_ranker_tournaments")) {
            tables.first_ranker_tournament = field_int(form, "tournaments");
        }
    }
    return true;
}

} // namespace

bool load_story_tables(StoryTables& tables, const std::string& data_dir, std::string& error) {
    return load_triggers(tables, data_dir, error) && load_tournaments(tables, data_dir, error) &&
           load_days_and_pools(tables, data_dir, error);
}

// decomp: event_policy_row_of_state 0xD109B
int trigger_row_of_state(const StoryTables& tables, int state) {
    for (int i = 0; i < trigger_row_count; ++i) {
        if (tables.triggers[static_cast<std::size_t>(i)].state == state) {
            return i;
        }
    }
    return trigger_row_count;
}
