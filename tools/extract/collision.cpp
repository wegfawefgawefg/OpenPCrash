#include "collision.hpp"

#include "disc.hpp"
#include "extract.hpp"
#include "sexp_out.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

struct Bytes {
    const std::vector<std::uint8_t>& data;
    std::uint32_t u32(std::size_t at) const {
        std::uint32_t value = 0;
        if (at + 4 <= data.size()) {
            std::memcpy(&value, data.data() + at, 4);
        }
        return value;
    }
    std::int32_t i32(std::size_t at) const {
        return static_cast<std::int32_t>(u32(at));
    }
    float f32(std::size_t at) const {
        const std::uint32_t bits = u32(at);
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    }
};

// CollisionTri, 0x34: v0, v1, v2, normal, type
std::string triangles(const Bytes& in, std::size_t at, std::uint32_t count) {
    std::string text;
    for (std::uint32_t t = 0; t < count; ++t) {
        const std::size_t tri = at + t * 0x34;
        text += "\n    (";
        for (int k = 0; k < 12; ++k) {
            const float value = in.f32(tri + k * 4);
            text += (k ? " " : "") + sx_float(k % 3 == 2 ? (value == 0.0f ? 0.0f : -value) : value);
        }
        text += " " + sx_int(in.i32(tri + 0x30)) + ")";
    }
    return text;
}

std::string ints(const Bytes& in, std::size_t at, std::size_t count) {
    std::string text;
    for (std::size_t i = 0; i < count; ++i) {
        text += (i % 32 ? " " : "\n    ") + sx_int(in.i32(at + i * 4));
    }
    return text;
}

} // namespace

bool extract_collision(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    int grids = 0;
    for (int id = 0; id < static_cast<int>(disc.assets.size()); ++id) {
        if (disc.assets[id].kind != 1 || !asset_wanted(options, id)) {
            continue;
        }
        const std::vector<std::uint8_t> bytes = read_asset(disc, id);
        const Bytes in{bytes};
        // header: dims[3], cell[3], tri count, ref count, cells[32][4][32], two pointer slots,
        // floor tri and ref counts, floor cells[32][32], two pointer slots, centre[3]; then the
        // lists
        if (bytes.size() < 0x5044 || in.u32(0) != 32 || in.u32(4) != 4 || in.u32(8) != 32) {
            std::fprintf(stderr, "collision: asset %d isn't a 32x4x32 grid\n", id);
            continue;
        }
        const std::uint32_t tris = in.u32(0x18), refs = in.u32(0x1C);
        const std::uint32_t floor_tris = in.u32(0x4028), floor_refs = in.u32(0x402C);
        const std::size_t tri_at = 0x5044;
        const std::size_t ref_at = tri_at + tris * 0x34;
        const std::size_t floor_tri_at = ref_at + refs * 4;
        const std::size_t floor_ref_at = floor_tri_at + floor_tris * 0x34;
        if (floor_ref_at + floor_refs * 4 != bytes.size()) {
            std::fprintf(stderr, "collision: asset %d doesn't close at its size\n", id);
            continue;
        }
        std::string text =
            ";; collision grid " + std::to_string(id) + " (" + asset_source(disc, id) +
            "), see tools/extract/collision.hpp\n(collision-grid (asset " + sx_int(id) + ")\n";
        text += "  (dims 32 4 32) (cell " + sx_float(in.f32(0xC)) + " " + sx_float(in.f32(0x10)) +
                " " + sx_float(in.f32(0x14)) + ")\n";
        text += "  (centre-original " + sx_float(in.f32(0x5038)) + " " + sx_float(in.f32(0x503C)) +
                " " + sx_float(in.f32(0x5040)) + ")  ; the grid's own frame: z not negated\n";
        // collision_surface_bits 0x2056F8: the contact bit of a triangle's type % 1000; 11 words
        // (types 0..10), past them the table runs into other data
        text += "  (surface-bits";
        for (int type = 0; type < 11; ++type) {
            text += " " + sx_hex(xbe_u32(disc.xbe, 0x2056F8 + type * 4));
        }
        text += ")  ; collision_surface_bits by type % 1000\n";
        text += "  (triangles  ; v0 v1 v2 normal (port frame), type" + triangles(in, tri_at, tris) +
                ")\n";
        text += "  (cells  ; first ref of each [x][y][z] cell, -1 none" +
                ints(in, 0x20, 32 * 4 * 32) + ")\n";
        text += "  (refs  ; triangle indices, each cell's run ended by -1" +
                ints(in, ref_at, refs) + ")\n";
        text += "  (floor-triangles" + triangles(in, floor_tri_at, floor_tris) + ")\n";
        text += "  (floor-cells  ; [x][z]" + ints(in, 0x4030, 32 * 32) + ")\n";
        text += "  (floor-refs" + ints(in, floor_ref_at, floor_refs) + "))\n";

        const std::string file = "collision/" + asset_folder_name(disc, id) + ".sexp";
        std::error_code made;
        std::filesystem::create_directories(options.out_dir + "/collision", made);
        std::ofstream out(options.out_dir + "/" + file, std::ios::binary);
        out << text;
        if (!out) {
            std::fprintf(stderr, "collision: can't write %s\n", file.c_str());
            return false;
        }
        manifest.entries.push_back("(collision-grid (asset " + sx_int(id) + ") (source " +
                                   sx_string(asset_source(disc, id)) + ") (triangles " +
                                   sx_int(tris) + ") (floor " + sx_int(floor_tris) + ") (file " +
                                   sx_string(file) + "))");
        ++grids;
    }
    std::printf("collision: %d grids\n", grids);
    return true;
}
