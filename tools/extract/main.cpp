// pc_extract: reads the original disc and the retail XBE once and writes data/, the editable
// standard-format copy the game is built from (docs/assets.md). One family a source pair; each
// writes its own folder and data/manifest/<family>.sexp, merged into data/manifest.sexp at the end
// (extract_run.hpp).
//
//   pc_extract [--only family[,family]] [--assets id,id...] [--disc PATH] [--xbe FILE] [--out DIR]
//
// --disc is an extracted disc (the folder holding Media/, with or without default.xbe), an Xbox
// disc image (XISO) or a full Redump-style image (disc_image.hpp). The XBE is --xbe if given, else
// the disc's default.xbe, else the build's default (an extracted folder kept without it).
#include "disc.hpp"
#include "extract.hpp"
#include "extract_run.hpp"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split_commas(const std::string& text) {
    std::vector<std::string> parts;
    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, ',')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

} // namespace

int main(int argc, char** argv) {
    std::string disc_dir = PHANTOMCRASH_DISC_DIR;
    std::string xbe_path; // given with --xbe
    ExtractOptions options;
    options.out_dir = PHANTOMCRASH_DATA_DIR;
    std::vector<std::string> only;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--only" && has_value) {
            only = split_commas(argv[++i]);
        } else if (arg == "--assets" && has_value) {
            for (const std::string& id : split_commas(argv[++i])) {
                options.only_assets.push_back(std::atoi(id.c_str()));
            }
        } else if (arg == "--disc" && has_value) {
            disc_dir = argv[++i];
        } else if (arg == "--xbe" && has_value) {
            xbe_path = argv[++i];
        } else if (arg == "--out" && has_value) {
            options.out_dir = argv[++i];
        } else {
            std::fprintf(stderr, "pc_extract [--only family,...] [--assets id,...] [--disc PATH] "
                                 "[--xbe FILE] [--out DIR]\n"
                                 "  --disc: an extracted disc folder, an XISO or a Redump image\n"
                                 "families:");
            for (int family = 0; family < extract_family_count(); ++family) {
                std::fprintf(stderr, " %s", extract_family_name(family));
            }
            std::fprintf(stderr, "\n");
            return arg == "--help" || arg == "-h" ? 0 : 2;
        }
    }
    for (const std::string& name : only) {
        if (!extract_family_known(name)) {
            std::fprintf(stderr, "pc_extract: no family %s\n", name.c_str());
            return 2;
        }
    }

    // open the original
    Disc disc;
    DiscProblem problem = DiscProblem::none;
    std::string error;
    if (!open_disc(disc, disc_dir, xbe_path, PHANTOMCRASH_XBE_PATH, problem, error)) {
        std::fprintf(stderr, "pc_extract: %s\n", error.c_str());
        return 1;
    }
    std::printf("pc_extract: %zu registry rows, %s, out %s\n", disc.assets.size(),
                disc_source_label(disc.source).c_str(), options.out_dir.c_str());

    // run the families asked for
    ExtractProgress progress; // for the counts it prints
    if (!run_extract(disc, options, only, &progress, error)) {
        std::fprintf(stderr, "pc_extract: %s\n", error.c_str());
        return 1;
    }
    SDL_Quit();
    return 0;
}
