#include "extract_run.hpp"

#include "addresses.hpp"
#include "audio.hpp"
#include "collision.hpp"
#include "effects.hpp"
#include "fighters.hpp"
#include "manifest.hpp"
#include "models.hpp"
#include "motion.hpp"
#include "nav.hpp"
#include "scenes.hpp"
#include "scripts.hpp"
#include "skeletons.hpp"
#include "stage_props.hpp"
#include "stages.hpp"
#include "systemdata.hpp"
#include "tables.hpp"
#include "text.hpp"
#include "textures.hpp"
#include "ui.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>

namespace {

struct Family {
    const char* name;
    const char* label; // what a player is told it's doing
    bool (*run)(const Disc&, const ExtractOptions&, Manifest&);
    // its share of a whole run's time in thousandths, and its asset_wanted calls (0: it asks
    // none), so the bar moves through a family as it goes (measured on the US disc, 2026-11-12:
    // 45 s in all, 37 of them the textures)
    int weight;
    int assets;
};

// every family, in the order they run
const Family families[] = {
    // clang-format off
    {"textures", "Pictures", extract_textures, 820, 1304},
    {"systemdata", "Pictures", extract_systemdata, 2, 0},
    {"models", "Models", extract_models, 3, 226},
    {"stages", "Stages", extract_stages, 2, 0},
    {"stage-props", "Stages", extract_stage_props, 1, 0},
    {"fighters", "Machines", extract_fighters, 1, 0},
    {"skeletons", "Machines", extract_skeletons, 1, 0},
    {"motion", "Animation", extract_motion, 3, 124},
    {"tables", "Game tables", extract_tables, 1, 0},
    {"effects", "Effects", extract_effects, 1, 0},
    {"audio", "Sound and music", extract_audio, 74, 0},
    {"text", "Text", extract_text, 5, 0},
    {"scripts", "Story scripts", extract_scripts, 1, 0},
    {"collision", "Stages", extract_collision, 1, 4},
    {"nav", "Stages", extract_nav, 1, 159},
    {"ui", "Menus", extract_ui, 80, 0},
    {"scenes", "Scenes", extract_scenes, 1, 0},
    // clang-format on
};
constexpr int family_count = static_cast<int>(sizeof families / sizeof families[0]);

} // namespace

std::string asset_folder_name(const Disc& disc, int id) {
    const AssetRecord& record = disc.assets[id];
    std::string stem;
    if (record.cabinet < 0) {
        const std::size_t slash = record.path.find_last_of('\\');
        stem = record.path.substr(slash == std::string::npos ? 0 : slash + 1);
        const std::size_t dot = stem.rfind('.');
        stem = dot == std::string::npos ? stem : stem.substr(0, dot);
    } else {
        stem = disc.cabinet_names[record.cabinet];
    }
    char number[16];
    std::snprintf(number, sizeof number, "%04d_", id);
    return number + stem;
}

int extract_family_count() {
    return family_count;
}

const char* extract_family_name(int family) {
    return family >= 0 && family < family_count ? families[family].name : "";
}

const char* extract_family_label(int family) {
    return family >= 0 && family < family_count ? families[family].label : "";
}

bool extract_family_known(const std::string& name) {
    return std::any_of(std::begin(families), std::end(families),
                       [&](const Family& family) { return name == family.name; });
}

bool run_extract(const Disc& disc, ExtractOptions options, const std::vector<std::string>& only,
                 ExtractProgress* progress, std::string& error) {
    options.progress = progress;
    // the families asked for, and their share of the bar
    bool wanted[family_count] = {};
    int total = 0;
    for (int i = 0; i < family_count; ++i) {
        wanted[i] = only.empty() || std::find(only.begin(), only.end(), families[i].name) !=
                                        only.end();
        total += wanted[i] ? families[i].weight : 0;
    }
    total = std::max(total, 1);

    // run them, each writing its manifest
    int done = 0;
    for (int i = 0; i < family_count; ++i) {
        if (!wanted[i]) {
            continue;
        }
        if (progress) {
            if (progress->cancel.load()) {
                error = "stopped";
                return false;
            }
            progress->family = i;
            progress->assets_seen = 0;
            progress->family_assets = families[i].assets;
            progress->span = families[i].weight * 1000 / total;
        }
        Manifest manifest;
        manifest.family = families[i].name;
        const std::uint64_t started = SDL_GetTicksNS();
        const bool ran = families[i].run(disc, options, manifest);
        std::printf("extract: %s in %.1f s (%d assets asked)\n", families[i].name,
                    static_cast<double>(SDL_GetTicksNS() - started) / 1e9,
                    progress ? progress->assets_seen.load() : -1);
        if (progress && progress->cancel.load()) {
            error = "stopped";
            return false;
        }
        if (!ran || !write_family_manifest(options.out_dir, manifest)) {
            error = std::string("the ") + families[i].name + " family failed";
            return false;
        }
        done += families[i].weight;
        if (progress) {
            progress->family_assets = 0;
            progress->permille = done * 1000 / total;
        }
    }
    if (!merge_manifests(options.out_dir)) {
        error = "can't write " + options.out_dir + "/manifest.sexp";
        return false;
    }
    if (progress) {
        progress->permille = 1000;
    }
    return true;
}

int extract_permille(const ExtractProgress& progress) {
    const int expected = progress.family_assets.load();
    const int seen = std::min(progress.assets_seen.load(), expected);
    const int within = expected > 0 ? progress.span.load() * seen / expected : 0;
    return std::min(1000, progress.permille.load() + within);
}
