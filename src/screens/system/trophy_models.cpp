#include "screens/system/trophy_models.hpp"

#include "cook/pack_library.hpp"
#include "render/mesh_draw.hpp"
#include "render/mesh_queue.hpp"
#include "screens/ui/ui.hpp"

#include <cmath>
#include <cstdio>

namespace {

// what the trophies upload is theirs (renderer_set_owner), freed with the screen
constexpr int render_owner_trophies = 2;

// the model set whose slots hold `slot`, and the object there; null when none does
const TrophyModels::Loaded* model_of(Ui& ui, TrophyModels& t, int slot, int& object) {
    for (const auto& [asset, entry] : t.manifest.assets) {
        if (entry.kind != "model-set" || entry.first_slot < 0 || slot < entry.first_slot ||
            slot >= entry.first_slot + entry.slot_count) {
            continue;
        }
        object = slot - entry.first_slot;
        auto found = t.sets.find(asset);
        if (found == t.sets.end()) {
            // loaded once: the model, its pictures by slot, uploaded as the trophies' own
            TrophyModels::Loaded loaded;
            std::string error;
            PackLibrary& packs = *ui.data.packs;
            const Pack* pack = entry.pack.empty() ? nullptr : pack_open(packs, entry.pack, error);
            if (pack && load_model_set(*pack, asset, loaded.set, error)) {
                loaded.set.first_slot = entry.first_slot;
                const auto pictures = t.manifest.assets.find(entry.texture_set);
                loaded.texture_base =
                    pictures != t.manifest.assets.end() ? pictures->second.first_slot : 0;
                build_texture_slots(t.slots, t.manifest, {entry.texture_set});
                renderer_set_owner(*ui.renderer, render_owner_trophies);
                t.textures.pack = pack;
                loaded.ok = upload_model_set(*ui.renderer, t.textures, loaded.set, loaded.gpu);
                load_slot_textures(*ui.renderer, t.textures, t.slots, *pack);
                t.textures.pack = nullptr;
                renderer_set_owner(*ui.renderer, 0);
            } else {
                std::fprintf(stderr, "trophy %d: %s\n", asset, error.c_str());
            }
            if (pack) {
                pack_close(packs, entry.pack);
            }
            found = t.sets.emplace(asset, std::move(loaded)).first;
        }
        const TrophyModels::Loaded& loaded = found->second;
        return loaded.ok && object < static_cast<int>(loaded.set.objects.size()) ? &loaded
                                                                                 : nullptr;
    }
    return nullptr;
}

} // namespace

void trophy_draw(Ui& ui, TrophyModels& t, int model_slot, const float position[3],
                 const float rotation[3], float yaw, int pass) {
    if (!ui.renderer || t.failed) {
        return;
    }
    if (!t.started) {
        std::string error;
        if (!load_manifest(ui.data.packs->core, t.manifest, error)) {
            std::fprintf(stderr, "trophies: %s\n", error.c_str());
            t.failed = true;
            return;
        }
        t.started = true;
    }
    int object = 0;
    const TrophyModels::Loaded* loaded = model_of(ui, t, model_slot, object);
    if (!loaded) {
        return;
    }
    Renderer& renderer = *ui.renderer;

    // camera_result_set / the records screen's: eye (0, 0, -10) on the origin, 52 degrees; the
    // port's frame is the original's with z negated and turns about x and y reversed
    const float eye[3] = {0.0f, 0.0f, 10.0f};
    const float whole[4] = {0, 0, 640, 480};
    const float aspect = renderer_scene_aspect(renderer, whole);
    const CameraMatrices matrices = camera_matrices(eye, 3.14159265f, 0.0f, aspect);
    SceneLights lights;
    lights.direction = Vec3f{0.0f, 0.0f, -1.0f}; // menu_light_set((0, 0, 3), 1), normalized
    lights.stage = &t.menu_stage;
    lights.table = &t.menu_table;

    // turned about its own y by the spin, then by the pose (z, y, x), then placed
    const Mat4 world = mat_multiply(
        mat_multiply(mat_multiply(mat_multiply(mat_rotation_y(-yaw), mat_rotation_x(-rotation[0])),
                                  mat_rotation_y(-rotation[1])),
                     mat_rotation_z(rotation[2])),
        mat_translation(position[0], position[1], -position[2]));
    std::vector<PassItem> items;
    queue_object(loaded->set.objects[static_cast<std::size_t>(object)],
                 loaded->gpu.object_mesh[static_cast<std::size_t>(object)], loaded->texture_base,
                 world, pass, 0, matrices.view, items);
    std::vector<const PassItem*> queue;
    for (const PassItem& item : items) {
        queue.push_back(&item);
    }
    sort_queue(queue);

    DrawContext context;
    context.renderer = &renderer;
    context.cache = &t.textures;
    context.slots = &t.slots;
    context.lights = &lights;
    context.view = matrices.view;
    context.projection = matrices.projection;
    context.view_projection = mat_multiply(matrices.view, matrices.projection);
    context.eye = matrices.eye;
    context.pass = pass;
    context.battle = false;
    const std::size_t first = ui.scene.items.size();
    draw_meshes(context, queue, ui.scene);
    for (std::size_t i = first; i < ui.scene.items.size(); ++i) {
        ui.scene.items[i].late = pass == 13;
    }
    ui.scene_drawn = true;
}

void trophy_models_release(Ui& ui, TrophyModels& t) {
    if (ui.renderer) {
        renderer_free_owner(*ui.renderer, render_owner_trophies);
    }
    t = TrophyModels{};
}
