#pragma once
// The cook: data/ to cooked/ packs (docs/assets.md, "Packs"). Linked by the game, which cooks on
// first run and re-cooks a stale pack when it opens it, and run alone as pc_cook.
#include "cook/cook_plan.hpp"

#include <atomic>
#include <string>
#include <vector>

struct CookOptions {
    std::string data_dir;
    std::string cooked_dir;
    int threads = 0;    // 0: the machine's, at most 16
    bool force = false; // every pack, fresh or not
    bool quiet = false;
    // set from another thread to stop: no pack is started after it, and a pack under way is
    // dropped unwritten (the first-run setup's Cancel)
    const std::atomic<bool>* cancel = nullptr;
};

struct CookReport {
    int packs = 0;  // in the plan
    int cooked = 0; // written this time
    int textures = 0;
    std::uint64_t bytes = 0; // written
    double seconds = 0;
};

std::string pack_path(const std::string& cooked_dir, const std::string& name);
// a pack file made from this plan entry whose sources haven't changed
bool pack_fresh(const CookOptions& options, const CookPlan& plan, const PackPlan& pack);

// the plan from data/, then every stale pack (all of them the first time); false with error
// "cancelled" when options.cancel stopped it
bool cook_all(const CookOptions& options, CookReport& report, std::string& error);
// the named packs from a plan already made (the core pack's); never the core pack itself
bool cook_packs(const CookOptions& options, const CookPlan& plan,
                const std::vector<std::string>& names, CookReport& report, std::string& error);
