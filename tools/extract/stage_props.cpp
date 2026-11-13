#include "stage_props.hpp"

#include "disc.hpp"
#include "extract.hpp"
#include "sexp_out.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>

namespace {

// where the tables sit in the retail XBE (decomp src/data/stage_props.c, stage_routes.c,
// stage_scene.c, ui_c.c, data/symbols.txt)
constexpr std::uint32_t prop_placements = 0x22E550;   // D_0022E550 StageProp *[8]
constexpr std::uint32_t prop_shapes = 0x22E570;       // D_0022E570 PropShape *[8]
constexpr std::uint32_t prop_types = 0x22E700;        // D_0022E700 int *[8]
constexpr std::uint32_t prop_scores = 0x203654;       // D_00203654 int[8], read up to [8]
constexpr std::uint32_t prop_model_assets = 0x22E278; // D_0022E278 int[8]
constexpr std::uint32_t craft_model_assets = 0x22E298; // D_0022E298 int[2] by night
constexpr std::uint32_t sky_model_assets = 0x22E2A0;   // D_0022E2A0 int[2] by night
constexpr std::uint32_t route_choices = 0x21C2E8;      // stage_route_choices StageRoute[31]
constexpr std::uint32_t route_counts = 0x21C64C;       // stage_route_counts int[8]
constexpr std::uint32_t ending_speeds = 0x21C66C;      // D_0021C66C float[7]
constexpr std::uint32_t sky_routes = 0x21C688;         // D_0021C688 RouteSegment *[8]
constexpr std::uint32_t sky_route_counts = 0x21C6A8;   // D_0021C6A8 int[8]
constexpr std::uint32_t craft_models = 0x21C1B8;   // D_0021C1B8..D_0021C1D8, five {day, night}
constexpr std::uint32_t crate_models = 0x21C298;   // D_0021C298 int[10][2]
constexpr std::uint32_t sky_screen_textures = 0x21C280; // int[6]
constexpr std::uint32_t ambience_points = 0x221274;     // float[4][3]
constexpr std::uint32_t moon_points = 0x2212A4;         // D_002212A4 float[4][3]
constexpr std::uint32_t moon_textures = 0x2212D8;       // D_002212D8 int[30]
constexpr std::uint32_t rand_table = 0x201578;          // int[0x7FF]
constexpr int rand_table_size = 0x7FF;
// PropShape rows per selector: the length of each prop_states_N array (stage_props.c), which
// the shape tables match
constexpr int shape_counts[8] = {16, 16, 12, 12, 12, 12, 6, 6};

// a point in the port frame (z negated, docs/assets.md)
std::string point(const Xbe& xbe, std::uint32_t at) {
    const float z = xbe_f32(xbe, at + 8);
    return sx_float(xbe_f32(xbe, at)) + " " + sx_float(xbe_f32(xbe, at + 4)) + " " +
           sx_float(z == 0.0f ? 0.0f : -z);
}

std::string ints(const Xbe& xbe, std::uint32_t at, int count) {
    std::string text;
    for (int i = 0; i < count; ++i) {
        text += (i ? " " : "") + sx_int(xbe_i32(xbe, at + i * 4));
    }
    return text;
}

bool write_text(const ExtractOptions& options, const std::string& file, const std::string& text) {
    std::error_code made;
    std::filesystem::create_directories(options.out_dir + "/tables", made);
    std::ofstream out(options.out_dir + "/" + file, std::ios::binary);
    out << text;
    if (!out) {
        std::fprintf(stderr, "stage-props: can't write %s\n", file.c_str());
        return false;
    }
    return true;
}

// a route's Bezier segments, four points each (RouteSegment, 0x30). The craft reads one segment
// past its count (the advance tests segment > count), so count + 1 are written
std::string route_segments(const Xbe& xbe, std::uint32_t at, int count) {
    std::string text;
    for (int s = 0; s <= count; ++s) {
        text += "\n    (segment";
        for (int p = 0; p < 4; ++p) {
            text += " (" + point(xbe, at + s * 0x30 + p * 12) + ")";
        }
        text += ")";
    }
    return text;
}

} // namespace

bool extract_stage_props(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    const Xbe& xbe = disc.xbe;
    std::string text =
        ";; stage_props: the stage's props, the supply craft's and the sky craft's routes, the\n"
        ";; crates' and craft's models, the ambience points and the night sky (decomp\n"
        ";; src/data/stage_props.c, stage_routes.c, stage_scene.c). Port frame: z negated, prop\n"
        ";; rotations (-x, -y, z), cell z negated; shape triangles keep their vertex order (the\n"
        ";; game takes the normal as (v2 - v0) x (v1 - v0) so it is the original's mirrored).\n";

    // score per prop type (the shot's hit path reads D_00203654[type], type 8 included)
    text += "(prop-scores " + ints(xbe, prop_scores, 9) + ")  ; D_00203654\n";
    text += "(asset-sets (props " + ints(xbe, prop_model_assets, 8) + ") (craft " +
            ints(xbe, craft_model_assets, 2) + ") (sky " + ints(xbe, sky_model_assets, 2) +
            "))  ; D_0022E278, D_0022E298, D_0022E2A0 (stage_props_enter 0x6EDA0)\n";

    for (int selector = 0; selector < 8; ++selector) {
        text += "(stage-props (selector " + sx_int(selector) + ")\n";
        // StageProp rows, 0x34, ended by model -1
        const std::uint32_t rows = xbe_u32(xbe, prop_placements + selector * 4);
        text += "  (placements  ; D_0022E550 rows: model alt pos rot cell shape gate\n";
        for (int row = 0; row < 100; ++row) {
            const std::uint32_t at = rows + row * 0x34;
            const int model = xbe_i32(xbe, at);
            if (model == -1) {
                break;
            }
            const float rx = xbe_f32(xbe, at + 0x14), ry = xbe_f32(xbe, at + 0x18);
            text += "    (prop (model " + sx_int(model) + ") (alt " +
                    sx_int(xbe_i32(xbe, at + 4)) + ") (pos " + point(xbe, at + 8) + ") (rot " +
                    sx_float(rx == 0.0f ? 0.0f : -rx) + " " + sx_float(ry == 0.0f ? 0.0f : -ry) +
                    " " + sx_float(xbe_f32(xbe, at + 0x1C)) + ") (cell " +
                    sx_int(xbe_i32(xbe, at + 0x20)) + " " + sx_int(xbe_i32(xbe, at + 0x24)) +
                    " " + sx_int(-xbe_i32(xbe, at + 0x28)) + ") (shape " +
                    sx_int(xbe_i32(xbe, at + 0x2C)) + ") (gate " +
                    sx_int(xbe_i32(xbe, at + 0x30)) + "))\n";
        }
        text += "  )\n";
        // behaviour per shape row, then the shapes: twelve triangles of three points
        text += "  (types " + ints(xbe, xbe_u32(xbe, prop_types + selector * 4),
                                  shape_counts[selector]) +
                ")  ; D_0022E700\n";
        const std::uint32_t shapes = xbe_u32(xbe, prop_shapes + selector * 4);
        text += "  (shapes  ; D_0022E570, twelve triangles v0 v1 v2 each\n";
        for (int shape = 0; shape < shape_counts[selector]; ++shape) {
            text += "    (shape";
            for (int tri = 0; tri < 12; ++tri) {
                const std::uint32_t at = shapes + shape * 0x1B0 + tri * 0x24;
                text += "\n      (" + point(xbe, at) + " " + point(xbe, at + 12) + " " +
                        point(xbe, at + 24) + ")";
            }
            text += ")\n";
        }
        text += "  ))\n";
    }

    // the supply craft's routes: unique segment arrays by address, read as far as any row reads
    std::map<std::uint32_t, int> routes; // address -> segments - 1 read
    for (int row = 0; row < 31; ++row) {
        const std::uint32_t at = route_choices + row * 0x1C;
        int& count = routes[xbe_u32(xbe, at)];
        count = std::max(count, xbe_i32(xbe, at + 4));
    }
    for (int selector = 0; selector < 8; ++selector) {
        int& count = routes[xbe_u32(xbe, sky_routes + selector * 4)];
        count = std::max(count, xbe_i32(xbe, sky_route_counts + selector * 4));
    }
    for (const auto& [address, count] : routes) {
        text += "(route (address " + sx_hex(address) + ")" + route_segments(xbe, address, count) +
                ")\n";
    }
    text += "(route-counts " + ints(xbe, route_counts, 8) + ")  ; stage_route_counts\n";
    text += ";; stage_route_choices: row selector + choice * 8; rows 24.. by index for the ending\n";
    for (int row = 0; row < 31; ++row) {
        const std::uint32_t at = route_choices + row * 0x1C;
        text += "(route-choice (row " + sx_int(row) + ") (route " + sx_hex(xbe_u32(xbe, at)) +
                ") (count " + sx_int(xbe_i32(xbe, at + 4)) + ") (ingress " +
                sx_int(xbe_i32(xbe, at + 8)) + ") (release-start " +
                sx_int(xbe_i32(xbe, at + 12)) + ") (release-end " +
                sx_int(xbe_i32(xbe, at + 20)) + ") (height " + sx_float(xbe_f32(xbe, at + 24)) +
                "))\n";
    }
    text += "(ending-speeds";
    for (int i = 0; i < 7; ++i) {
        text += " " + sx_float(xbe_f32(xbe, ending_speeds + i * 4));
    }
    text += ")  ; D_0021C66C\n";
    text += "(sky-routes  ; D_0021C688, D_0021C6A8 by selector: route, last segment\n";
    for (int selector = 0; selector < 8; ++selector) {
        text += "  (sky (route " + sx_hex(xbe_u32(xbe, sky_routes + selector * 4)) + ") (count " +
                sx_int(xbe_i32(xbe, sky_route_counts + selector * 4)) + "))\n";
    }
    text += ")\n";

    // model slots by {day, night}: body, wreck, rotor, hatch doors; crates by kind
    text += "(craft-models (body " + ints(xbe, craft_models, 2) + ") (wreck " +
            ints(xbe, craft_models + 8, 2) + ") (rotor " + ints(xbe, craft_models + 16, 2) +
            ") (hatch-a " + ints(xbe, craft_models + 24, 2) + ") (hatch-b " +
            ints(xbe, craft_models + 32, 2) + "))  ; D_0021C1B8..D_0021C1D8\n";
    text += "(crate-models " + ints(xbe, crate_models, 20) + ")  ; D_0021C298 [kind][night]\n";
    text += "(sky-screen-textures " + ints(xbe, sky_screen_textures, 6) + ")\n";
    text += "(ambience-points";
    for (int i = 0; i < 4; ++i) {
        text += " (" + point(xbe, ambience_points + i * 12) + ")";
    }
    text += ")  ; ambience_points 0x221274\n";
    text += "(moon-points";
    for (int i = 0; i < 4; ++i) {
        text += " (" + point(xbe, moon_points + i * 12) + ")";
    }
    text += ")  ; D_002212A4, by stage\n";
    text += "(moon-textures " + ints(xbe, moon_textures, 30) + ")  ; D_002212D8, by day % 30\n";
    if (!write_text(options, "tables/stage_props.sexp", text)) {
        return false;
    }

    // the gameplay random table rand_table_next walks
    std::string table = ";; rand_table 0x201578: the 0x7FF values rand_table_next 0x44685 steps "
                        "through (docs/port/architecture.md, \"Randomness\")\n(rand-table";
    for (int i = 0; i < rand_table_size; ++i) {
        table += (i % 16 ? " " : "\n  ") + sx_int(xbe_i32(xbe, rand_table + i * 4));
    }
    table += ")\n";
    if (!write_text(options, "tables/rand_table.sexp", table)) {
        return false;
    }
    manifest.entries.push_back(
        "(table (name stage-props) (file \"tables/stage_props.sexp\") (xbe D_0022E550 0x22e550 "
        "stage_route_choices 0x21c2e8))");
    manifest.entries.push_back(
        "(table (name rand-table) (file \"tables/rand_table.sexp\") (xbe rand_table 0x201578))");
    std::printf("stage-props: 8 selectors, %zu routes, rand table\n", routes.size());
    return true;
}
