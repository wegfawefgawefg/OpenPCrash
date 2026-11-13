#pragma once
// The game's cooked/ packs: the core pack held from start, the others opened for a stage, a
// fighter or a screen and closed when it's done with them. A pack that's missing or stale is
// cooked when it's opened; a missing or stale core pack cooks everything stale first.
#include "cook/cook_plan.hpp"
#include "cook/pack.hpp"

#include <map>
#include <memory>
#include <string>

struct PackLibrary {
    std::string data_dir;
    std::string cooked_dir;
    Pack core;
    CookPlan plan; // read from the core pack
    std::map<std::string, std::unique_ptr<Pack>> open;
};

bool packs_start(PackLibrary& library, const std::string& data_dir, const std::string& cooked_dir,
                 std::string& error);
// the pack, opened (or already open); null with why on failure
const Pack* pack_open(PackLibrary& library, const std::string& name, std::string& error);
void pack_close(PackLibrary& library, const std::string& name);
