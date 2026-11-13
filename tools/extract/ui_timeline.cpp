// The sprite player's two timelines, level_meter_a and level_meter_b (src/data/misc_f.c), to
// data/ui/timelines/<name>.sexp: a Flash movie flattened to per-frame command lists (AnimResource,
// AnimFrame, AnimCommand, AnimSpriteKey and AnimPart in the decomp's include/tables.h; played by
// sprite_player_apply_frame 0xD1C5C and sprite_player_place 0xD1F2E).
#include "addresses.hpp"
#include "disc.hpp"
#include "manifest.hpp"
#include "sexp_out.hpp"
#include "ui.hpp"

#include <cstdio>
#include <fstream>
#include <map>

namespace {

// render registry row of each rectangle record, for AnimPart.tex (a rectangle's address)
std::map<std::uint32_t, int> registry_rows(const Xbe& xbe) {
    std::map<std::uint32_t, int> rows;
    for (int id = 0; id < xbe_at::render_registry_count; ++id) {
        rows[xbe_u32(xbe, xbe_at::render_registry + id * 8)] = id;
    }
    return rows;
}

std::string floats(const Xbe& xbe, std::uint32_t va, int n) {
    std::string text;
    for (int i = 0; i < n; ++i) {
        text += " " + sx_float(xbe_f32(xbe, va + i * 4));
    }
    return text;
}

// AnimSpriteKey: character, depth, move, matrix[4], x, y, color[4], color_add[4]
std::string place_text(const Xbe& xbe, std::uint32_t key) {
    return "(place (character " + sx_int(xbe_i32(xbe, key)) + ") (depth " +
           sx_int(xbe_i32(xbe, key + 4)) + ") (move " + sx_int(xbe_u8(xbe, key + 8)) + ") (matrix" +
           floats(xbe, key + 0x0C, 4) + ") (x " + sx_int(xbe_i32(xbe, key + 0x1C)) + ") (y " +
           sx_int(xbe_i32(xbe, key + 0x20)) + ") (color" + floats(xbe, key + 0x24, 4) +
           ") (color-add" + floats(xbe, key + 0x34, 4) + "))";
}

std::string timeline_text(const Xbe& xbe, const char* name, std::uint32_t resource,
                          std::uint32_t parts, const std::map<std::uint32_t, int>& rows) {
    // AnimResource: define_count, f_04, width, height, fps, frame_count, defines, frames
    const int fps = static_cast<std::uint16_t>(xbe_i16(xbe, resource + 0x0A));
    const int frame_count = xbe_i32(xbe, resource + 0x0C);
    const std::uint32_t frames = xbe_u32(xbe, resource + 0x14);
    std::string text = ";; " + std::string(name) +
                       ": a sprite player timeline (pc_extract ui family, docs/ui-assets.md). "
                       "Frames play 60 / fps ticks apart;\n;; a place command puts a part at "
                       "its depth with a 2x2 matrix (scale and rotation), a position in 640x480 "
                       "pixels and a colour\n;; multiplier; move 1 keeps the slot's part, 0 "
                       "replaces it. Frame 0's command count less one is the slot count.\n";
    text += "(timeline " + std::string(name) + " (va " + sx_hex(resource) + ")\n  (stage " +
            sx_int(xbe_i16(xbe, resource + 6)) + " " + sx_int(xbe_i16(xbe, resource + 8)) +
            ") (fps " + sx_int(fps) + ") (frame-count " + sx_int(frame_count) + ") (define-count " +
            sx_int(xbe_i32(xbe, resource)) + ") (f_04 " + sx_int(xbe_i16(xbe, resource + 4)) +
            ")\n";

    // the parts its caller hands the player: AnimPart {tex, render_id, depth}, seven of them
    text += "  (parts";
    for (int i = 0; i < 7; ++i) {
        const std::uint32_t part = parts + i * 12;
        const auto row = rows.find(xbe_u32(xbe, part));
        text += "\n   (part " + sx_int(i) + " (sprite " +
                sx_int(row == rows.end() ? -1 : row->second) + ") (render-handle " +
                sx_int(xbe_i32(xbe, part + 4)) + ") (depth " + sx_int(xbe_i32(xbe, part + 8)) +
                "))";
    }
    text += ")";

    // frames: AnimFrame {short count; AnimCommand *commands}, commands {kind, data}
    for (int f = 0; f < frame_count; ++f) {
        const int count = xbe_i16(xbe, frames + f * 8);
        const std::uint32_t commands = xbe_u32(xbe, frames + f * 8 + 4);
        text += "\n  (frame " + sx_int(f);
        for (int c = 0; c < count; ++c) {
            const int kind = xbe_i32(xbe, commands + c * 8);
            const std::uint32_t data = xbe_u32(xbe, commands + c * 8 + 4);
            if (kind == 1) {
                text += "\n   " + place_text(xbe, data);
            } else if (kind == 7) {
                // four bytes read high first into the clear colour
                const std::uint32_t colour =
                    static_cast<std::uint32_t>(xbe_u8(xbe, data)) << 24 |
                    static_cast<std::uint32_t>(xbe_u8(xbe, data + 1)) << 16 |
                    static_cast<std::uint32_t>(xbe_u8(xbe, data + 2)) << 8 | xbe_u8(xbe, data + 3);
                text += "\n   (background " + sx_hex(colour) + ")";
            } else if (kind == 4) {
                const int action = xbe_i32(xbe, data);
                text += action == 6 ? "\n   (play)" : action == 7 ? "\n   (stop)" : "";
                if (action != 6 && action != 7) {
                    text += "\n   (event " + sx_int(action) + ")";
                }
            } else {
                text += "\n   (command " + sx_int(kind) + " " + sx_hex(data) + ")";
            }
        }
        text += ")";
    }
    return text + ")\n";
}

} // namespace

bool write_ui_timelines(const Disc& disc, const std::string& out_dir, Manifest& manifest) {
    struct Timeline {
        const char* name;
        std::uint32_t resource;
        std::uint32_t parts;
    };
    const Timeline timelines[] = {
        {"level_meter_a", xbe_at::level_meter_a, xbe_at::level_meter_a_parts},
        {"level_meter_b", xbe_at::level_meter_b, xbe_at::level_meter_b_parts},
    };
    const std::map<std::uint32_t, int> rows = registry_rows(disc.xbe);
    for (const Timeline& timeline : timelines) {
        const std::string file = "ui/timelines/" + std::string(timeline.name) + ".sexp";
        std::ofstream out(out_dir + "/" + file, std::ios::binary);
        out << timeline_text(disc.xbe, timeline.name, timeline.resource, timeline.parts, rows);
        if (!out) {
            std::fprintf(stderr, "ui: can't write %s\n", file.c_str());
            return false;
        }
        manifest.entries.push_back("(ui-timeline (name " + std::string(timeline.name) + ") (xbe " +
                                   sx_hex(timeline.resource) + ") (file " + sx_string(file) + "))");
    }
    std::printf("ui: 2 sprite player timelines\n");
    return true;
}
