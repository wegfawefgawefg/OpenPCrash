// pc_cook: data/ to cooked/ (docs/assets.md, "Packs"), the game's cook step run alone.
//
//   pc_cook [--data DIR] [--out DIR] [--threads N] [--force] [pack...]
//     no packs: every stale pack (all of them with --force); packs named: those, cooked again
#include "cook/cook.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    CookOptions options;
    options.data_dir = PHANTOMCRASH_DATA_DIR;
    options.cooked_dir = PHANTOMCRASH_COOKED_DIR;
    std::vector<std::string> names;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const char* value = i + 1 < argc ? argv[i + 1] : nullptr;
        if (std::strcmp(arg, "--data") == 0 && value) {
            options.data_dir = value;
            ++i;
        } else if (std::strcmp(arg, "--out") == 0 && value) {
            options.cooked_dir = value;
            ++i;
        } else if (std::strcmp(arg, "--threads") == 0 && value) {
            options.threads = std::atoi(value);
            ++i;
        } else if (std::strcmp(arg, "--force") == 0) {
            options.force = true;
        } else if (arg[0] != '-') {
            names.push_back(arg);
        } else {
            std::fprintf(stderr,
                         "pc_cook [--data DIR] [--out DIR] [--threads N] [--force] [pack...]\n");
            return 2;
        }
    }

    CookReport report;
    std::string error;
    bool ok = true;
    if (names.empty()) {
        ok = cook_all(options, report, error);
    } else {
        // named packs from a fresh plan; core goes through cook_all
        CoreTables core;
        ok = make_cook_plan(options.data_dir, core, error) &&
             cook_packs(options, core.plan, names, report, error);
    }
    if (!ok) {
        std::fprintf(stderr, "pc_cook: %s\n", error.c_str());
        return 1;
    }
    std::printf("pc_cook: %d packs in the plan, %d cooked, %d textures, %.1f MB, %.2f s\n",
                report.packs, report.cooked, report.textures,
                static_cast<double>(report.bytes) / 1048576.0, report.seconds);
    return 0;
}
