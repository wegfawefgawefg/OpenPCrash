// The stand's loader (model_viewer_service, model_viewer_thread): one machine
// read at a time on a worker while the screen runs, uploaded on the main thread once the read is
// done. Retail keeps one request slot the screen overwrites with its latest wish; so does this:
// only the newest ask waits behind the load in flight. The packs a load reads stay open from its
// start to its upload, and every screen's destroy waits for it first (stand_settle), so nothing
// closes them under the worker.
#include "cook/pack_library.hpp"
#include "perf_log.hpp"
#include "screens/garage/model_stand.hpp"
#include "screens/ui/ui.hpp"

#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>

namespace {

int class_of(const std::vector<int>& table, int id) {
    return id >= 0 && id < static_cast<int>(table.size()) ? table[static_cast<std::size_t>(id)]
                                                          : -1;
}

// a finished read: uploaded and kept, or noted as broken; its packs closed
void finish(Ui& ui, ModelStand& stand) {
    StandJob& job = *stand.job;
    const bool ok = job.read.get();
    if (ok) {
        const std::uint64_t upload_start = perf_now();
        mech_load_upload(*job.mech, job.load, *ui.renderer, stand.textures);
        SDL_Log("load: stand machine %s in %.1f ms (its upload %.1f ms)", job.key.c_str(),
                static_cast<double>(perf_now() - job.started) * 1e-6,
                static_cast<double>(perf_now() - upload_start) * 1e-6);
        stand.mechs.emplace(job.key, std::move(job.mech));
    } else {
        std::fprintf(stderr, "model stand: %s\n", job.load.error.c_str());
        stand.broken.insert(job.key);
    }
    mech_load_close(job.load, *ui.data.packs);
    stand.job.reset();
}

// the ask, opened here and read on the worker
void begin(Ui& ui, ModelStand& stand) {
    stand.asked = false;
    stand.fighters.builds.back() = stand.asked_row;
    auto job = std::make_unique<StandJob>();
    job->key = stand.asked_key;
    job->started = perf_now();
    job->mech = std::make_unique<MechParts>();
    std::string error;
    if (!mech_load_open(*job->mech, job->load, stand.fighters,
                        static_cast<int>(stand.fighters.builds.size()) - 1, stand.manifest,
                        *ui.data.packs, error, stand.asked_parts_only)) {
        std::fprintf(stderr, "model stand: %s\n", error.c_str());
        stand.broken.insert(job->key);
        return;
    }
    MechParts* mech = job->mech.get();
    MechLoad* load = &job->load;
    job->read =
        std::async(std::launch::async, [mech, load] { return mech_load_read(*mech, *load); });
    stand.job = std::move(job);
}

} // namespace

MechParts* stand_load(Ui& ui, ModelStand& stand, const ShopBuild& b, bool need_chip, bool* complete,
                      bool* loading) {
    const bool whole = b.body >= 0 && b.leg >= 0 && (!need_chip || b.chip >= 0);
    if (complete) {
        *complete = whole;
    }
    if (loading) {
        *loading = false;
    }
    bool present[6];
    stand_parts_present(b, present);
    bool any = false;
    for (bool p : present) {
        any = any || p;
    }
    if (!stand.started || !any) {
        return nullptr;
    }
    // the machine's look: its classes and paint
    const FighterTables& t = stand.fighters;
    char key[112];
    std::snprintf(key, sizeof key, "%d %d %d %d %d %d %d %d%s", class_of(t.body_class, b.body),
                  class_of(t.leg_class, b.leg), class_of(t.arm_class, b.weapon[0]),
                  class_of(t.arm_class, b.weapon[1]), class_of(t.back_class, b.weapon[2]),
                  class_of(t.back_class, b.weapon[3]), b.color, b.sticker, whole ? "" : " parts");
    const auto found = stand.mechs.find(key);
    if (found != stand.mechs.end()) {
        return found->second.get();
    }
    if (stand.broken.count(key)) {
        return nullptr;
    }
    if (loading) {
        *loading = true;
    }
    if (stand.job && stand.job->key == key) {
        return nullptr;
    }
    // the request: the build as the stand's own row
    FighterBuild& row = stand.asked_row;
    row = FighterBuild{};
    row.body = b.body;
    row.leg = b.leg;
    for (int m = 0; m < 4; ++m) {
        row.weapons[m] = b.weapon[m];
    }
    row.colour = b.color;
    row.sticker = b.sticker;
    row.name = "stand";
    stand.asked = true;
    stand.asked_key = key;
    stand.asked_parts_only = !whole;
    stand_service(ui, stand);
    return nullptr;
}

void stand_service(Ui& ui, ModelStand& stand) {
    if (!stand.started || !ui.renderer) {
        return;
    }
    if (stand.job &&
        stand.job->read.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        finish(ui, stand);
    }
    if (!stand.job && stand.asked) {
        begin(ui, stand);
    }
}

void stand_settle(Ui& ui, ModelStand& stand) {
    stand.asked = false;
    if (stand.job) {
        stand.job->read.wait();
        finish(ui, stand);
    }
}

bool stand_idle(const ModelStand& stand) {
    return !stand.job && !stand.asked;
}
