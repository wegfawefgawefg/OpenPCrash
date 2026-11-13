#pragma once
// What every family is handed: the opened disc, where data/ goes, and an optional list of asset
// ids to limit the run to (--assets, for trying one stage or one mech quickly).
#include "disc.hpp"
#include "manifest.hpp"

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

// how far a run is, and a stop asked for (the game's first-run screen, extract_run.hpp)
struct ExtractProgress {
    std::atomic<int> permille{0}; // 0..1000: the families done (extract_permille adds the one running)
    std::atomic<int> family{-1};  // the family running (extract_family_name)
    std::atomic<int> span{0};     // the share the running family adds when it's done
    std::atomic<int> family_assets{0}; // the asset_wanted calls it makes (0: unknown)
    std::atomic<int> assets_seen{0};   // and those made so far
    std::atomic<bool> cancel{false};
};

struct ExtractOptions {
    std::string out_dir;
    std::vector<int> only_assets; // empty: all
    ExtractProgress* progress = nullptr;
};

// every family asks this of each asset it might write, so it counts them for the progress and
// says no to the rest once a stop is asked for
inline bool asset_wanted(const ExtractOptions& options, int id) {
    if (options.progress) {
        options.progress->assets_seen.fetch_add(1, std::memory_order_relaxed);
        if (options.progress->cancel.load(std::memory_order_relaxed)) {
            return false;
        }
    }
    return options.only_assets.empty() ||
           std::find(options.only_assets.begin(), options.only_assets.end(), id) !=
               options.only_assets.end();
}

// "1558_b00_shinjuku", "1684_ascii_font_sys_tex": the asset id, then the loose file's stem or the
// cabinet's name, so a folder says where it came from
std::string asset_folder_name(const Disc& disc, int id);
