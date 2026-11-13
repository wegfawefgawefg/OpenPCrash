#include "skeletons.hpp"

#include "addresses.hpp"
#include "disc.hpp"
#include "extract.hpp"
#include "sexp_out.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

namespace {

void write_node(const Xbe& xbe, std::uint32_t va, std::set<std::uint32_t>& done,
                std::string& text) {
    if (!va || done.count(va)) {
        return;
    }
    done.insert(va);
    // type, flags, track, translate[3], rotate[3], children, matrix, sphere group, remap, f_34
    text += "(node " + sx_hex(va) + " (type " + sx_int(xbe_i32(xbe, va)) + ") (flags " +
            sx_hex(xbe_u32(xbe, va + 4)) + ") (track " + sx_int(xbe_i32(xbe, va + 8)) +
            ")\n  (translate " + sx_float(xbe_f32(xbe, va + 0x0C)) + " " +
            sx_float(xbe_f32(xbe, va + 0x10)) + " " + sx_float(xbe_f32(xbe, va + 0x14)) +
            ") (rotate " + sx_float(xbe_f32(xbe, va + 0x18)) + " " +
            sx_float(xbe_f32(xbe, va + 0x1C)) + " " + sx_float(xbe_f32(xbe, va + 0x20)) +
            ")\n  (matrix " + sx_int(xbe_i32(xbe, va + 0x28)) + ") (spheres " +
            sx_int(xbe_i32(xbe, va + 0x2C)) + ") (remap " + sx_int(xbe_i32(xbe, va + 0x30)) +
            ") (f34 " + sx_int(xbe_i32(xbe, va + 0x34)) + ")\n  (children";
    std::vector<std::uint32_t> children;
    const std::uint32_t list = xbe_u32(xbe, va + 0x24);
    for (int i = 0; list && i < 64; ++i) {
        const std::uint32_t child = xbe_u32(xbe, list + i * 4);
        if (child == 0) {
            break;
        }
        if (child == 0xFFFFFFFF) {
            text += " weapon";
            continue;
        }
        text += " " + sx_hex(child);
        children.push_back(child);
    }
    text += "))\n";
    for (std::uint32_t child : children) {
        write_node(xbe, child, done, text);
    }
}

} // namespace

bool extract_skeletons(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    const Xbe& xbe = disc.xbe;
    std::string text =
        ";; sv_graphs: SV articulation graph nodes (sv_pose_walk 0xAA419), port frame; see\n"
        ";; tools/extract/skeletons.hpp. Roots by class are in fighter_assets.sexp.\n";
    std::set<std::uint32_t> done;
    const std::uint32_t tables[3] = {xbe_at::leg_graph_roots, xbe_at::arm_a_graph_roots,
                                     xbe_at::arm_b_graph_roots};
    const int counts[3] = {21, 34, 34};
    for (int t = 0; t < 3; ++t) {
        for (int i = 0; i < counts[t]; ++i) {
            write_node(xbe, xbe_u32(xbe, tables[t] + i * 4), done, text);
        }
    }
    const std::string file = "tables/sv_graphs.sexp";
    std::error_code made;
    std::filesystem::create_directories(options.out_dir + "/tables", made);
    std::ofstream out(options.out_dir + "/" + file, std::ios::binary);
    out << text;
    if (!out) {
        std::fprintf(stderr, "skeletons: can't write %s\n", file.c_str());
        return false;
    }
    manifest.entries.push_back(
        "(table (name sv-graphs) (file " + sx_string(file) +
        ") (xbe D_002415D0 0x2415d0 D_002416B0 0x2416b0 D_00241628 0x241628))");
    std::printf("skeletons: %zu nodes\n", done.size());
    return true;
}
