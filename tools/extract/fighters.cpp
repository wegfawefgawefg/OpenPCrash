#include "fighters.hpp"

#include "addresses.hpp"
#include "disc.hpp"
#include "extract.hpp"
#include "motion.hpp"
#include "sexp_out.hpp"
#include "sjis.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

std::string int_list(const Xbe& xbe, std::uint32_t va, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        out += " " + sx_int(xbe_i32(xbe, va + i * 4));
    }
    return out;
}

std::string node_list(const Xbe& xbe, std::uint32_t va, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        out += " " + sx_hex(xbe_u32(xbe, va + i * 4));
    }
    return out;
}

std::string name_at(const Xbe& xbe, std::uint32_t pointer) {
    return pointer ? sx_string(sjis_to_utf8(xbe_cstring(xbe, pointer))) : "\"\"";
}

std::uint32_t u32_of(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    std::uint32_t v = 0;
    if (at + 4 <= bytes.size()) {
        std::memcpy(&v, bytes.data() + at, 4);
    }
    return v;
}
std::uint16_t u16_of(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return at + 2 <= bytes.size() ? static_cast<std::uint16_t>(bytes[at] | bytes[at + 1] << 8) : 0;
}

// every weapon block (kind 10) the four block tables name, sorted
std::vector<int> weapon_block_assets(const Xbe& xbe) {
    std::vector<int> blocks;
    for (const auto& [table, count] :
         {std::pair{xbe_at::arm_a_block_assets, 34}, std::pair{xbe_at::arm_b_block_assets, 34},
          std::pair{xbe_at::back_a_block_assets, 37}, std::pair{xbe_at::back_b_block_assets, 37}}) {
        for (int i = 0; i < count; ++i) {
            const int asset = xbe_i32(xbe, table + i * 4);
            if (asset > 0 && std::find(blocks.begin(), blocks.end(), asset) == blocks.end()) {
                blocks.push_back(asset);
            }
        }
    }
    std::sort(blocks.begin(), blocks.end());
    return blocks;
}

// The weapon blocks whole (docs/port/data.md section 10): resource 0's attach streams 0..3,
// resource 1's clip (NodeAnimSet) and resource 2's open and close frames.
// - Streams {x y z node} (weapon_model_part 0x9DAFE, the count at +4 shared by all): 0 muzzles,
//   1 the flash model 0x3F3's point, 2 the casing's origin, 3 the casing's direction. Points 0..2
//   are translated by (x, y, -z) in the original's frame (weapon_attach_matrix 0x9A954), so the
//   stored words are the port frame's point as they stand. Stream 3 is a direction that
//   weapon_eject_casing 0x998EE turns un-negated (transform_normal of the original's matrix): it
//   is written with z negated, the same direction in the port frame.
// - The clip (anim_player_sample 0xB06E5): (clip frames (channel node flags (scale x y z) (rotate
//   x y z) (translate x y z)) ...), frames the set's raw u16 (end = (frames & 0x7FFF) - 1). A
//   channel's curves run 0x4000, scale 0x2000..0x800, rotate 0x400..0x100, translate 0x80..0x20,
//   0x10, 0x8 on one cursor; 0x4000, 0x10 and 0x8 are read past and dropped. The writer's flips
//   (model_node_write_transform 0xB0E11: translate (x, -y, z), rotate (-x, y, -z)) and the port
//   frame are baked in as the motion family does: translate and rotate are (x, -y, -z) of the
//   curves'. Rotations are still to be wrapped through 16 bits when sampled.
// - (frames open close): list 0 of resource 2 when it has two entries, each less 1
//   (weapon_anim_init 0x9AEF5, anim_list_entry): the clip locks open and closed. Absent: unlocked.
std::string weapon_blocks_text(const Disc& disc) {
    std::string text = "(weapon-blocks  ; (block asset (points n) (stream k (x y z node) ...) x 4 "
                       "[(frames open close)] [(clip ...)]), port frame, see fighters.cpp\n";
    int warnings = 0;
    for (int asset : weapon_block_assets(disc.xbe)) {
        const std::vector<std::uint8_t> bytes = read_asset(disc, asset);
        if (u32_of(bytes, 0) < 1) {
            continue;
        }
        const std::size_t points = u32_of(bytes, 4);
        const std::uint32_t count = u32_of(bytes, points + 4);
        text += "  (block " + sx_int(asset) + " (points " + sx_int(count) + ")";
        for (int k = 0; k < 4; ++k) {
            const std::uint32_t stream = u32_of(bytes, points + 0x10 + k * 4);
            text += "\n    (stream " + sx_int(k);
            for (std::uint32_t i = 0; stream && i < count && i < 64; ++i) {
                const std::size_t at = points + stream + i * 16;
                if (at + 16 > bytes.size()) {
                    ++warnings;
                    break;
                }
                float f[3];
                std::int32_t node = 0;
                std::memcpy(f, bytes.data() + at, 12);
                std::memcpy(&node, bytes.data() + at + 12, 4);
                if (k == 3) {
                    f[2] = f[2] == 0.0f ? 0.0f : -f[2];
                }
                text += " (" + sx_float(f[0]) + " " + sx_float(f[1]) + " " + sx_float(f[2]) + " " +
                        sx_int(node) + ")";
            }
            text += ")";
        }
        // resource 2: list 0 of two entries locks the clip
        if (u32_of(bytes, 0) >= 3) {
            const std::size_t lists = u32_of(bytes, 12);
            const bool any = u32_of(bytes, lists) | u32_of(bytes, lists + 4) |
                             u32_of(bytes, lists + 8) | u32_of(bytes, lists + 12);
            if (any && u32_of(bytes, lists) == 2) {
                text += "\n    (frames " +
                        sx_int(static_cast<std::int32_t>(u32_of(bytes, lists + 4)) - 1) + " " +
                        sx_int(static_cast<std::int32_t>(u32_of(bytes, lists + 8)) - 1) + ")";
            }
        }
        // resource 1: the clip, when anim_player_start finds one
        if (u32_of(bytes, 0) >= 2) {
            const std::size_t set = u32_of(bytes, 8);
            const int channels = u16_of(bytes, set);
            const bool present = u32_of(bytes, set) | u32_of(bytes, set + 4) |
                                 u32_of(bytes, set + 8) | u32_of(bytes, set + 12);
            if (present) {
                text += "\n    (clip " + sx_int(u16_of(bytes, set + 2));
                for (int c = 0; c < channels; ++c) {
                    const std::size_t at = set + 4 + static_cast<std::size_t>(c) * 0x3C;
                    const std::uint32_t flags = u16_of(bytes, at + 2);
                    const std::uint32_t values_rel = u32_of(bytes, at + 4);
                    const std::uint32_t keys_rel = u32_of(bytes, at + 8);
                    std::uint32_t keys = keys_rel ? static_cast<std::uint32_t>(set + keys_rel) : 0;
                    std::uint32_t values =
                        values_rel ? static_cast<std::uint32_t>(set + values_rel) : 0;
                    bool ok = true;
                    // channel i tests bit 0x4000 >> i; 1..9 are scale, rotate, translate x y z
                    std::string curves[12];
                    const float signs[12] = {1, 1, 1, 1, 1, -1, -1, 1, -1, -1, 1, 1};
                    for (int i = 0; i < 12; ++i) {
                        curves[i] = flags & (0x4000u >> i)
                                        ? motion_curve_text(bytes, keys, values, signs[i], ok)
                                        : "(none)";
                    }
                    text += "\n      (channel " + sx_int(u16_of(bytes, at)) + " " + sx_hex(flags);
                    const char* names[3] = {"scale", "rotate", "translate"};
                    for (int g = 0; g < 3; ++g) {
                        text += " (" + std::string(names[g]);
                        for (int axis = 0; axis < 3; ++axis) {
                            text += " " + curves[1 + g * 3 + axis];
                        }
                        text += ")";
                    }
                    text += ")";
                    if (!ok) {
                        ++warnings;
                        std::fprintf(stderr, "fighters: block %d channel %d runs past the file\n",
                                     asset, c);
                    }
                }
                text += ")";
            }
        }
        text += ")\n";
    }
    if (warnings) {
        std::fprintf(stderr, "fighters: %d weapon block warnings\n", warnings);
    }
    return text + ")\n";
}

} // namespace

bool extract_fighters(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    const Xbe& xbe = disc.xbe;
    std::string text = ";; fighter_assets: the asset ids and tables a fighter is assembled from "
                       "(fighter_assets_load\n"
                       ";; 0x817E3, actor_part_batches_build 0x78185). Weapon slots: 0 arm A, 1 "
                       "arm B, 2 back A, 3 back B.\n";
    text += "(body-models" + int_list(xbe, xbe_at::body_model_assets, 7) +
            ")  ; body_model_assets, by body class\n";
    text += "(body-sticker-models" + int_list(xbe, xbe_at::body_sticker_model_assets, 7) +
            ")  ; body_sticker_model_assets\n";
    text += "(leg-models" + int_list(xbe, xbe_at::leg_model_assets, 21) +
            ")  ; leg_model_assets, by leg class\n";
    text += "(arm-a-models" + int_list(xbe, xbe_at::arm_a_model_assets, 34) +
            ")  ; D_0025ACA0, by arm class\n";
    text += "(arm-b-models" + int_list(xbe, xbe_at::arm_b_model_assets, 34) + ")  ; D_0025AC18\n";
    // 37 a table: the decomp splits D_0025AE38[22] from D_0025AE90 (whose first word, typed as a
    // pointer, is asset 128) and D_0025AED0 from D_0025AF48, but back classes run to 36 and the
    // readers index straight through; class 30 is body class 2's default pair (D_0025AE90.f_20,
    // D_0025AF48[0])
    text += "(back-a-models" + int_list(xbe, xbe_at::back_a_model_assets, 37) +
            ")  ; D_0025AE38.., by back class\n";
    text +=
        "(back-b-models" + int_list(xbe, xbe_at::back_b_model_assets, 37) + ")  ; D_0025AED0..\n";
    text += "(arm-a-blocks" + int_list(xbe, xbe_at::arm_a_block_assets, 34) +
            ")  ; D_0025AD28 weapon blocks\n";
    text += "(arm-b-blocks" + int_list(xbe, xbe_at::arm_b_block_assets, 34) + ")  ; D_0025ADB0\n";
    text += "(back-a-blocks" + int_list(xbe, xbe_at::back_a_block_assets, 37) +
            ")  ; back_wfb_assets_mount2, by back class\n";
    text += "(back-b-blocks" + int_list(xbe, xbe_at::back_b_block_assets, 37) +
            ")  ; back_wfb_assets_mount3\n";
    text += "(leg-graphs" + node_list(xbe, xbe_at::leg_graph_roots, 21) +
            ")  ; D_002415D0, by leg class\n";
    text += "(arm-a-graphs" + node_list(xbe, xbe_at::arm_a_graph_roots, 34) +
            ")  ; D_002416B0, by arm class\n";
    text += "(arm-b-graphs" + node_list(xbe, xbe_at::arm_b_graph_roots, 34) + ")  ; D_00241628\n";
    text += "(leg-motions" + int_list(xbe, xbe_at::leg_motion_assets, 21) +
            ")  ; sv_leg_root_curve_table, 37 slots\n";
    text += "(body-motions" + int_list(xbe, xbe_at::body_motion_assets, 7) +
            ")  ; body_motion_assets, 7 slots\n";
    text += "(arm-a-motions" + int_list(xbe, xbe_at::arm_a_motion_assets, 42) +
            ")  ; D_002590A8 [leg class][arm motion class], 37 slots\n";
    text += "(arm-b-motions" + int_list(xbe, xbe_at::arm_b_motion_assets, 42) + ")  ; D_00259150\n";
    text += "(arm-motion-class" + int_list(xbe, xbe_at::arm_motion_class, 34) +
            ")  ; sv_arm_motion_class_table\n";

    // PartColorAssets {int *ids; int count} by leg class: the paint (a texture set) by colour
    text += "(part-paints  ; part_color_assets: ids by colour (under 0x39), by leg class\n";
    for (int leg = 0; leg < 21; ++leg) {
        const std::uint32_t ids = xbe_u32(xbe, xbe_at::part_color_assets + leg * 8);
        const int count = xbe_i32(xbe, xbe_at::part_color_assets + leg * 8 + 4);
        text += "  (leg " + sx_int(leg) + int_list(xbe, ids, count) + ")\n";
    }
    text += ")\n";

    // MotionTiming {enabled, phase_a, phase_b, phase_end} [21][37]
    text += "(motion-timing  ; sv_motion_timing_records [leg class][slot]: enabled phase-a phase-b "
            "phase-end, ticks\n";
    for (int leg = 0; leg < 21; ++leg) {
        text += "  (leg " + sx_int(leg);
        for (int slot = 0; slot < 37; ++slot) {
            const std::uint32_t at = xbe_at::motion_timing + (leg * 37 + slot) * 16;
            text += "\n    (" + sx_int(xbe_i32(xbe, at)) + " " + sx_float(xbe_f32(xbe, at + 4)) +
                    " " + sx_float(xbe_f32(xbe, at + 8)) + " " + sx_float(xbe_f32(xbe, at + 12)) +
                    ")";
        }
        text += ")\n";
    }
    text += ")\n";

    // D_0022F338 [21][2]: actor_init 0x80DC1 reads column 1 (+4) as the fighter's radius;
    // sv_geometry_response_table [21][2]: walk and run root-motion scales (0 none)
    text += "(leg-radius  ; D_0022F338 [leg class]: two floats, actor_init reads the second\n";
    for (int leg = 0; leg < 21; ++leg) {
        const std::uint32_t at = xbe_at::leg_radius + leg * 8;
        text += "  (" + sx_int(leg) + " " + sx_float(xbe_f32(xbe, at)) + " " +
                sx_float(xbe_f32(xbe, at + 4)) + ")\n";
    }
    text += ")\n(leg-root-motion  ; sv_geometry_response_table [leg class]: walk run, 0 none\n";
    for (int leg = 0; leg < 21; ++leg) {
        const std::uint32_t at = xbe_at::geometry_response + leg * 8;
        text += "  (" + sx_int(leg) + " " + sx_float(xbe_f32(xbe, at)) + " " +
                sx_float(xbe_f32(xbe, at + 4)) + ")\n";
    }
    text += ")\n";

    // sv_leg_attachment_table 0x259270: per leg class the asset of its fixed attachment vectors
    // (kind 4, 392 bytes: 14 records of f32 a[3], u32 selector, f32 b[3]; docs/port/data.md
    // section 8). actor_attachments_update 0x84D61 negates a's and b's z before the node matrix;
    // the port's mirror negates it back, so the stored words are the port's node-local points
    text += "(leg-attachments  ; sv_leg_attachment_table: (leg n asset (a.x a.y a.z selector b.x "
            "b.y b.z) x 14), port frame\n";
    for (int leg = 0; leg < 21; ++leg) {
        const int asset = xbe_i32(xbe, xbe_at::leg_attachment_assets + leg * 4);
        const std::vector<std::uint8_t> bytes = read_asset(disc, asset);
        text += "  (leg " + sx_int(leg) + " " + sx_int(asset);
        for (int row = 0; row < 14 && bytes.size() >= 392; ++row) {
            float f[7];
            std::uint32_t selector = 0;
            std::memcpy(f, bytes.data() + row * 28, 12);
            std::memcpy(&selector, bytes.data() + row * 28 + 12, 4);
            std::memcpy(f + 4, bytes.data() + row * 28 + 16, 12);
            text += "\n    (" + sx_float(f[0]) + " " + sx_float(f[1]) + " " + sx_float(f[2]) + " " +
                    sx_int(static_cast<int>(selector)) + " " + sx_float(f[4]) + " " +
                    sx_float(f[5]) + " " + sx_float(f[6]) + ")";
        }
        text += ")\n";
    }
    text += ")\n";

    // the weapon blocks' muzzles (kind 10, docs/port/data.md section 10): resource 0's stream 0,
    // {f32 x, y, z; i32 node} per point, the count at +4. weapon_attach_matrix 0x9A954 translates
    // by (x, y, -z); the port's mirror negates z back, so the stored words are port-frame points
    text += "(weapon-muzzles  ; weapon blocks' attach stream 0: (asset (x y z node) ...), port "
            "frame\n";
    {
        std::vector<int> blocks;
        for (const auto& [table, count] :
             {std::pair{xbe_at::arm_a_block_assets, 34}, std::pair{xbe_at::arm_b_block_assets, 34},
              std::pair{xbe_at::back_a_block_assets, 37},
              std::pair{xbe_at::back_b_block_assets, 37}}) {
            for (int i = 0; i < count; ++i) {
                const int asset = xbe_i32(xbe, table + i * 4);
                if (asset > 0 && std::find(blocks.begin(), blocks.end(), asset) == blocks.end()) {
                    blocks.push_back(asset);
                }
            }
        }
        std::sort(blocks.begin(), blocks.end());
        const auto u32_at = [](const std::vector<std::uint8_t>& bytes, std::size_t at) {
            std::uint32_t v = 0;
            if (at + 4 <= bytes.size()) {
                std::memcpy(&v, bytes.data() + at, 4);
            }
            return v;
        };
        for (int asset : blocks) {
            const std::vector<std::uint8_t> bytes = read_asset(disc, asset);
            // the block: u32 count, u32 offset[count]; resource 0 the attach points
            if (u32_at(bytes, 0) < 1) {
                continue;
            }
            const std::size_t points = u32_at(bytes, 4);
            const std::uint32_t count = u32_at(bytes, points + 4);
            const std::uint32_t stream = u32_at(bytes, points + 0x10);
            text += "  (" + sx_int(asset);
            for (std::uint32_t i = 0; stream && i < count && i < 64; ++i) {
                const std::size_t at = points + stream + i * 16;
                if (at + 16 > bytes.size()) {
                    break;
                }
                float f[3];
                std::int32_t node = 0;
                std::memcpy(f, bytes.data() + at, 12);
                std::memcpy(&node, bytes.data() + at + 12, 4);
                text += " (" + sx_float(f[0]) + " " + sx_float(f[1]) + " " + sx_float(f[2]) + " " +
                        sx_int(node) + ")";
            }
            text += ")\n";
        }
    }
    text += ")\n";
    text += weapon_blocks_text(disc);

    // the part records' class fields (the full records go to the tables family)
    text += "(body-classes  ; sv_body_table +0x04, by body record\n";
    for (int i = 0; i < xbe_at::body_record_count; ++i) {
        const std::uint32_t at = xbe_at::body_records + i * 0x40;
        text += "  (" + sx_int(i) + " " + sx_int(xbe_u8(xbe, at + 4)) + " " +
                name_at(xbe, xbe_u32(xbe, at)) + ")\n";
    }
    text += ")\n(leg-classes  ; sv_leg_table +0x08 geometry\n";
    for (int i = 0; i < xbe_at::leg_record_count; ++i) {
        const std::uint32_t at = xbe_at::leg_records + i * 0xC0;
        text += "  (" + sx_int(i) + " " + sx_int(xbe_i32(xbe, at + 8)) + " " +
                name_at(xbe, xbe_u32(xbe, at)) + ")\n";
    }
    text += ")\n(arm-classes  ; sv_arm_table +0x08 geometry\n";
    for (int i = 0; i < xbe_at::arm_record_count; ++i) {
        const std::uint32_t at = xbe_at::arm_records + i * 0xC8;
        text += "  (" + sx_int(i) + " " + sx_int(xbe_i32(xbe, at + 8)) + " " +
                name_at(xbe, xbe_u32(xbe, at)) + ")\n";
    }
    text += ")\n(back-classes  ; sv_back_table +0x08 geometry, -1 none\n";
    for (int i = 0; i < xbe_at::back_record_count; ++i) {
        const std::uint32_t at = xbe_at::back_records + i * 0xC8;
        text += "  (" + sx_int(i) + " " + sx_int(xbe_i32(xbe, at + 8)) + " " +
                name_at(xbe, xbe_u32(xbe, at)) + ")\n";
    }
    text += ")\n";

    // BuildRecord, 0x3C: body, leg, weapon[4] {short id; char tune}, chip, colour, sticker, name
    text += "(builds  ; rumble_scoobee_loadouts: (row body leg (weapons a b c d) colour sticker "
            "name)\n";
    for (int row = 0; row < xbe_at::scoobee_loadout_count; ++row) {
        const std::uint32_t at = xbe_at::scoobee_loadouts + row * 0x3C;
        text += "  (" + sx_int(row) + " " + sx_int(xbe_i16(xbe, at + 2)) + " " +
                sx_int(xbe_i16(xbe, at + 6)) + " (weapons";
        for (int w = 0; w < 4; ++w) {
            text += " " + sx_int(xbe_i16(xbe, at + 0x0A + w * 4));
        }
        text += ") " + sx_int(xbe_i16(xbe, at + 0x1E)) + " " + sx_int(xbe_i16(xbe, at + 0x20)) +
                " " + name_at(xbe, xbe_u32(xbe, at + 0x38)) + ")\n";
    }
    text += ")\n";

    const std::string file = "tables/fighter_assets.sexp";
    std::error_code made;
    std::filesystem::create_directories(options.out_dir + "/tables", made);
    std::ofstream out(options.out_dir + "/" + file, std::ios::binary);
    out << text;
    if (!out) {
        std::fprintf(stderr, "fighters: can't write %s\n", file.c_str());
        return false;
    }
    manifest.entries.push_back(
        "(table (name fighter-assets) (file " + sx_string(file) +
        ") (xbe body_model_assets 0x25ab88 leg_model_assets 0x25abc0 part_color_assets 0x25aae0 "
        "rumble_scoobee_loadouts 0x19f920))");
    std::printf("fighters: written\n");
    return true;
}
