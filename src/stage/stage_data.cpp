#include "stage/stage_data.hpp"

#include "assets/pack_sexp.hpp"
#include "cook/sexp_read.hpp"

#include <array>
#include <bit>
#include <map>

namespace {

// a float of the table as the world's fixed point (fx_bits on the float's bits: integer only)
Fx fx_atom(gsexp::Node node) {
    return fx_bits(std::bit_cast<std::uint32_t>(atom_float(node)));
}

FxVec3 vec_of(gsexp::Node list, int first) {
    return {fx_atom(list.child_at(first)), fx_atom(list.child_at(first + 1)),
            fx_atom(list.child_at(first + 2))};
}

void ints_of(gsexp::Node list, int* out, int count) {
    for (int i = 0; i < count; ++i) {
        out[i] = atom_int(list.child_at(1 + i));
    }
}

void read_props(gsexp::Node form, StagePropSet& set) {
    const gsexp::FormView view(form);
    for (gsexp::Node row : view.find("placements").children()) {
        if (!row.is_list() || !row.head().is_atom("prop")) {
            continue;
        }
        const gsexp::FormView prop(row);
        PropPlacement placement;
        placement.model = prop.get_int("model").value_or(-1);
        placement.alt_model = prop.get_int("alt").value_or(-1);
        placement.pos = vec_of(prop.find("pos"), 1);
        const gsexp::Node rot = prop.find("rot");
        for (int k = 0; k < 3; ++k) {
            placement.rot[k] = angle_from_radians(fx_atom(rot.child_at(1 + k)));
        }
        ints_of(prop.find("cell"), placement.cell, 3);
        placement.shape = prop.get_int("shape").value_or(0);
        placement.gate = prop.get_int("gate").value_or(-1);
        set.placements.push_back(placement);
    }
    for (gsexp::Node type : view.find("types").children()) {
        if (!type.is_list() && !type.is_atom("types")) {
            set.types.push_back(atom_int(type));
        }
    }
    for (gsexp::Node shape_form : view.find("shapes").children()) {
        if (!shape_form.is_list() || !shape_form.head().is_atom("shape")) {
            continue;
        }
        PropShape shape;
        for (int t = 0; t < 12; ++t) {
            const gsexp::Node tri = shape_form.child_at(1 + t);
            for (int v = 0; v < 3; ++v) {
                shape.v[t][v] = vec_of(tri, v * 3);
            }
        }
        set.shapes.push_back(shape);
    }
}

void floats_of(gsexp::Node form, float (*out)[3], int count) {
    for (int i = 0; i < count; ++i) {
        const gsexp::Node point = form.child_at(1 + i);
        for (int k = 0; k < 3; ++k) {
            out[i][k] = atom_float(point.child_at(k));
        }
    }
}

bool read_weights(StageData& data, const Pack& core, std::string& error) {
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, "tables/tournaments.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("tournament")) {
            continue;
        }
        const int id = form.get_int("id").value_or(-1);
        if (id < 0) {
            continue;
        }
        if (static_cast<int>(data.pickup_weights.size()) <= id) {
            data.pickup_weights.resize(id + 1, std::vector<Fx>(10));
        }
        // (pickup_weights (score_100 w) ... (refill_100 w)), in kind order
        const gsexp::Node weights = form.find("pickup_weights");
        for (int k = 0; k < 10; ++k) {
            data.pickup_weights[id][k] = fx_atom(weights.child_at(1 + k).child_at(1));
        }
    }
    gsexp::ParseResult days;
    if (!pack_sexp(core, "tables/day_schedule.sexp", days, error)) {
        return false;
    }
    for (std::size_t i = 0; i < days.root_count(); ++i) {
        const gsexp::FormView form(days.root(i));
        if (form.head().is_atom("day")) {
            std::array<int, 4> slots{};
            ints_of(form.find("tournament"), slots.data(), 4);
            data.day_tournaments.push_back(slots);
        }
    }
    return true;
}

} // namespace

bool load_stage_data(StageData& data, const Pack& core, std::string& error) {
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, "tables/stage_props.sexp", parsed, error)) {
        error += " (pc_extract --only stage-props)";
        return false;
    }
    std::map<unsigned, int> route_index; // address -> routes row
    struct ChoiceRoute {
        int row;
        unsigned address;
    };
    std::vector<ChoiceRoute> choice_routes;
    unsigned sky_addresses[8] = {};
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node node = parsed.root(i);
        const gsexp::FormView form(node);
        if (form.head().is_atom("prop-scores")) {
            ints_of(node, data.prop_scores, 9);
        } else if (form.head().is_atom("asset-sets")) {
            ints_of(form.find("props"), data.prop_assets, 8);
            ints_of(form.find("craft"), data.craft_assets, 2);
            ints_of(form.find("sky"), data.sky_assets, 2);
        } else if (form.head().is_atom("stage-props")) {
            const int selector = form.get_int("selector").value_or(-1);
            if (selector >= 0 && selector < 8) {
                read_props(node, data.props[selector]);
            }
        } else if (form.head().is_atom("route")) {
            route_index[atom_hex(form.find("address").child_at(1))] =
                static_cast<int>(data.routes.size());
            std::vector<RouteSegment> segments;
            for (gsexp::Node segment : node.children()) {
                if (!segment.is_list() || !segment.head().is_atom("segment")) {
                    continue;
                }
                RouteSegment row;
                for (int p = 0; p < 4; ++p) {
                    row.p[p] = vec_of(segment.child_at(1 + p), 0);
                }
                segments.push_back(row);
            }
            data.routes.push_back(segments);
        } else if (form.head().is_atom("route-counts")) {
            ints_of(node, data.route_counts, 8);
        } else if (form.head().is_atom("route-choice")) {
            const int row = form.get_int("row").value_or(-1);
            if (row < 0 || row >= 31) {
                continue;
            }
            RouteChoice& choice = data.choices[row];
            choice.count = form.get_int("count").value_or(0);
            choice.ingress = form.get_int("ingress").value_or(0);
            choice.release_start = form.get_int("release-start").value_or(0);
            choice.release_end = form.get_int("release-end").value_or(0);
            choice.height = fx_atom(form.find("height").child_at(1));
            choice_routes.push_back({row, atom_hex(form.find("route").child_at(1))});
        } else if (form.head().is_atom("ending-speeds")) {
            for (int k = 0; k < 7; ++k) {
                data.ending_speeds[k] = fx_atom(node.child_at(1 + k));
            }
        } else if (form.head().is_atom("sky-routes")) {
            int selector = 0;
            for (gsexp::Node sky : node.children()) {
                if (sky.is_list() && sky.head().is_atom("sky") && selector < 8) {
                    const gsexp::FormView row(sky);
                    sky_addresses[selector] = atom_hex(row.find("route").child_at(1));
                    data.sky_count[selector] = row.get_int("count").value_or(0);
                    ++selector;
                }
            }
        } else if (form.head().is_atom("craft-models")) {
            ints_of(form.find("body"), data.craft_body, 2);
            ints_of(form.find("wreck"), data.craft_wreck, 2);
            ints_of(form.find("rotor"), data.craft_rotor, 2);
            ints_of(form.find("hatch-a"), data.craft_hatch_a, 2);
            ints_of(form.find("hatch-b"), data.craft_hatch_b, 2);
        } else if (form.head().is_atom("crate-models")) {
            ints_of(node, &data.crate_models[0][0], 20);
        } else if (form.head().is_atom("sky-screen-textures")) {
            ints_of(node, data.sky_screen_textures, 6);
        } else if (form.head().is_atom("ambience-points")) {
            floats_of(node, data.ambience_points, 4);
        } else if (form.head().is_atom("moon-points")) {
            floats_of(node, data.moon_points, 4);
        } else if (form.head().is_atom("moon-textures")) {
            ints_of(node, data.moon_textures, 30);
        }
    }
    for (const ChoiceRoute& choice : choice_routes) {
        data.choices[choice.row].route = route_index[choice.address];
    }
    for (int selector = 0; selector < 8; ++selector) {
        data.sky_route[selector] = route_index[sky_addresses[selector]];
    }
    if (data.routes.empty() || data.props[0].placements.empty()) {
        error = "stage_props.sexp has no routes or props";
        return false;
    }
    return read_weights(data, core, error);
}

int stage_crate_tournament(const StageData& data, int selector, int day) {
    if (selector == 6 || selector == 7 || data.day_tournaments.empty()) {
        return 6;
    }
    const std::array<int, 4>& slots =
        data.day_tournaments[static_cast<std::size_t>(day) % data.day_tournaments.size()];
    return slots[selector / 2];
}
