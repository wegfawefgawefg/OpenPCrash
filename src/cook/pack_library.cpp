#include "cook/pack_library.hpp"

#include "cook/cook.hpp"

#include <cstdio>

namespace {

CookOptions options_of(const PackLibrary& library) {
    CookOptions options;
    options.data_dir = library.data_dir;
    options.cooked_dir = library.cooked_dir;
    return options;
}

} // namespace

bool packs_start(PackLibrary& library, const std::string& data_dir, const std::string& cooked_dir,
                 std::string& error) {
    library.data_dir = data_dir;
    library.cooked_dir = cooked_dir;
    library.open.clear();
    const CookOptions options = options_of(library);
    const std::string core_path = pack_path(cooked_dir, "core");
    std::string why;
    if (!pack_read(core_path, library.core, why) || !pack_sources_fresh(library.core, data_dir)) {
        // first run, or data/'s tables changed: everything stale
        pack_release(library.core);
        CookReport report;
        if (!cook_all(options, report, error) || !pack_read(core_path, library.core, error)) {
            return false;
        }
    }
    const PackEntry* plan = pack_find(library.core, PackKind::plan, -1);
    if (!plan) {
        error = "the core pack has no plan";
        return false;
    }
    BlobReader reader = pack_reader(library.core, *plan);
    reader(library.plan);
    if (!reader.ok) {
        error = "the core pack's plan is short";
        return false;
    }
    return true;
}

const Pack* pack_open(PackLibrary& library, const std::string& name, std::string& error) {
    const auto found = library.open.find(name);
    if (found != library.open.end()) {
        return found->second.get();
    }
    if (name == "core") {
        return &library.core;
    }
    const PackPlan* plan = plan_pack(library.plan, name);
    if (!plan) {
        error = "no pack " + name + " in the plan";
        return nullptr;
    }
    auto pack = std::make_unique<Pack>();
    std::string why;
    const std::string path = pack_path(library.cooked_dir, name);
    const bool fresh = pack_read(path, *pack, why) &&
                       pack->header->plan_hash == pack_plan_hash(library.plan, *plan) &&
                       pack_sources_fresh(*pack, library.data_dir);
    if (!fresh) {
        pack_release(*pack);
        CookReport report;
        if (!cook_packs(options_of(library), library.plan, {name}, report, error) ||
            !pack_read(path, *pack, error)) {
            return nullptr;
        }
    }
    const Pack* opened = pack.get();
    library.open[name] = std::move(pack);
    return opened;
}

void pack_close(PackLibrary& library, const std::string& name) {
    library.open.erase(name);
}
