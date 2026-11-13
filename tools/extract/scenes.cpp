#include "scenes.hpp"

#include "disc.hpp"
#include "extract.hpp"
#include "sexp_out.hpp"
#include "tables.hpp"

#include <cstdio>
#include <filesystem>

namespace {

// scene_camera_rows 0x258E68: 12 bytes per scene state (scene_table 0x242C50's rows 0..0x2E):
// the camera motion, the root motion, a byte saying both are relative
constexpr std::uint32_t scene_camera_rows = 0x258E68;
constexpr int scene_states = 0x2F;
// camera_rails 0x2329F8 {points, count}[8] and camera_rail_zone_ranges 0x232AD8 RailRange *[8],
// five zones each
constexpr std::uint32_t camera_rails = 0x2329F8;
constexpr std::uint32_t camera_rail_zone_ranges = 0x232AD8;
constexpr int day_modes = 8;
constexpr int rail_zones = 5;
// the bark tables (src/data/ui_c.c) and the Area Rankers' defeat lines by area
constexpr std::uint32_t bark_speaker_rule = 0x203768;      // short[36]
constexpr std::uint32_t bark_chance_human = 0x2037B0;      // float[36]
constexpr std::uint32_t bark_chance_fixed = 0x203840;      // float[36]
constexpr std::uint32_t bark_chance_ai = 0x2038D0;         // float[35]
constexpr std::uint32_t ranker_defeat_scripts = 0x2430CC;  // EventCommand *[3]
constexpr int bark_kinds = 35;

// a cursor pair over a motion record's curves (keys and values are separate runs)
struct Cursor {
    const Xbe& xbe;
    std::uint32_t keys = 0;
    std::uint32_t values = 0;
};

// one curve as motion_curve_eval walks it (tools/extract/motion.cpp's read_curve over the XBE):
// a u16 header (count & 0x1FFF, mode & 0xE000), count u16 times (none for mode 0), the mode's floats
std::string read_curve(Cursor& in, float sign) {
    if (!in.keys) {
        return "(none)";
    }
    const std::uint16_t header = static_cast<std::uint16_t>(xbe_i16(in.xbe, in.keys));
    const int count = header & 0x1FFF;
    const int mode = header & 0xE000;
    int floats = 0;
    switch (mode) {
    case 0:
        floats = count ? 1 : 0;
        break;
    case 0x2000:
    case 0x4000:
    case 0x6000:
        floats = count + 1;
        break;
    case 0x8000:
    case 0xA000:
        floats = 2 * count + 1;
        break;
    case 0xC000:
        floats = 3 * count + 1;
        break;
    default:
        floats = 2 * count + 2;
        break;
    }
    std::string text = "(curve " + sx_hex(static_cast<std::uint32_t>(mode)) + " (times";
    in.keys += 2;
    if (mode != 0) {
        for (int i = 0; i < count; ++i) {
            text += " " + sx_int(static_cast<std::uint16_t>(
                              xbe_i16(in.xbe, in.keys + static_cast<std::uint32_t>(i) * 2)));
        }
        in.keys += static_cast<std::uint32_t>(count) * 2;
    }
    text += ") (values";
    for (int i = 0; i < floats; ++i) {
        const float value = xbe_f32(in.xbe, in.values + static_cast<std::uint32_t>(i) * 4) * sign;
        text += " " + fl(value == 0.0f ? 0.0f : value);
    }
    in.values += static_cast<std::uint32_t>(floats) * 4;
    return text + "))";
}

std::string read_group(Cursor& in, const char* name, const float signs[3]) {
    std::string text = "\n    (" + std::string(name);
    for (int axis = 0; axis < 3; ++axis) {
        text += "\n      " + read_curve(in, signs[axis]);
    }
    return text + ")";
}

bool write_text(Tables& tables, const std::string& name, const std::string& decomp,
                std::uint32_t va, int rows, const std::string& body) {
    TableFile file;
    file.name = name;
    file.decomp = decomp;
    file.va = va;
    file.rows = rows;
    file.forms.push_back(body);
    return write_table(tables, file);
}

// scene_camera_rows: the camera curves are roll, fov, eye x y z, target x y z (scene_camera_sample
// reads eye.z and target.z negated: in the port frame they are the stored values, written as
// stored); the root motion is a motion track (rotation if flags & 0x700, then translation) written
// as the motion family writes tracks
bool write_scene_cameras(Tables& tables) {
    const Xbe& xbe = *tables.xbe;
    std::string body =
        ";; (scene STATE (relative 0|1) (camera (duration TICKS) (curves roll fov eye-x eye-y\n"
        ";; eye-z target-x target-y target-z)) (root (flags F) (rotate x y z) (translate x y z)))\n"
        ";; Camera z curves are already the port frame's z (retail reads them negated, the port\n"
        ";; frame negates z); the root track's signs are the motion family's (rotate and translate\n"
        ";; (x, -y, -z)).";
    int rows = 0;
    for (int state = 0; state < scene_states; ++state) {
        const std::uint32_t row = scene_camera_rows + static_cast<std::uint32_t>(state) * 12;
        const std::uint32_t camera = xbe_u32(xbe, row);
        const std::uint32_t root = xbe_u32(xbe, row + 4);
        const int relative = xbe_u8(xbe, row + 8);
        if (!camera && !root) {
            continue;
        }
        body += "\n(scene " + sx_hex(static_cast<std::uint32_t>(state)) + " (relative " +
                sx_int(relative) + ")";
        if (camera) {
            Cursor in{xbe, xbe_u32(xbe, camera + 8), xbe_u32(xbe, camera + 12)};
            body += "\n  (camera (duration " + sx_int(xbe_i32(xbe, camera)) + ") (address " +
                    sx_hex(camera) + ")\n    (curves";
            for (int k = 0; k < 8; ++k) {
                body += "\n      " + read_curve(in, 1.0f);
            }
            body += "))";
        }
        if (root) {
            const std::uint32_t flags = xbe_u32(xbe, root + 4);
            Cursor in{xbe, xbe_u32(xbe, root + 8), xbe_u32(xbe, root + 12)};
            body += "\n  (root (flags " + sx_hex(flags) + ") (address " + sx_hex(root) + ")";
            const float signs[3] = {1, -1, -1};
            if (flags & 0x700) {
                body += read_group(in, "rotate", signs);
            }
            if (flags & 0xE0) {
                body += read_group(in, "translate", signs);
            }
            body += ")";
        }
        body += ")";
        ++rows;
    }
    return write_text(tables, "scene_cameras", "scene_camera_rows", scene_camera_rows, rows, body);
}

// camera_rails: the rail points per day mode (port frame, z negated) and each zone's [first, end)
bool write_camera_rails(Tables& tables) {
    const Xbe& xbe = *tables.xbe;
    std::string body = ";; (rails MODE (points (x y z) ...) (zones (first end) x5)), port frame";
    for (int mode = 0; mode < day_modes; ++mode) {
        const std::uint32_t points = xbe_u32(xbe, camera_rails + static_cast<std::uint32_t>(mode) * 8);
        const int count = xbe_i32(xbe, camera_rails + static_cast<std::uint32_t>(mode) * 8 + 4);
        body += "\n(rails " + sx_int(mode) + "\n  (points";
        for (int i = 0; i < count && points; ++i) {
            const std::uint32_t at = points + static_cast<std::uint32_t>(i) * 12;
            const float z = -xbe_f32(xbe, at + 8);
            body += " (" + fl(xbe_f32(xbe, at)) + " " + fl(xbe_f32(xbe, at + 4)) + " " +
                    fl(z == 0.0f ? 0.0f : z) + ")";
        }
        body += ")\n  (zones";
        const std::uint32_t ranges =
            xbe_u32(xbe, camera_rail_zone_ranges + static_cast<std::uint32_t>(mode) * 4);
        for (int zone = 0; zone < rail_zones; ++zone) {
            const std::uint32_t at = ranges + static_cast<std::uint32_t>(zone) * 8;
            body += " (" + sx_int(ranges ? xbe_i32(xbe, at) : 0) + " " +
                    sx_int(ranges ? xbe_i32(xbe, at + 4) : 0) + ")";
        }
        body += "))";
    }
    return write_text(tables, "camera_rails", "camera_rails, camera_rail_zone_ranges", camera_rails,
                      day_modes, body);
}

bool write_barks(Tables& tables) {
    const Xbe& xbe = *tables.xbe;
    std::string body = ";; by Bark kind 0..34 (enum Bark, include/actor.h): who may say it (0 anyone,\n"
                       ";; 1 humans, 2 fixed opponents), the chance a request is kept by speaker;\n"
                       ";; the Area Rankers' defeat lines by area (script addresses, bark_<hex> in\n"
                       ";; data/scripts/bark.sexp)\n";
    body += ints_i16("speaker-rule", xbe, bark_speaker_rule, bark_kinds) + "\n";
    body += floats("chance-human", xbe, bark_chance_human, bark_kinds) + "\n";
    body += floats("chance-fixed-opponent", xbe, bark_chance_fixed, bark_kinds) + "\n";
    body += floats("chance-ai", xbe, bark_chance_ai, bark_kinds) + "\n";
    body += "(ranker-defeat-scripts";
    for (int area = 0; area < 3; ++area) {
        body += " " + sx_hex(xbe_u32(xbe, ranker_defeat_scripts + static_cast<std::uint32_t>(area) * 4));
    }
    body += ")";
    return write_text(tables, "barks", "bark_speaker_rule, bark_chance_*, ranker_defeat_scripts",
                      bark_speaker_rule, bark_kinds, body);
}

// battle_tournament_trophy_map 0x249D28: (tournament, trophy) pairs to (-1, -1), whole (the ui
// family's tables cut it in three where the source did)
bool write_trophies(Tables& tables) {
    const Xbe& xbe = *tables.xbe;
    constexpr std::uint32_t map = 0x249D28;
    std::string body = ";; (trophy-map (TOURNAMENT TROPHY) ...): the result screen's trophy by today's\n"
                       ";; tournament (result_screen_setup 0x94F6E); trophies index\n"
                       ";; battle_trophy_models and battle_trophy_model_slots\n(trophy-map";
    int rows = 0;
    for (std::uint32_t at = map; rows < 64; at += 8, ++rows) {
        const int tournament = xbe_i32(xbe, at), trophy = xbe_i32(xbe, at + 4);
        if (tournament == -1 && trophy == -1) {
            break;
        }
        body += " (" + sx_int(tournament) + " " + sx_int(trophy) + ")";
    }
    body += ")";
    return write_text(tables, "trophies", "battle_tournament_trophy_map", map, rows, body);
}

// scene_ending_shots 0x242E90 (TerminalRow, 0x34 bytes, scene states 8..0x12): each shot's lead-in
// (length, compared with the scene's door_wait), the fade in it starts (+4), the timer the fade out
// starts at (+8), the frame capture's length on its exit (+0xC), the supply craft's route (+0x10),
// the paper storm's three anchors and flags (+0x14..+0x29), its scene text (+0x2C, a script in
// scripts/bark.sexp used by (data 0x242EBC + row x 0x34)) and the frame that text starts (+0x30);
// then the last shot's beep schedule (D_001B5B14, -1 ended) and the ending routes' speeds
// (ending_route_speeds 0x21C66C, routes 24..30)
bool write_ending_shots(Tables& tables) {
    const Xbe& xbe = *tables.xbe;
    constexpr std::uint32_t shots = 0x242E90;
    constexpr int rows = 11;
    constexpr std::uint32_t beeps = 0x1B5B14;
    constexpr std::uint32_t route_speeds = 0x21C66C;
    std::string body =
        ";; (shot STATE (length L) (fade-in T) (fade-out-at T) (capture T) (route R)\n"
        ";; (paper A FA B FB C FC FD) (script ADDRESS) (script-at FRAME)): the in-engine ending's\n"
        ";; shots, ticks and curve frames as stored; -1 none";
    for (int row = 0; row < rows; ++row) {
        const std::uint32_t at = shots + static_cast<std::uint32_t>(row) * 0x34;
        body += "\n(shot " + sx_hex(static_cast<std::uint32_t>(8 + row)) + " (length " +
                fl(xbe_f32(xbe, at)) + ") (fade-in " + fl(xbe_f32(xbe, at + 4)) +
                ") (fade-out-at " + fl(xbe_f32(xbe, at + 8)) + ") (capture " +
                fl(xbe_f32(xbe, at + 12)) + ") (route " + sx_int(xbe_i32(xbe, at + 0x10)) +
                ")\n  (paper " + sx_int(xbe_i32(xbe, at + 0x14)) + " " +
                sx_int(xbe_i32(xbe, at + 0x18)) + " " + sx_int(xbe_i32(xbe, at + 0x1C)) + " " +
                sx_int(xbe_i32(xbe, at + 0x20)) + " " + sx_int(xbe_i32(xbe, at + 0x24)) + " " +
                sx_int(xbe_u8(xbe, at + 0x28)) + " " + sx_int(xbe_u8(xbe, at + 0x29)) +
                ") (script " + sx_hex(xbe_u32(xbe, at + 0x2C)) + ") (script-at " +
                fl(xbe_f32(xbe, at + 0x30)) + "))";
    }
    body += "\n(beeps";
    for (int k = 0; k < 32; ++k) {
        const int tick = xbe_i32(xbe, beeps + static_cast<std::uint32_t>(k) * 4);
        body += " " + sx_int(tick);
        if (tick == -1) {
            break;
        }
    }
    body += ")\n" + floats("route-speeds", xbe, route_speeds, 7);
    return write_text(tables, "scene_ending", "scene_ending_shots, D_001B5B14, ending_route_speeds",
                      shots, rows, body);
}

} // namespace

bool extract_scenes(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    Tables tables;
    tables.disc = &disc;
    tables.xbe = &disc.xbe;
    tables.dir = options.out_dir + "/tables";
    tables.manifest = &manifest;
    std::error_code made;
    std::filesystem::create_directories(tables.dir, made);
    if (!write_scene_cameras(tables) || !write_camera_rails(tables) || !write_barks(tables) ||
        !write_trophies(tables) || !write_ending_shots(tables)) {
        return false;
    }
    std::printf("scenes: %d files\n", tables.files);
    return true;
}
