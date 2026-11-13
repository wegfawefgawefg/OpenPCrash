#include "ai/ai_data.hpp"

#include "assets/pack_sexp.hpp"
#include "battle/sexp_fx.hpp"
#include "cook/cook_plan.hpp"
#include "cook/pack_library.hpp"

namespace {

// a field's k-th value: (name v0 v1 ...)
int value_at(const gsexp::FormView& form, const char* name, std::size_t k) {
    return atom_int(form.find(name).child_at(1 + k));
}

bool read_ai_tables(AiTables& t, const Pack& core, std::string& error) {
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, "tables/ai.sexp", parsed, error)) {
        return false;
    }
    int profile_rows = 0, timing_rows = 0;
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        const std::string_view head = form.head().text();
        const int id = form.get_int("id").value_or(-1);
        if (head == "ai_transition_probs_normal" || head == "ai_transition_probs_alternate") {
            auto& rows = head == "ai_transition_probs_normal" ? t.probs_normal : t.probs_alternate;
            if (id >= 0 && id < ai_prob_rows) {
                for (std::size_t k = 0; k < 5; ++k) {
                    rows[static_cast<std::size_t>(id)][k] = value_at(form, "thresholds", k);
                }
            }
        } else if (head == "ai_selection_profiles" && id >= 0 && id < ai_profiles) {
            for (std::size_t k = 0; k < 25; ++k) {
                t.selection[static_cast<std::size_t>(id)][k / 5][k % 5] =
                    value_at(form, "cells", k);
            }
            ++profile_rows;
        } else if (head == "ai_stat_bands" && id >= 0 && id < 20) {
            t.stat_bands[static_cast<std::size_t>(id)] = {form.get_int("boost").value_or(0),
                                                          form.get_int("jump").value_or(0),
                                                          form.get_int("dodge").value_or(0)};
        } else if (head == "ai_transition_result_map") {
            for (std::size_t k = 0; k < 5; ++k) {
                t.result_map[k] = value_at(form, "states", k);
            }
        } else if (head == "ai_level_timing_profiles") {
            const int row = form.get_int("level").value_or(-129) + 128;
            if (row >= 0 && row < 256) {
                for (std::size_t k = 0; k < 6; ++k) {
                    t.timing[static_cast<std::size_t>(row)][k] = value_at(form, "timers", k);
                }
                ++timing_rows;
            }
        } else if (head == "ai_search_move_weights") {
            const gsexp::Node weights = form.find("weights");
            for (std::size_t k = 1; k < weights.child_count(); ++k) {
                const int w = atom_int(weights.child_at(k));
                if (w == -1) {
                    break;
                }
                t.search_weights.push_back(w);
            }
        }
    }
    // past the last row: the timer table's neighbour holds 18000 like every unreal row
    t.timing[256] = {18000, 18000, 18000, 18000, 18000, 18000};
    if (profile_rows != ai_profiles || timing_rows != 256 || t.search_weights.empty()) {
        error = "tables/ai.sexp: missing rows (pc_extract --only tables)";
        return false;
    }
    return true;
}

bool read_weapons(std::vector<AiWeaponRecord>& rows, const Pack& core, const std::string& file,
                  std::string& error) {
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, "tables/" + file, parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (form.head().is_atom("weapon")) {
            rows.push_back({value_at(form, "behavior", 0), value_at(form, "lock_rule", 0)});
        }
    }
    return true;
}

bool read_roster_tables(AiData& data, const Pack& core, std::string& error) {
    gsexp::ParseResult opponents, pilots;
    if (!pack_sexp(core, "tables/opponents.sexp", opponents, error) ||
        !pack_sexp(core, "tables/pilots.sexp", pilots, error)) {
        return false;
    }
    for (std::size_t i = 0; i < opponents.root_count(); ++i) {
        const gsexp::FormView form(opponents.root(i));
        if (form.head().is_atom("opponent")) {
            data.opponents.push_back(
                {form.get_int("pilot").value_or(0), form.get_int("loadout").value_or(0),
                 form.get_int("ai_profile").value_or(4), form.get_int("level").value_or(0)});
        }
    }
    for (std::size_t i = 0; i < pilots.root_count(); ++i) {
        const gsexp::FormView form(pilots.root(i));
        if (form.head().is_atom("pilot")) {
            data.pilot_teams.push_back(form.get_int("team").value_or(-1));
        }
    }
    return true;
}

// the stage's ENPT graph: the one nav file its pack holds
bool read_nav(NavGraph& nav, PackLibrary& packs, int stage, std::string& error) {
    const Pack* pack = pack_open(packs, stage_pack_name(stage), error);
    if (!pack) {
        return false;
    }
    gsexp::ParseResult parsed;
    bool found = false;
    for (const char* name : {"nav/1687_b00_shinjuku.sexp", "nav/1688_b01_shibuya.sexp",
                             "nav/1689_b02_umihotaru.sexp", "nav/1690_choco_es_01_enepath.sexp"}) {
        std::string missing;
        if (pack_sexp(*pack, name, parsed, missing)) {
            found = true;
            break;
        }
    }
    if (!found) {
        error = stage_pack_name(stage) + ": no route graph (pc_extract --only nav, then cook)";
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node root = parsed.root(i);
        if (!root.is_list() || !root.head().is_atom("enpt")) {
            continue;
        }
        for (gsexp::Node child : root.children()) {
            if (!child.is_list() || !child.head().is_atom("node")) {
                continue;
            }
            const gsexp::FormView form(child);
            NavNode node;
            node.id = value_at(form, "id", 1);
            node.kind = form.get_int("kind").value_or(3);
            const gsexp::Node links = form.find("links");
            for (std::size_t k = 1; k < links.child_count(); ++k) {
                node.links.push_back(atom_int(links.child_at(k)));
            }
            node.segments = form.get_int("segments").value_or(0);
            const gsexp::Node points = form.find("points");
            for (std::size_t k = 1; k + 2 < points.child_count(); k += 3) {
                node.points.push_back({atom_fx(points.child_at(k)), atom_fx(points.child_at(k + 1)),
                                       atom_fx(points.child_at(k + 2))});
            }
            if (static_cast<int>(node.points.size()) != 1 + 3 * node.segments) {
                error = "route graph: node " + std::to_string(nav.nodes.size()) + "'s points";
                return false;
            }
            nav.nodes.push_back(std::move(node));
        }
    }
    if (nav.nodes.empty()) {
        error = "route graph: no nodes";
        return false;
    }
    return true;
}

} // namespace

bool load_nav_graph(NavGraph& nav, PackLibrary& packs, int stage, std::string& error) {
    nav = NavGraph{};
    return read_nav(nav, packs, stage, error);
}

bool load_ai_data(AiData& data, const Pack& core, PackLibrary& packs, int stage,
                  std::string& error) {
    return read_ai_tables(data.tables, core, error) &&
           read_weapons(data.arms, core, "arm_weapons.sexp", error) &&
           read_weapons(data.backs, core, "back_weapons.sexp", error) &&
           read_roster_tables(data, core, error) && read_nav(data.nav, packs, stage, error);
}
