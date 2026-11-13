#include "nav.hpp"

#include "disc.hpp"
#include "extract.hpp"
#include "sexp_out.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace {

// ai_nav_resource_selectors: the ENPT asset of each stage selector 0..7
constexpr std::uint32_t nav_selectors_va = 0x20614C;

struct Bytes {
    const std::vector<std::uint8_t>& data;
    bool has(std::size_t at, std::size_t n) const {
        return at + n <= data.size();
    }
    std::uint8_t u8(std::size_t at) const {
        return at < data.size() ? data[at] : 0;
    }
    std::uint32_t u32(std::size_t at) const {
        std::uint32_t value = 0;
        if (has(at, 4)) {
            std::memcpy(&value, data.data() + at, 4);
        }
        return value;
    }
    float f32(std::size_t at) const {
        const std::uint32_t bits = u32(at);
        float value;
        std::memcpy(&value, &bits, 4);
        return value;
    }
};

// a point in the port frame: z negated
std::string point(const Bytes& in, std::size_t at) {
    const float z = in.f32(at + 8);
    return sx_float(in.f32(at)) + " " + sx_float(in.f32(at + 4)) + " " +
           sx_float(z == 0.0f ? 0.0f : -z);
}

// the graph as text, or empty with the reason in error
std::string graph_text(const Bytes& in, std::string& error) {
    if (!in.has(0, 0xC) || std::memcmp(in.data.data(), "ENPT", 4) != 0) {
        error = "no ENPT magic";
        return {};
    }
    const int count = in.u8(0xB);
    std::map<std::uint32_t, int> index_of;
    for (int i = 0; i < count; ++i) {
        index_of[in.u32(0xC + 4 * static_cast<std::size_t>(i))] = i;
    }
    std::string text;
    for (int i = 0; i < count; ++i) {
        const std::size_t node = in.u32(0xC + 4 * static_cast<std::size_t>(i));
        const int links = in.u8(node + 3);
        const std::size_t spline = in.u32(node + 8);
        if (!in.has(node, 0xC + 4 * static_cast<std::size_t>(links)) || !in.has(spline, 4)) {
            error = "node " + std::to_string(i) + " runs past the file";
            return {};
        }
        const std::uint32_t segments = in.u32(spline);
        if (!in.has(spline + 4, 12 + 36 * static_cast<std::size_t>(segments))) {
            error = "node " + std::to_string(i) + "'s spline runs past the file";
            return {};
        }
        text += "\n  (node (index " + sx_int(i) + ") (id " + sx_int(in.u8(node)) + " " +
                sx_int(in.u8(node + 1)) + ") (kind " + sx_int(in.u8(node + 2)) + ")\n    (links";
        for (int k = 0; k < links; ++k) {
            const auto found = index_of.find(in.u32(node + 0xC + 4 * static_cast<std::size_t>(k)));
            if (found == index_of.end()) {
                error = "node " + std::to_string(i) + " links to no node";
                return {};
            }
            text += " " + sx_int(found->second);
        }
        text += ")\n    (segments " + sx_int(segments) + ")\n    (points";
        for (std::uint32_t p = 0; p < 1 + 3 * segments; ++p) {
            text += (p % 3 == 0 ? "\n      " : "  ") + point(in, spline + 4 + 12 * p);
        }
        text += "))";
    }
    return text;
}

} // namespace

bool extract_nav(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    int graphs = 0;
    for (int id = 0; id < static_cast<int>(disc.assets.size()); ++id) {
        if (disc.assets[id].kind != 10 || !asset_wanted(options, id)) {
            continue;
        }
        const std::vector<std::uint8_t> bytes = read_asset(disc, id);
        const Bytes in{bytes};
        std::string error;
        const std::string nodes = graph_text(in, error);
        if (nodes.empty()) {
            std::fprintf(stderr, "nav: asset %d: %s\n", id, error.c_str());
            continue;
        }
        std::string selectors;
        for (int s = 0; s < 8; ++s) {
            if (static_cast<int>(xbe_u32(disc.xbe, nav_selectors_va + 4 * s)) == id) {
                selectors += " " + sx_int(s);
            }
        }
        const std::string file = "nav/" + asset_folder_name(disc, id) + ".sexp";
        std::string text = ";; ENPT route graph " + std::to_string(id) + " (" +
                           asset_source(disc, id) + "), see tools/extract/nav.hpp\n";
        text +=
            ";; node: table index, the id bytes (+0, +1), kind (+2), links as node indices,\n"
            ";; the spline's segment count and its points (start, then control, control, end a\n"
            ";; segment), port frame\n";
        text += "(enpt (asset " + sx_int(id) + ") (selectors" + selectors + ") (nodes " +
                sx_int(in.u8(0xB)) + ")" + nodes + ")\n";
        std::error_code made;
        std::filesystem::create_directories(options.out_dir + "/nav", made);
        std::ofstream out(options.out_dir + "/" + file, std::ios::binary);
        out << text;
        if (!out) {
            std::fprintf(stderr, "nav: can't write %s\n", file.c_str());
            return false;
        }
        manifest.entries.push_back("(nav-graph (asset " + sx_int(id) + ") (source " +
                                   sx_string(asset_source(disc, id)) + ") (nodes " +
                                   sx_int(in.u8(0xB)) + ") (selectors" + selectors + ") (file " +
                                   sx_string(file) + "))");
        ++graphs;
    }
    std::printf("nav: %d graphs\n", graphs);
    return true;
}
