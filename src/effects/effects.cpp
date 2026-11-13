#include "effects/effects.hpp"

#include "cook/pack.hpp"
#include "cook/pack_library.hpp"
#include "render/mesh_flags.hpp"
#include "render/mesh_queue.hpp"
#include "render/scenery.hpp"
#include "view/footsteps.hpp"
#include "world/world.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

// the model sets the effects draw (decomp docs/port/effects.md): c00_effect's flashes and shots
// (0xC2), the billboard quad 0x3F8 (0xC3), the decals and bubble (0xC5), the trail (0xCE) and
// grid_mdl (0xD6), the lamps' glows (0xB9, actor_decals_update) and the cockpit instruments
// 0x4DD..0x4F1 (0xD8, hud_draw_meters)
constexpr int effect_model_sets[] = {0xC2, 0xC3, 0xC5, 0xCE, 0xD6, 0xB9, 0xD8};
// the texture sets they draw with besides: sprite_fx_enter's 0x61E, 0x625, 0x61F, the flare
// 0x624, the crate glow and flicker 0x626, the trail 0x62F, the lamp glows 0x614, the cockpit's
// 0x636
constexpr int effect_texture_sets[] = {0x61E, 0x625, 0x61F, 0x624, 0x626, 0x62F, 0x614, 0x636};
constexpr int grid_slot = 0x4DB;
constexpr int quad2_slot = 0x3F8;
constexpr int trail_slot = 1432; // trail_model_slots[0]: all 50 are this model
constexpr int atlas_slot = 0x190;
constexpr int flare_slot = 0x125;
constexpr int flicker_slot = 0x140;
constexpr int sphere_slot = 0x396; // the blast's sphere
constexpr int beam_slot = 0x3A5;   // the laser's beam

bool load_set(EffectAssets& assets, int asset, const AssetManifest& manifest, PackLibrary& packs,
              Renderer& renderer, TextureCache& textures, std::string& error) {
    const auto entry = manifest.assets.find(asset);
    if (entry == manifest.assets.end() || entry->second.pack.empty()) {
        error = "the manifest has no pack for model set " + std::to_string(asset);
        return false;
    }
    const Pack* pack = pack_open(packs, entry->second.pack, error);
    ViewModelSet loaded;
    if (!pack || !load_model_set(*pack, asset, loaded.set, error)) {
        pack_close(packs, entry->second.pack);
        return false;
    }
    loaded.set.first_slot = entry->second.first_slot;
    const auto texture_set = manifest.assets.find(entry->second.texture_set);
    loaded.texture_base = texture_set != manifest.assets.end() ? texture_set->second.first_slot : 0;
    textures.pack = pack;
    const bool ok = upload_model_set(renderer, textures, loaded.set, loaded.gpu);
    textures.pack = nullptr;
    pack_close(packs, entry->second.pack);
    const int index = static_cast<int>(assets.sets.size());
    for (int o = 0; o < static_cast<int>(loaded.set.objects.size()); ++o) {
        assets.slots.push_back({loaded.set.first_slot + o, {index, o}});
    }
    assets.sets.push_back(std::move(loaded));
    return ok;
}

// every picture of a texture set into the cache, so the mesh path finds its slots
void load_texture_set(int asset, const AssetManifest& manifest, PackLibrary& packs,
                      Renderer& renderer, TextureCache& textures) {
    const auto entry = manifest.assets.find(asset);
    if (entry == manifest.assets.end() || entry->second.pack.empty()) {
        std::fprintf(stderr, "effects: no pack for texture set %d\n", asset);
        return;
    }
    std::string error;
    const Pack* pack = pack_open(packs, entry->second.pack, error);
    if (!pack) {
        std::fprintf(stderr, "effects: %s\n", error.c_str());
        return;
    }
    textures.pack = pack;
    for (int k = 0; k < entry->second.slot_count; ++k) {
        char name[16];
        std::snprintf(name, sizeof name, "/%02d.png", k);
        texture_for(renderer, textures, entry->second.file + name);
    }
    textures.pack = nullptr;
    pack_close(packs, entry->second.pack);
}

ModelObject* object_of(EffectAssets& assets, int slot) {
    for (const auto& [s, where] : assets.slots) {
        if (s == slot) {
            return &assets.sets[static_cast<std::size_t>(where.first)]
                        .set.objects[static_cast<std::size_t>(where.second)];
        }
    }
    return nullptr;
}

// the queued node counts textured_quad_draw_full leaves: none opaque; one middle unless
// additive (flag 0x8000), which goes to the last group
// decomp: textured_quad_draw_full 0xAFF6B
void quad_counts(ModelObject& object, bool additive) {
    for (ModelNode& node : object.nodes) {
        node.opaque_count = 0;
        node.middle_count = additive ? 0 : 1;
    }
}

} // namespace

Vec3f original_of(const FxVec3& v) {
    return {fx_float(v.x), fx_float(v.y), -fx_float(v.z)};
}

Mat4 port_matrix(const Mat4& m) {
    Mat4 out = m;
    out.m[0][2] = -out.m[0][2];
    out.m[1][2] = -out.m[1][2];
    out.m[3][2] = -out.m[3][2];
    out.m[2][0] = -out.m[2][0];
    out.m[2][1] = -out.m[2][1];
    return out;
}

// decomp: matrix_billboard_at 0x661A3
Mat4 billboard_matrix(const Vec3f& at, const Vec3f& eye, float off_x, float off_y, float off_z) {
    const Vec3f b = vec_sub(at, eye);
    const Vec3f x = vec_normalize(vec_cross(Vec3f{0, 1, 0}, b));
    const Vec3f y = vec_normalize(vec_cross(b, vec_cross(Vec3f{0, 1, 0}, b)));
    const Vec3f z = vec_normalize(b);
    Mat4 m;
    m.m[0][0] = x.x, m.m[0][1] = x.y, m.m[0][2] = x.z;
    m.m[1][0] = y.x, m.m[1][1] = y.y, m.m[1][2] = y.z;
    m.m[2][0] = z.x, m.m[2][1] = z.y, m.m[2][2] = z.z;
    m.m[3][0] = x.x * off_x + at.x + y.x * off_y + z.x * off_z;
    m.m[3][1] = x.y * off_x + at.y + y.y * off_y + z.y * off_z;
    m.m[3][2] = x.z * off_x + at.z + y.z * off_y + z.z * off_z;
    return m;
}

void effects_queue_slot(const Effects& fx, int slot, const Mat4& world, int pass,
                        std::uint32_t flags, std::uint32_t factor, const Mat4& view,
                        std::vector<PassItem>& items) {
    for (const auto& [s, where] : fx.assets.slots) {
        if (s != slot) {
            continue;
        }
        const ViewModelSet& set = fx.assets.sets[static_cast<std::size_t>(where.first)];
        const std::size_t first = items.size();
        queue_object(set.set.objects[static_cast<std::size_t>(where.second)],
                     set.gpu.object_mesh[static_cast<std::size_t>(where.second)], set.texture_base,
                     port_matrix(world), pass, flags, view, items);
        for (std::size_t i = first; i < items.size(); ++i) {
            items[i].item.factor = factor;
        }
        return;
    }
}

// decomp: textured_quad_draw_full 0xAFF6B, textured_quad2_draw_full 0xB00A4
void effects_queue_quad(const Effects& fx, int base, const Mat4& world, std::uint32_t flags,
                        std::uint32_t factor, int pass, const Mat4& view,
                        std::vector<PassItem>& items) {
    if (fx.assets.grid_mesh < 0) {
        return;
    }
    // none opaque when the factor has alpha or the layer counts it (always: layer 0x40); one
    // middle unless additive
    const ModelObject& quad = fx.assets.grid[(flags & mesh_additive) ? 1 : 0];
    const std::size_t first = items.size();
    queue_object(quad, fx.assets.grid_mesh, base, port_matrix(world), pass, flags | mesh_factor,
                 view, items);
    for (std::size_t i = first; i < items.size(); ++i) {
        items[i].item.factor = factor;
    }
}

void effects_queue_quad2(const Effects& fx, int base, const Mat4& world, std::uint32_t flags,
                         std::uint32_t factor, int pass, const Mat4& view, const Vec3f& eye,
                         std::vector<PassItem>& items) {
    if (fx.assets.quad2_mesh < 0) {
        return;
    }
    // pass 9's mesh 0x200: faces the eye with y kept up, scaled by the matrix's diagonal
    const Vec3f at{world.m[3][0], world.m[3][1], world.m[3][2]};
    const Mat4 board = mat_multiply(mat_scaling(world.m[0][0], world.m[1][1], world.m[2][2]),
                                    billboard_matrix(at, eye, 0, 0, 0));
    const std::size_t first = items.size();
    queue_object(fx.assets.quad2, fx.assets.quad2_mesh, base, port_matrix(board), pass,
                 flags | mesh_factor | mesh_billboard, view, items);
    for (std::size_t i = first; i < items.size(); ++i) {
        items[i].item.factor = factor;
    }
}

EffectHandle effects_play(Effects& fx, int set, const Vec3f& pos, const Vec3f& prev, float scale) {
    const EffectHandle handle = effect_set_play(fx.pool, set, pos, prev);
    if (set >= 0 && set < static_cast<int>(fx.data.sets.size())) {
        for (int kind : fx.data.sets[static_cast<std::size_t>(set)].sprites) {
            sprite_fx_spawn(fx, kind, scale, pos);
        }
    }
    return handle;
}

void effects_light(Effects& fx, const Vec3f& at, std::uint32_t colour, float size) {
    // a ring of six; the cursor is never reset, so past six in an update the oldest go
    EffectLight& light = fx.lights[fx.light_cursor];
    light.at = at;
    light.colour = colour;
    light.size = size;
    light.set = true;
    fx.light_cursor = (fx.light_cursor + 1) % 6;
}

bool effects_load(Effects& fx, PackLibrary& packs, Scenery& scenery, Renderer& renderer,
                  std::string& error) {
    if (!load_effect_data(packs.core, fx.data, error)) {
        return false;
    }
    EffectAssets& assets = fx.assets;
    assets = EffectAssets{};
    for (int asset : effect_model_sets) {
        if (!load_set(assets, asset, scenery.manifest, packs, renderer, scenery.textures, error)) {
            return false;
        }
    }
    for (int asset : effect_texture_sets) {
        load_texture_set(asset, scenery.manifest, packs, renderer, scenery.textures);
    }
    assets.atlas = slot_texture(renderer, scenery.textures, scenery.slots, atlas_slot);
    assets.flare = slot_texture(renderer, scenery.textures, scenery.slots, flare_slot);
    assets.flicker = slot_texture(renderer, scenery.textures, scenery.slots, flicker_slot);

    // grid_mdl as frame_cadence_init sets it up: uvs, layer 0x40, section flags 0xA
    if (const ModelObject* grid = object_of(assets, grid_slot)) {
        for (int k = 0; k < 2; ++k) {
            ModelObject& quad = assets.grid[k];
            quad = *grid;
            const float uvs[4][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
            for (std::size_t v = 0; v < quad.vertices.size() && v < 4; ++v) {
                quad.vertices[v].uv[0] = uvs[v][0];
                quad.vertices[v].uv[1] = uvs[v][1];
            }
            for (ModelSection& section : quad.sections) {
                section.layer[0] = layer_texture_alpha;
                section.tex[0] = 0;
                section.flags = 0x8 | sub_indexed;
            }
            quad_counts(quad, k == 1);
        }
        assets.grid_mesh =
            renderer_upload_mesh(renderer, assets.grid[0].vertices, assets.grid[0].indices);
    }
    // quad 0x3F8 as textured_quad2_draw_full sets it: section flags 2, tex 0, middle
    if (const ModelObject* quad2 = object_of(assets, quad2_slot)) {
        assets.quad2 = *quad2;
        for (ModelSection& section : assets.quad2.sections) {
            section.flags = 2;
            section.tex[0] = 0;
        }
        for (ModelNode& node : assets.quad2.nodes) {
            if (node.opaque_count) {
                node.opaque_count = 0;
                node.middle_count = 1;
            }
        }
        assets.quad2_mesh =
            renderer_upload_mesh(renderer, assets.quad2.vertices, assets.quad2.indices);
    }
    // the trail model, one mesh each (trail_draw rewrites its positions and colours)
    if (const ModelObject* trail = object_of(assets, trail_slot)) {
        for (auto& meshes : assets.trail_mesh) {
            for (int& mesh : meshes) {
                mesh = renderer_upload_mesh(renderer, trail->vertices, trail->indices);
            }
        }
        // trail_draw: section flags 0x802, tex 0 (-1 untextured), no box, one middle group
        for (int mode = 0; mode < 3; ++mode) {
            ModelObject& object = assets.trail_objects[mode];
            object = *trail;
            object.vertices.clear();
            for (ModelSection& section : object.sections) {
                section.flags = sub_two_sided | sub_indexed;
                section.tex[0] = mode == 2 ? -1 : 0;
                section.center[0] = section.center[1] = section.center[2] = 0;
                section.half[0] = section.half[1] = section.half[2] = 0;
            }
            for (ModelNode& node : object.nodes) {
                node.center[0] = node.center[1] = node.center[2] = 0;
                node.half[0] = node.half[1] = node.half[2] = 0;
                if (mode != 2 && node.opaque_count) {
                    node.opaque_count = 0;
                    node.middle_count = 1;
                }
            }
        }
        assets.trail_vertices = trail->vertices;
    }
    // what the shot looks leave set on their models for good: impact_volume_draw draws the
    // sphere two-sided (section 0 |= 0x800), weapon_laser_controller the beam unlit and additive
    // (section 0 |= 0x300)
    if (ModelObject* sphere = object_of(assets, sphere_slot); sphere && !sphere->sections.empty()) {
        sphere->sections[0].flags |= sub_two_sided;
    }
    if (ModelObject* beam = object_of(assets, beam_slot); beam && !beam->sections.empty()) {
        beam->sections[0].flags |= sub_additive | sub_unlit;
    }
    assets.loaded = true;
    return true;
}

void effects_reset(Effects& fx, int selector, std::uint32_t seed) {
    fx.seed = seed;
    particle_pool_clear(fx.pool, fx.data, seed * 0x2545F491u + 1);
    for (SpriteFx& s : fx.sprites) {
        s.active = false;
    }
    fx.pulse_phase = 0;
    fx.mark_next = fx.mark_count = 0;
    fx.mark_angle = 0;
    for (Trail& t : fx.trails) {
        t = Trail{};
    }
    for (ContactDecal& d : fx.decals) {
        d = ContactDecal{};
    }
    for (ShieldBubble& b : fx.bubbles) {
        b = ShieldBubble{};
    }
    fx.bubble_spin = fx.rainbow_scroll = 0;
    fx.rainbow_phase = 1;
    for (auto& ring : fx.scratches) {
        for (Scratch& s : ring) {
            s = Scratch{};
        }
    }
    fx.flare = LensFlare{};
    for (EffectLight& light : fx.lights) {
        light = EffectLight{};
    }
    for (EffectHandle& h : fx.craft) {
        h = {};
    }
    for (EffectHandle& h : fx.paper) {
        h = {};
    }
    fx.paper_anchors = EndingPaper{};
    fx.craft_state = 0;
    for (auto& g : fx.get_items) {
        g = Effects::GetItem{};
    }
    for (FighterEffects& f : fx.fighters) {
        f = FighterEffects{};
    }
    fx.flashes.clear();
    fx.screen.clear();
    fx.pending.clear();
    fx.pending_combat.clear();
    for (Effects::ShotTrail& st : fx.shot_trails) {
        st = Effects::ShotTrail{};
    }
    for (Debris& d : fx.debris) {
        d = Debris{};
    }
    fx.debris_cursor = 0;
    for (auto& mounts : fx.casing_due) {
        for (bool& due : mounts) {
            due = false;
        }
    }
    for (ShotSnap& s : fx.shot_now) {
        s = ShotSnap{};
    }
    for (ShotSnap& s : fx.shot_before) {
        s = ShotSnap{};
    }
    fx.updates = 0;
    fx.phase = 0;
    fx.day_mode = selector;
    fx.battle = true;
}

void effects_step(Effects& fx, const World& before, const World& world) {
    screen_effects_seat(fx, world);
    shot_looks_step(fx, world);
    shield_bubble_step(fx, world);
    fx.pending.insert(fx.pending.end(), world.stage.events.begin(),
                      world.stage.events.begin() + world.stage.event_count);
    if (world.battle) {
        fx.pending_combat.insert(fx.pending_combat.end(), world.combat.events.begin(),
                                 world.combat.events.begin() + world.combat.event_count);
        for (int slot = 0; slot < fighter_slots; ++slot) {
            const std::size_t i = static_cast<std::size_t>(slot);
            fx.fighters[slot].footsteps |=
                fighter_footsteps(world, before.fighters[i], world.fighters[i]);
        }
    }
    fx.phase += 1;
    // the 30 Hz update after every even step (the step the stage's static draws on)
    if ((world.step - 1) % 2 != 0) {
        return;
    }
    fx.phase = 0;
    // point_lights_reset at the top of battle_update: a light lives one update
    for (EffectLight& light : fx.lights) {
        light.set = false;
    }
    fx.flashes.clear();
    // the step's events in order, then the fighters' effects, the stage's, the sprite effects,
    // and the pool last (battle_task_begin makes the effects task the last 0x3000 child)
    for (const CombatEvent& event : fx.pending_combat) {
        if (std::getenv("PHANTOMCRASH_FX_TRACE") &&
            (event.kind == event_impact || event.kind == event_muzzle)) {
            std::printf("fx step %lld combat %s slot %d value %d effect %d\n",
                        static_cast<long long>(world.step),
                        event.kind == event_impact ? "impact" : "muzzle", event.slot, event.value,
                        event.effect);
        }
        combat_effects_event(fx, world, event);
    }
    for (const StageEvent& event : fx.pending) {
        stage_effects_event(fx, world, event);
        stage_light_event(fx, event);
    }
    fx.pending.clear();
    fx.pending_combat.clear();
    // PHANTOMCRASH_FX=set[,period[,distance[,scale[,seat]]]]: a look at one set, played ahead of
    // fighter 1 (or the free camera) every period updates; set -1 a hit static now and then, in
    // split screen shown in that seat's view
    if (const char* test = std::getenv("PHANTOMCRASH_FX")) {
        int set = -1, period = 45;
        float distance = 8, scale = 1;
        std::sscanf(test, "%d,%d,%f,%f", &set, &period, &distance, &scale);
        if (period > 0 && fx.updates % period == 1) {
            const Fighter& f = world.fighters[0];
            const FxVec3 from = f.present ? f.position : world.camera.position;
            const Angle yaw = f.present ? f.heading : world.camera.yaw;
            const Vec3f at =
                vec_add(original_of(from), {std::sin(fx_float(angle_radians(yaw))) * distance, 1.0f,
                                            -std::cos(fx_float(angle_radians(yaw))) * distance});
            if (set >= 0) {
                const EffectHandle h = effects_play(fx, set, at, at, scale);
                effect_set_matrix(fx.pool, h, mat_identity());
                effects_light(fx, at, 0xFFFF8000, 12);
            } else {
                for (int i = 0; i < 15; ++i) {
                    screen_scratch_add(fx, rand_float_crt(fx.seed, 300), 1 + crt_rand(fx.seed) % 30,
                                       rand_float_crt(fx.seed, 3), 0);
                    screen_scratch_add(fx, rand_float_crt(fx.seed, 300), 1 + crt_rand(fx.seed) % 3,
                                       0, 1);
                }
            }
        }
    }
    fighter_effects_update(fx, world);
    lamp_trails_update(fx, world);
    weapon_break_update(fx, world);
    combat_effects_update(fx, world);
    contact_decals_update(fx, world);
    shield_bubble_update(fx, world);
    casings_update(fx);
    sprite_fx_update(fx);
    stage_effects_update(fx, world);
    trails_update(fx);
    screen_effects_update(fx, world);
    particle_pool_update(fx.pool);
    if (std::getenv("PHANTOMCRASH_FX_TRACE")) {
        int emitters = 0;
        for (const Emitter& e : fx.pool.emitters) {
            emitters += e.active ? 1 : 0;
        }
        std::printf("fx update %lld: %d emitters, %zu billboards\n",
                    static_cast<long long>(fx.updates), emitters, fx.pool.pushes.size());
    }
    fx.updates += 1;
}

void effects_queue(Effects& fx, Renderer& renderer, float alpha, const Mat4& view,
                   const Mat4& projection, const float eye[3], int view_index,
                   std::vector<PassItem>& items, std::vector<Billboard>& billboards,
                   PointLight lights[6]) {
    if (!fx.assets.loaded) {
        return;
    }
    // how far between the last two updates: steps since the last, over two
    float t = (static_cast<float>(fx.phase) + alpha) * 0.5f;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const Vec3f eye_original{eye[0], eye[1], -eye[2]};
    fx.view_projection = mat_multiply(view, projection);
    // lens_flare_occlusion_test reads the depth where the flare's sun falls (a frame late)
    fx.sun_depth = renderer_depth_probe(renderer, fx.flare.probe_x, fx.flare.probe_y);
    for (int i = 0; i < 6; ++i) {
        if (fx.lights[i].set) {
            lights[i].position[0] = fx.lights[i].at.x;
            lights[i].position[1] = fx.lights[i].at.y;
            lights[i].position[2] = -fx.lights[i].at.z;
            lights[i].colour = flash_colour(fx, fx.lights[i].colour);
            lights[i].range = fx.lights[i].size;
        }
    }
    billboards_prepare(fx.pool.pushes, t, view, billboards);
    sprite_fx_queue(fx, t, view, eye_original, items);
    trails_queue(fx, renderer, t, view, eye_original, view_index, items);
    combat_effects_queue(fx, view, eye_original, items);
    shot_looks_queue(fx, alpha, view, eye_original, items);
    casings_queue(fx, t, view, items);
    lamp_glows_queue(fx, t, view, items);
    contact_decals_queue(fx, view, items);
    shield_bubble_queue(fx, alpha, view, items);
}
