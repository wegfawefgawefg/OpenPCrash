#include "cook/cook.hpp"

#include "cook/cook_pc_art.hpp"
#include "cook/cook_read.hpp"
#include "cook/cook_textures.hpp"
#include "cook/pack.hpp"
#include "cook/pack_write.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace {

struct TextureJob {
    std::string path; // relative to data/
    bool mips = true;
    bool from_picture = false; // asked for by a model, not the plan: a missing file only warns
};

// one stale pack on its way to cooked/
struct PackJob {
    const PackPlan* plan = nullptr;
    PackBuild build;
    std::vector<TextureJob> textures;
    std::vector<CookedTexture> cooked;
    std::vector<std::string> texture_errors;
    std::atomic<int> remaining{0};
    std::string error;
};

int thread_count(const CookOptions& options) {
    const int machine = static_cast<int>(std::thread::hardware_concurrency());
    return options.threads > 0 ? options.threads : std::clamp(machine, 1, 16);
}

// fn(i) for i in [0, count) on the given threads, in index order as far as they go
template <class Fn> void parallel_for(int count, int threads, Fn fn) {
    std::atomic<int> next{0};
    const auto work = [&] {
        for (int i = next++; i < count; i = next++) {
            fn(i);
        }
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < std::min(threads, count); ++t) {
        pool.emplace_back(work);
    }
    work();
    for (std::thread& thread : pool) {
        thread.join();
    }
}

bool read_text(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// everything in a pack but its textures, which it lists for the second pass
bool cook_items(const CookOptions& options, const CookPlan& plan, const CoreTables* core,
                PackJob& job) {
    std::set<std::string> texture_paths;
    const auto want_texture = [&](const std::string& path, bool mips, bool picture) {
        if (!path.empty() && texture_paths.insert(path).second) {
            job.textures.push_back({path, mips, picture});
        }
    };
    PackBuild& build = job.build;
    for (const CookItem& item : job.plan->items) {
        const std::string full = options.data_dir + "/" + item.path;
        std::string& error = job.error;
        switch (item.kind) {
        case CookItemKind::texture:
        case CookItemKind::sprite:
            want_texture(item.path, item.kind == CookItemKind::texture, false);
            break;
        case CookItemKind::model_set: {
            ModelSet set;
            if (!read_model_set(options.data_dir, item.path, set, error)) {
                return false;
            }
            for (ModelObject& object : set.objects) {
                for (ModelSection& section : object.sections) {
                    if (item.flags & item_no_pictures) {
                        section.picture.clear();
                    }
                    want_texture(section.picture, true, true);
                }
            }
            build_add_value(build, PackKind::model_set, item.id, item.path, set);
            build.sources.push_back(item.path);
            const std::string bin = item.path.substr(0, item.path.size() - 5) + ".bin";
            if (std::filesystem::exists(options.data_dir + "/" + bin)) {
                build.sources.push_back(bin);
            }
            break;
        }
        case CookItemKind::motion_set: {
            MotionSet set;
            if (!read_motion_set(full, set, error)) {
                return false;
            }
            build_add_value(build, PackKind::motion_set, item.id, item.path, set);
            build.sources.push_back(item.path);
            break;
        }
        case CookItemKind::collision: {
            CollisionGrid grid;
            if (!read_collision_grid(full, grid, error)) {
                return false;
            }
            build_add_value(build, PackKind::collision, item.id, item.path, grid);
            build.sources.push_back(item.path);
            break;
        }
        case CookItemKind::sexp: {
            std::vector<std::uint8_t> text;
            if (!read_text(full, text)) {
                error = "can't read " + full;
                return false;
            }
            build_add(build, PackKind::sexp, -1, item.path, std::move(text));
            build.sources.push_back(item.path);
            break;
        }
        case CookItemKind::pc_art: {
            std::vector<std::uint8_t> text;
            std::vector<std::string> sources;
            if (!cook_pc_art(options.data_dir, text, sources, error)) {
                return false;
            }
            build_add(build, PackKind::sexp, -1, item.path, std::move(text));
            build.sources.push_back("ui/images.sexp");
            build.sources.insert(build.sources.end(), sources.begin(), sources.end());
            break;
        }
        case CookItemKind::manifest:
        case CookItemKind::stage_tables:
        case CookItemKind::fighter_tables:
        case CookItemKind::plan: {
            if (!core) {
                error = "the core pack is cooked only from data/'s tables (cook_all)";
                return false;
            }
            const CoreTables& values = *core;
            if (item.kind == CookItemKind::manifest) {
                build_add_value(build, PackKind::manifest, -1, "", values.manifest);
            } else if (item.kind == CookItemKind::stage_tables) {
                build_add_value(build, PackKind::stage_tables, -1, "", values.stages);
            } else if (item.kind == CookItemKind::fighter_tables) {
                build_add_value(build, PackKind::fighter_tables, -1, "", values.fighters);
            } else {
                build_add_value(build, PackKind::plan, -1, "", values.plan);
                build.sources.insert(build.sources.end(), plan.core_sources.begin(),
                                     plan.core_sources.end());
            }
            break;
        }
        }
    }
    job.cooked.resize(job.textures.size());
    job.texture_errors.resize(job.textures.size());
    job.remaining = static_cast<int>(job.textures.size());
    return true;
}

// the cooked textures into the pack, then the pack to disk
bool finish_pack(const CookOptions& options, const CookPlan& plan, PackJob& job,
                 std::uint64_t& written) {
    for (std::size_t i = 0; i < job.textures.size(); ++i) {
        const TextureJob& texture = job.textures[i];
        if (!job.texture_errors[i].empty()) {
            if (!texture.from_picture) {
                job.error = job.texture_errors[i];
                return false;
            }
            std::fprintf(stderr, "cook: %s: %s (skipped)\n", job.plan->name.c_str(),
                         job.texture_errors[i].c_str());
            continue;
        }
        CookedTexture& cooked = job.cooked[i];
        build_add(job.build, PackKind::texture, -1, texture.path, std::move(cooked.blocks));
        PackEntry& entry = job.build.entries.back().entry;
        entry.format = cooked.format;
        entry.width = cooked.width;
        entry.height = cooked.height;
        entry.levels = cooked.levels;
        job.build.sources.push_back(texture.path);
    }
    const bool ok =
        write_pack(job.build, pack_path(options.cooked_dir, job.plan->name), options.data_dir,
                   pack_plan_hash(plan, *job.plan), written, job.error);
    job.build = PackBuild{};
    job.cooked.clear();
    return ok;
}

bool run_cook(const CookOptions& options, const CookPlan& plan, const CoreTables* core,
              const std::vector<const PackPlan*>& packs, CookReport& report, std::string& error) {
    const std::uint64_t start = SDL_GetTicksNS();
    const int threads = thread_count(options);
    std::error_code failed;
    std::filesystem::create_directories(options.cooked_dir, failed);
    cook_textures_init();
    std::vector<std::unique_ptr<PackJob>> jobs;
    for (const PackPlan* pack : packs) {
        jobs.push_back(std::make_unique<PackJob>());
        jobs.back()->plan = pack;
    }
    if (!options.quiet && !jobs.empty()) {
        std::printf("cook: %zu of %zu packs to %s on %d threads\n", jobs.size(), plan.packs.size(),
                    options.cooked_dir.c_str(), threads);
    }

    // pass 1: models, motion, tables, collision; which textures each pack needs
    std::atomic<std::uint64_t> written_total{0};
    std::atomic<int> cooked_packs{0};
    const auto finish = [&](PackJob& job) {
        std::uint64_t written = 0;
        if (job.error.empty() && finish_pack(options, plan, job, written)) {
            written_total += written;
            ++cooked_packs;
        }
    };
    parallel_for(static_cast<int>(jobs.size()), threads, [&](int i) {
        PackJob& job = *jobs[i];
        if (cook_items(options, plan, core, job) && job.textures.empty()) {
            finish(job);
        }
    });

    // pass 2: every texture of every pack, pack by pack; the last texture of a pack writes it
    std::vector<std::pair<int, int>> textures;
    for (std::size_t p = 0; p < jobs.size(); ++p) {
        if (jobs[p]->error.empty()) {
            for (std::size_t t = 0; t < jobs[p]->textures.size(); ++t) {
                textures.push_back({static_cast<int>(p), static_cast<int>(t)});
            }
        }
    }
    parallel_for(static_cast<int>(textures.size()), threads, [&](int i) {
        PackJob& job = *jobs[textures[i].first];
        const int t = textures[i].second;
        const TextureJob& texture = job.textures[t];
        cook_texture(options.data_dir + "/" + texture.path, plan_hint(plan, texture.path),
                     texture.mips, job.cooked[t], job.texture_errors[t]);
        if (--job.remaining == 0) {
            finish(job);
        }
    });

    report.packs = static_cast<int>(plan.packs.size());
    report.cooked += cooked_packs;
    report.textures += static_cast<int>(textures.size());
    report.bytes += written_total;
    report.seconds += static_cast<double>(SDL_GetTicksNS() - start) / 1e9;
    for (const auto& job : jobs) {
        if (!job->error.empty()) {
            std::fprintf(stderr, "cook: %s: %s\n", job->plan->name.c_str(), job->error.c_str());
            if (error.empty()) {
                error = job->plan->name + ": " + job->error;
            }
        }
    }
    if (!options.quiet && !jobs.empty()) {
        std::printf("cook: %d packs, %d textures, %.1f MB in %.2f s\n", cooked_packs.load(),
                    static_cast<int>(textures.size()),
                    static_cast<double>(written_total) / 1048576.0,
                    static_cast<double>(SDL_GetTicksNS() - start) / 1e9);
    }
    return error.empty();
}

} // namespace

std::string pack_path(const std::string& cooked_dir, const std::string& name) {
    return cooked_dir + "/" + name + ".pack";
}

bool pack_fresh(const CookOptions& options, const CookPlan& whole, const PackPlan& plan) {
    Pack pack;
    std::string error;
    return pack_read(pack_path(options.cooked_dir, plan.name), pack, error) &&
           pack.header->plan_hash == pack_plan_hash(whole, plan) &&
           pack_sources_fresh(pack, options.data_dir);
}

bool cook_all(const CookOptions& options, CookReport& report, std::string& error) {
    CoreTables core;
    if (!make_cook_plan(options.data_dir, core, error)) {
        return false;
    }
    std::vector<const PackPlan*> stale;
    for (const PackPlan& pack : core.plan.packs) {
        if (options.force || !pack_fresh(options, core.plan, pack)) {
            stale.push_back(&pack);
        }
    }
    return run_cook(options, core.plan, &core, stale, report, error);
}

bool cook_packs(const CookOptions& options, const CookPlan& plan,
                const std::vector<std::string>& names, CookReport& report, std::string& error) {
    std::vector<const PackPlan*> packs;
    for (const std::string& name : names) {
        const PackPlan* pack = plan_pack(plan, name);
        if (!pack || name == "core") {
            error = "no pack " + name + " in the plan";
            return false;
        }
        packs.push_back(pack);
    }
    return run_cook(options, plan, nullptr, packs, report, error);
}
