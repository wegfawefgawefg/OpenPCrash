#include "effects.hpp"

#include "disc.hpp"
#include "extract.hpp"
#include "sexp_out.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace {

// where the tables sit in the retail XBE (decomp data/symbols.txt)
constexpr std::uint32_t effect_lists = 0x215848; // EffectEmitter *[62]
constexpr std::uint32_t effect_names = 0x20C8B0; // char *[62], read by nothing
constexpr int effect_count = 62;
constexpr std::uint32_t effect_sets = 0x205B30; // {int *lists; int *kinds}[79] (effect_sets_kinds)
constexpr int effect_set_count = 79;
constexpr std::uint32_t sprite_kinds = 0x215D68; // sprite_fx_kinds DebrisKind[4]
constexpr int sprite_kind_count = 4;
constexpr std::uint32_t flare_cells = 0x22E370; // D_0022E370 float[12][4]
constexpr int flare_cell_count = 12;
constexpr std::uint32_t flare_pieces = 0x22E430; // D_0022E430 FlareElement[], ended by cell -1
// impact_fx.rows: scale, effect set, mark; 13 rows (tables.h has 11: rows 11 and 12 are the
// words after it, the melee rows) where the tables family writes 11
constexpr std::uint32_t impact_rows = 0x253320;
constexpr int impact_count = 13;
// foot_landing_sets 0x242AF0: the set a foot's landing plays, by motion state and leg group
constexpr std::uint32_t foot_landing_sets = 0x242AF0;
constexpr int foot_landing_states = 15;
// shot_model_draw 0x9829E's rows (shot_model_sprites + 0x18, 0x2C each) by WeaponRecord
// .shot_model: +0 billboarded, +0xC model, +0x10 quad (no model), +0x14 a flame and trail, +0x18
// scale, +0x1C the flame's z, +0x20 its scale
constexpr std::uint32_t shot_looks = 0x2533C0;
constexpr int shot_look_count = 18;
// arc_restart_delay_max 0x2325F8: float[4][4] by arc side, then the arc; the most updates a
// broken arm's arc waits before it lights again (effect_arc_emission's callers)
constexpr std::uint32_t arc_delays = 0x2325F8;
// actor_decals_update 0x88254's lamp glows: D_00242BFC [body class][2] models, D_00242C34 [lamp]
// the column each lamp takes
constexpr std::uint32_t lamp_glow_models = 0x242BFC;
constexpr std::uint32_t lamp_glow_columns = 0x242C34;

// ParticleRecord (include/tables.h, 0x150 bytes): fields in order, by offset and kind
enum class Kind { f32, i32, u8, i16 };
struct Field {
    const char* name;
    std::uint32_t offset;
    Kind kind;
    int count;
};
constexpr Field record_fields[] = {
    {"forces", 0x00, Kind::f32, 6},
    {"draw_flag", 0x18, Kind::i32, 1},
    {"gravity_off", 0x1C, Kind::u8, 1},
    {"gravity", 0x20, Kind::f32, 4},
    {"force_a_off", 0x30, Kind::u8, 1},
    {"force_a", 0x34, Kind::f32, 1},
    {"force_a_rot", 0x38, Kind::f32, 3},
    {"force_b_off", 0x44, Kind::u8, 1},
    {"force_b", 0x48, Kind::f32, 1},
    {"force_b_radial", 0x4C, Kind::u8, 1},
    {"force_b_range", 0x50, Kind::f32, 1},
    {"force_b_rot", 0x54, Kind::f32, 3},
    {"offset", 0x60, Kind::f32, 3},
    {"color_time", 0x6C, Kind::i32, 1},
    {"alpha_time", 0x70, Kind::i32, 1},
    {"life", 0x74, Kind::f32, 1},
    {"life_jitter", 0x78, Kind::f32, 1},
    {"mass", 0x7C, Kind::f32, 1},
    {"mass_time", 0x80, Kind::i32, 1},
    {"scale", 0x88, Kind::f32, 1},
    {"scale_time", 0x8C, Kind::i32, 1},
    {"scale_jitter", 0x90, Kind::f32, 1},
    {"speed_min", 0x94, Kind::f32, 1},
    {"speed_min_time", 0x98, Kind::i32, 1}, // speed_limits[1], read as an int
    {"speed_max", 0x9C, Kind::f32, 1},
    {"speed_max_time", 0xA0, Kind::i32, 1},
    {"color", 0xA4, Kind::f32, 4},
    {"hsv_spread", 0xB4, Kind::f32, 3},
    {"alpha_bias", 0xC0, Kind::f32, 1},
    {"shape", 0xC4, Kind::i32, 1},
    {"shape_kind", 0xC8, Kind::i32, 1},
    {"shape_axis", 0xCC, Kind::i32, 1},
    {"emitter_setup", 0xD0, Kind::f32, 11},
    {"spread_deg", 0xFC, Kind::f32, 1},
    {"spread_jitter", 0x100, Kind::f32, 1},
    {"speed", 0x104, Kind::f32, 1},
    {"speed_jitter", 0x108, Kind::f32, 1},
    {"burst_radius", 0x110, Kind::f32, 1},
    {"texture", 0x114, Kind::i16, 1},
    {"frame_count", 0x116, Kind::i16, 1},
};
// the curve pointers from +0x11C: IntKey lists for the four colour bytes, FloatKey for the rest
constexpr const char* curve_names[12] = {"color_r",       "color_g",         "color_b",
                                         "alpha",         "scale_curve",     "force_b_curve",
                                         "force_a_curve", "rate_curve",      "mass_curve",
                                         "speed_curve",   "speed_min_curve", "speed_max_curve"};

std::string value_text(const Xbe& xbe, std::uint32_t at, Kind kind) {
    switch (kind) {
    case Kind::f32:
        return sx_float(xbe_f32(xbe, at));
    case Kind::i32:
        return sx_int(xbe_i32(xbe, at));
    case Kind::u8:
        return sx_int(xbe_u8(xbe, at));
    case Kind::i16:
        return sx_int(xbe_i16(xbe, at));
    }
    return "0";
}

// a key list to its time -1 key, which is written too
std::string curve_text(const Xbe& xbe, const char* name, std::uint32_t at, bool ints) {
    std::string text = std::string("(") + name;
    if (at == 0) {
        return text + " nil)";
    }
    for (int k = 0; k < 64; ++k) {
        const std::uint32_t key = at + static_cast<std::uint32_t>(k) * 8;
        const float time = xbe_f32(xbe, key);
        text += " (" + sx_float(time) + " " +
                (ints ? sx_int(xbe_i32(xbe, key + 4)) : sx_float(xbe_f32(xbe, key + 4))) + ")";
        if (time < 0.0f) {
            break;
        }
    }
    return text + ")";
}

std::string record_text(const Xbe& xbe, int id, std::uint32_t at) {
    std::string text = "(particle (id " + sx_int(id) + ") (xbe " + sx_hex(at) + ")";
    for (const Field& field : record_fields) {
        text += "\n  (" + std::string(field.name);
        const std::uint32_t size = field.kind == Kind::i16 ? 2 : field.kind == Kind::u8 ? 1 : 4;
        for (int i = 0; i < field.count; ++i) {
            text += " " + value_text(xbe, at + field.offset + size * static_cast<std::uint32_t>(i),
                                     field.kind);
        }
        text += ")";
    }
    for (int c = 0; c < 12; ++c) {
        const std::uint32_t pointer = xbe_u32(xbe, at + 0x11C + static_cast<std::uint32_t>(c) * 4);
        text += "\n  " + curve_text(xbe, curve_names[c], pointer, c < 4);
    }
    return text + ")\n";
}

} // namespace

bool extract_effects(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    const Xbe& xbe = disc.xbe;

    // the records every list points at, numbered by address
    std::set<std::uint32_t> addresses;
    for (int e = 0; e < effect_count; ++e) {
        const std::uint32_t rows = xbe_u32(xbe, effect_lists + static_cast<std::uint32_t>(e) * 4);
        for (int r = 0; r < 32; ++r) {
            const std::uint32_t row = rows + static_cast<std::uint32_t>(r) * 8;
            if (xbe_u32(xbe, row) & 0x200000) {
                break;
            }
            if (xbe_u32(xbe, row + 4)) {
                addresses.insert(xbe_u32(xbe, row + 4));
            }
        }
    }
    std::map<std::uint32_t, int> record_id;
    for (std::uint32_t at : addresses) {
        const int id = static_cast<int>(record_id.size());
        record_id[at] = id;
    }

    std::string text =
        ";; effects: the particle effects' data (decomp docs/port/effects.md, include/tables.h\n"
        ";; ParticleRecord, src/data/particles.c, ui_d.c, stage_routes.c, stage_props.c), in the\n"
        ";; original's frame. Written by pc_extract (effects).\n"
        ";; particle: a ParticleRecord, numbered by address; curves are (time value) keys to the\n"
        ";;   time -1 key, nil for none (IntKey values for the colour bytes; color_r writes byte "
        "0,\n"
        ";;   which draws as blue, and color_b byte 2).\n"
        ";; effect: a row of effect_lists 0x215848: (emitter flags particle) rows up to the "
        "0x200000\n"
        ";;   row; its name from effect_names 0x20C8B0.\n"
        ";; effect-set: a row of effect_sets 0x205B30: effect ids (ended by -1) and sprite effect\n"
        ";;   kinds (ended by -2).\n"
        ";; sprite-kind: sprite_fx_kinds 0x215D68. flare-cell: D_0022E370 left top right bottom\n"
        ";;   texels. flare-piece: D_0022E430 cell, along, scale, RGB.\n"
        ";; impact-fx: impact_fx.rows 0x253320 by WeaponRecord.impact_fx, all 13: the set, its "
        "sprite\n"
        ";;   scale, mark 1 for a scorch mark too.\n"
        ";; foot-landing: foot_landing_sets 0x242AF0 by motion state: the set for leg groups 0, 1, "
        "2\n"
        ";;   (-1 none).\n"
        ";; shot-look: shot_model_draw's rows 0x2533C0 by WeaponRecord.shot_model.\n"
        ";; arc-delays: arc_restart_delay_max 0x2325F8, [arc side][arc], updates.\n"
        ";; lamp-glow-models: D_00242BFC [body class][column]; lamp-glow-columns: D_00242C34 by "
        "lamp.\n";
    for (const auto& [at, id] : record_id) {
        text += record_text(xbe, id, at);
    }
    for (int e = 0; e < effect_count; ++e) {
        const std::uint32_t rows = xbe_u32(xbe, effect_lists + static_cast<std::uint32_t>(e) * 4);
        const std::string name =
            xbe_cstring(xbe, xbe_u32(xbe, effect_names + static_cast<std::uint32_t>(e) * 4));
        text += "(effect (id " + sx_int(e) + ") (name " + sx_string(name) + ")";
        for (int r = 0; r < 32; ++r) {
            const std::uint32_t row = rows + static_cast<std::uint32_t>(r) * 8;
            const std::uint32_t flags = xbe_u32(xbe, row);
            if (flags & 0x200000) {
                break;
            }
            const auto found = record_id.find(xbe_u32(xbe, row + 4));
            text += " (emitter " + sx_hex(flags) + " " +
                    sx_int(found == record_id.end() ? -1 : found->second) + ")";
        }
        text += ")\n";
    }
    for (int s = 0; s < effect_set_count; ++s) {
        const std::uint32_t lists = xbe_u32(xbe, effect_sets + static_cast<std::uint32_t>(s) * 8);
        const std::uint32_t kinds =
            xbe_u32(xbe, effect_sets + static_cast<std::uint32_t>(s) * 8 + 4);
        text += "(effect-set (id " + sx_int(s) + ") (effects";
        for (int i = 0; i < 16; ++i) {
            const int value = xbe_i32(xbe, lists + static_cast<std::uint32_t>(i) * 4);
            if (value == -1) {
                break;
            }
            text += " " + sx_int(value);
        }
        text += ") (sprites";
        for (int i = 0; i < 16; ++i) {
            const int value = xbe_i32(xbe, kinds + static_cast<std::uint32_t>(i) * 4);
            if (value == -2) {
                break;
            }
            text += " " + sx_int(value);
        }
        text += "))\n";
    }
    for (int k = 0; k < sprite_kind_count; ++k) {
        const std::uint32_t at = sprite_kinds + static_cast<std::uint32_t>(k) * 0x20;
        text += "(sprite-kind (id " + sx_int(k) + ") (model " + sx_int(xbe_i32(xbe, at)) +
                ") (frames " + sx_int(xbe_i32(xbe, at + 4)) + ") (step " +
                sx_int(xbe_i32(xbe, at + 8)) + ") (scale_from_kind " +
                sx_int(xbe_i32(xbe, at + 12)) + ") (no_billboard " + sx_int(xbe_i32(xbe, at + 16)) +
                ") (flags " + sx_hex(xbe_u32(xbe, at + 20)) + "))\n";
    }
    for (int c = 0; c < flare_cell_count; ++c) {
        const std::uint32_t at = flare_cells + static_cast<std::uint32_t>(c) * 16;
        text += "(flare-cell (id " + sx_int(c) + ") (rect " + sx_float(xbe_f32(xbe, at)) + " " +
                sx_float(xbe_f32(xbe, at + 4)) + " " + sx_float(xbe_f32(xbe, at + 8)) + " " +
                sx_float(xbe_f32(xbe, at + 12)) + "))\n";
    }
    for (int p = 0; p < 32; ++p) {
        const std::uint32_t at = flare_pieces + static_cast<std::uint32_t>(p) * 16;
        const int cell = xbe_i32(xbe, at);
        if (cell == -1) {
            break;
        }
        text += "(flare-piece (id " + sx_int(p) + ") (cell " + sx_int(cell) + ") (along " +
                sx_float(xbe_f32(xbe, at + 4)) + ") (scale " + sx_float(xbe_f32(xbe, at + 8)) +
                ") (color " + sx_hex(xbe_u32(xbe, at + 12)) + "))\n";
    }

    for (int i = 0; i < impact_count; ++i) {
        const std::uint32_t at = impact_rows + static_cast<std::uint32_t>(i) * 0xC;
        text += "(impact-fx (id " + sx_int(i) + ") (scale " + sx_float(xbe_f32(xbe, at)) +
                ") (effect_set " + sx_int(xbe_i32(xbe, at + 4)) + ") (mark " +
                sx_int(xbe_u8(xbe, at + 8)) + "))\n";
    }

    for (int s = 0; s < foot_landing_states; ++s) {
        const std::uint32_t at = foot_landing_sets + static_cast<std::uint32_t>(s) * 12;
        text += "(foot-landing (id " + sx_int(s) + ") (sets " + sx_int(xbe_i32(xbe, at)) + " " +
                sx_int(xbe_i32(xbe, at + 4)) + " " + sx_int(xbe_i32(xbe, at + 8)) + "))\n";
    }

    for (int i = 0; i < shot_look_count; ++i) {
        const std::uint32_t at = shot_looks + static_cast<std::uint32_t>(i) * 0x2C;
        text += "(shot-look (id " + sx_int(i) + ") (billboard " + sx_int(xbe_i32(xbe, at)) +
                ") (model " + sx_int(xbe_i32(xbe, at + 0xC)) + ") (quad " +
                sx_int(xbe_i32(xbe, at + 0x10)) + ") (flame " + sx_int(xbe_i32(xbe, at + 0x14)) +
                ") (scale " + sx_float(xbe_f32(xbe, at + 0x18)) + ") (flame_z " +
                sx_float(xbe_f32(xbe, at + 0x1C)) + ") (flame_scale " +
                sx_float(xbe_f32(xbe, at + 0x20)) + " " + sx_float(xbe_f32(xbe, at + 0x24)) + " " +
                sx_float(xbe_f32(xbe, at + 0x28)) + "))\n";
    }
    text += "(lamp-glow-models";
    for (int i = 0; i < 14; ++i) {
        text += " " + sx_int(xbe_i32(xbe, lamp_glow_models + static_cast<std::uint32_t>(i) * 4));
    }
    text += ")\n(lamp-glow-columns";
    for (int i = 0; i < 4; ++i) {
        text += " " + sx_int(xbe_i32(xbe, lamp_glow_columns + static_cast<std::uint32_t>(i) * 4));
    }
    text += ")\n";
    text += "(arc-delays";
    for (int i = 0; i < 16; ++i) {
        text += " " + sx_float(xbe_f32(xbe, arc_delays + static_cast<std::uint32_t>(i) * 4));
    }
    text += ")\n";

    std::error_code made;
    std::filesystem::create_directories(options.out_dir + "/tables", made);
    const std::string path = options.out_dir + "/tables/effects.sexp";
    std::ofstream out(path, std::ios::binary);
    out << text;
    if (!out) {
        std::fprintf(stderr, "effects: can't write %s\n", path.c_str());
        return false;
    }
    manifest.entries.push_back(
        "(table (name effects) (file \"tables/effects.sexp\") (xbe effect_lists 0x215848 "
        "effect_sets 0x205b30 sprite_fx_kinds 0x215d68 D_0022E370 0x22e370))");
    std::printf("effects: %zu particle records, %d effects, %d sets\n", record_id.size(),
                effect_count, effect_set_count);
    return true;
}
