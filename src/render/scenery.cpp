#include "render/scenery.hpp"

#include "effects/effects.hpp"
#include "mech/dance_pose.hpp"
#include "perf_log.hpp"
#include "render/camo_draw.hpp"
#include "render/mesh_draw.hpp"
#include "render/vk_sprites.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// the idle motion slot (motion_set_animation(actor, 0x1C, 0xB, ...)) and its length, ticks
constexpr int idle_slot = 28;
constexpr float idle_ticks = 44;

// the texture sets a battle has in the table besides the stage's: texture_set_load 0x615 and
// 0x635 in battle_task_begin, and the effects set the blob shadows and effect models use
const int battle_texture_sets[] = {1557, 1589, 1555};

// the original's single-view rectangle in its 640x480 screen (viewport_rect)
constexpr float safe_rect[4] = {32, 16, 576, 448};

// the fighters' shadow volume batch (actor_part_batches_build, part slot 9): object 1 of
// the body and leg model sets, skinned by the chassis matrices in the order the graph's remap
// gives (sv_matrix_store_pass stores them at +0xD70 of the fighter's record)
// decomp: actor_part_batches_build 0x78185
void fighter_shadow_items(const Scenery& scenery, const MechParts& mech, const MechPose& pose,
                          std::vector<PassItem>& out) {
    const FighterTables& tables = scenery.fighters;
    Mat4 palette[32];
    bool stored[32] = {};
    std::vector<int> pending{mech.leg_graph};
    while (!pending.empty()) {
        const int index = pending.back();
        pending.pop_back();
        if (index < 0 || index >= static_cast<int>(tables.nodes.size())) {
            continue;
        }
        const SvGraphNode& node = tables.nodes[index];
        if (node.matrix >= 0 && node.matrix < 32 && node.remap >= 0 && node.remap < 32 &&
            pose.stored[0][node.matrix]) {
            palette[node.remap] = pose.matrices[0][node.matrix];
            stored[node.remap] = true;
        }
        pending.insert(pending.end(), node.children.begin(), node.children.end());
    }
    int bone = 0;
    for (MechPart part : {part_body, part_legs}) {
        if (!mech.loaded[part] || mech.models[part].objects.size() < 2) {
            continue;
        }
        const ModelObject& object = mech.models[part].objects[1];
        for (const ModelNode& node : object.nodes) {
            if (node.section_count <= 0) {
                continue;
            }
            if (bone < 32 && stored[bone]) {
                const Mat4 world = mat_multiply(node_local_matrix(node), palette[bone]);
                for (int s = 0; s < node.section_count; ++s) {
                    const int section = node.first_section + s;
                    if (section >= 0 && section < static_cast<int>(object.sections.size())) {
                        PassItem item;
                        item.pass = 3;
                        item.item = section_draw(object.sections[section],
                                                 mech.gpu[part].object_mesh[1], -1, world, 3, 0);
                        out.push_back(item);
                    }
                }
            }
            ++bone;
        }
    }
}

} // namespace

bool load_scenery(Scenery& scenery, Renderer& renderer, PackLibrary& packs, int stage, int build,
                  std::string& error, const std::vector<FighterBuild>& extra) {
    if (!load_manifest(packs.core, scenery.manifest, error) ||
        !load_stage_tables(packs.core, scenery.stages, error) ||
        !load_stage_lighting(packs.core, scenery.lighting, error) ||
        !load_fighter_tables(packs.core, scenery.fighters, error)) {
        return false;
    }
    scenery.fighters.builds.insert(scenery.fighters.builds.end(), extra.begin(), extra.end());
    if (!load_stage_scene(scenery.stage, scenery.stages, stage, scenery.manifest, packs, renderer,
                          scenery.textures, error) ||
        !load_mech(scenery.mech, scenery.fighters, build, scenery.manifest, packs, renderer,
                   scenery.textures, error) ||
        !load_stage_data(scenery.stage_data, packs.core, error) ||
        !load_stage_view(scenery.stage_view, scenery.stage_data, stage, 0, scenery.manifest, packs,
                         renderer, scenery.textures, error)) {
        return false;
    }
    std::vector<int> preferred{scenery.stage.texture_set};
    preferred.insert(preferred.end(), std::begin(battle_texture_sets),
                     std::end(battle_texture_sets));
    build_texture_slots(scenery.slots, scenery.manifest, preferred);
    // the texture table's pictures from the stage's pack and the battle's, and the pictures the
    // renderer loads by name (systemdata, in the battle pack)
    for (const std::string& name : {stage_pack_name(stage), std::string("battle")}) {
        const Pack* pack = pack_open(packs, name, error);
        if (!pack) {
            return false;
        }
        load_slot_textures(renderer, scenery.textures, scenery.slots, *pack);
        if (name == "battle") {
            scenery.textures.pack = pack;
            scenery.bump_light =
                texture_for(renderer, scenery.textures, "textures/systemdata/bump_light.png");
            scenery.yurayura =
                texture_for(renderer, scenery.textures, "textures/systemdata/yurayuraTest.png");
            scenery.wave02 =
                texture_for(renderer, scenery.textures, "textures/systemdata/wave02.png");
            scenery.muteki =
                texture_for(renderer, scenery.textures, "textures/systemdata/muteki01.png");
            scenery.textures.pack = nullptr;
        }
        pack_close(packs, name);
    }

    const StageInfo& info = scenery.stages.stages[stage];
    if (!info.entry_points.empty()) {
        std::copy(info.entry_points[0].position, info.entry_points[0].position + 3,
                  scenery.mech_position);
        scenery.mech_heading = info.entry_points[0].yaw;
    }
    std::printf("stage %d %s: %zu objects; fighter at %.2f %.2f %.2f\n", stage, info.name.c_str(),
                info.objects.size(), scenery.mech_position[0], scenery.mech_position[1],
                scenery.mech_position[2]);
    return true;
}

const MechParts& scenery_mech(const Scenery& scenery, int slot) {
    return slot <= 0 || slot > 5 ? scenery.mech
                                 : scenery.other_mechs[static_cast<std::size_t>(slot - 1)];
}

bool load_scenery_fighter(Scenery& scenery, Renderer& renderer, PackLibrary& packs, int slot,
                          int build, std::string& error) {
    if (slot < 1 || slot > 5) {
        return false;
    }
    return load_mech(scenery.other_mechs[static_cast<std::size_t>(slot - 1)], scenery.fighters,
                     build, scenery.manifest, packs, renderer, scenery.textures, error);
}

bool draw_scenery(Scenery& scenery, Renderer& renderer, const CameraPose& camera,
                  const FighterPose* fighter, float ticks, float alpha,
                  const std::string& capture_path) {
    FighterPose fighters[6];
    if (fighter) {
        fighters[0] = *fighter;
    }
    return draw_scenery_fighters(scenery, renderer, camera, fighter ? fighters : nullptr, ticks,
                                 alpha, capture_path);
}

namespace {

// one view's draws appended to the frame's: the queues for a camera through a rectangle of the
// 640x480 screen, its passes in order (render_passes_run for a view)
void view_draws(Scenery& scenery, Renderer& renderer, const CameraPose& camera, const float* rect,
                int view_index, const FighterPose* fighters, float ticks, float alpha,
                SceneLights lights, bool cockpit, FrameDraws& draws) {
    const int updates = static_cast<int>(ticks) / 2;
    // the view's camera, its projection for the rectangle's aspect in the target's pixels
    const float aspect = renderer_scene_aspect(renderer, rect);
    const CameraMatrices matrices =
        camera_matrices(camera.position, camera.yaw, camera.pitch, aspect, camera.fov, camera.roll);

    // the queues: the stage's objects by layer, the fighter's parts (pass 4) and its shadow
    // volume batch (pass 3, day stages only)
    std::vector<PassItem>& items = scenery.items;
    items.clear();
    const Dance* dance = scenery.dance;
    if (!dance) {
        stage_draw_items(scenery.stage, camera.position, matrices.view, items);
        scenery.stage_view.day = scenery.day;
        stage_view_draw(scenery.stage_view, renderer, matrices.view, camera.position, alpha, items);
    }
    // the effects: their meshes, the billboards of pass 10, the point lights (src/effects)
    std::vector<Billboard>& billboards = scenery.billboards;
    billboards.clear();
    if (scenery.effects && !dance) {
        scenery.effects->flash_scale = static_cast<float>(renderer.options.flashes) / 100.0f;
        effects_queue(*scenery.effects, renderer, alpha, matrices.view, matrices.projection,
                      camera.position, view_index, items, billboards, lights.points);
        // the cockpit instruments in pass 11 (one view only)
        if (cockpit && scenery.cockpit) {
            effects_queue_cockpit(*scenery.effects, *scenery.cockpit, scenery.stage_view.quad,
                                  scenery.stage_view.quad_mesh, aspect, items);
        }
    }

    // the fighters: the world's pose as the view interpolated it (mech/fighter_pose.hpp), or
    // with no fighter in the world (the free camera) slot 0's float walk of the idle clip on the
    // entry point
    for (int slot = 0; slot < 6; ++slot) {
        const FighterPose* fighter = fighters ? &fighters[slot] : nullptr;
        const bool moving = fighter && fighter->present;
        const bool dancing = dance && slot == 0 && !moving;
        if (!moving && !dancing && (slot > 0 || fighters)) {
            continue;
        }
        const MechParts& mech = scenery_mech(scenery, slot);
        if (!mech.tables) {
            continue;
        }
        const float heading = moving ? fighter->heading : scenery.mech_heading;
        MechPose pose;
        if (moving) {
            pose = fighter->pose;
        } else {
            const float* at = scenery.mech_position;
            const Mat4 world =
                mat_multiply(mat_rotation_y(heading), mat_translation(at[0], at[1], at[2]));
            build_mech_pose(
                dancing ? dance_pose_input(*dance, mech, ticks, world)
                        : mech_pose_input(mech, idle_slot, std::fmod(ticks, idle_ticks), world),
                pose);
        }
        const std::size_t fighter_first = items.size();
        const bool body = !moving || !fighter->body_hidden;
        mech_draw_items(mech, pose, camera.position, items, body);
        for (std::size_t i = fighter_first; i < items.size(); ++i) {
            items[i].item.heading = heading;
        }
        // camouflaged (states 1..8): the parts in pass 5 with the reveal sphere (camo_draw.cpp)
        const bool cloaked = moving && camo_pose_cloaked(*fighter);
        if (cloaked) {
            for (std::size_t i = fighter_first; i < items.size(); ++i) {
                if (items[i].pass == 4) {
                    items[i].pass = 5;
                    std::copy(fighter->camo_sphere, fighter->camo_sphere + 4,
                              items[i].item.reveal);
                }
            }
        }
        // the sticker (actor_draw): pass 2 as a cutout, or cloaked in pass 8 fading with the
        // cloak (timer / 60 in states 1 and 2, (60 - timer) / 60 in 5 and 6, gone in 3, 4, 7, 8)
        if (!body) {
            // the cockpit view: no sticker, no shadow
        } else if (cloaked) {
            float fade = 0;
            const int state = fighter->camo_state;
            if (state == 1 || state == 2) {
                fade = fighter->camo_timer / 60.0f;
            } else if (state == 5 || state == 6) {
                fade = (60.0f - fighter->camo_timer) / 60.0f;
            }
            const auto a = static_cast<std::uint32_t>(std::clamp(fade, 0.0f, 1.0f) * 255.0f);
            mech_sticker_items(mech, pose, 8, 0, a << 24 | 0xFFFFFF, items);
        } else {
            mech_sticker_items(mech, pose, 2, 0x400, 0xFFFFFFFF, items);
        }
        if (body && !dance && scenery.stage.selector % 2 == 0) {
            fighter_shadow_items(scenery, mech, pose, items);
        }
        // PHANTOMCRASH_POSE=1: slot 0's pose matrices, once
        static bool told = SDL_getenv("PHANTOMCRASH_POSE") == nullptr;
        if (slot == 0 && !told) {
            told = true;
            std::printf("pose: %zu fighter items, %d IK tips with rotation\n",
                        items.size() - fighter_first, pose.ik_tip_rotations);
            for (int part = 0; part < 3; ++part) {
                for (int m = 0; m < 32; ++m) {
                    if (pose.stored[part][m]) {
                        const Vec3f p = mat_position(pose.matrices[part][m]);
                        std::printf("  part %d matrix %d at %.3f %.3f %.3f\n", part, m, p.x, p.y,
                                    p.z);
                    }
                }
            }
            for (int i = 0; i < 5; ++i) {
                const Vec3f p = mat_position(pose.mount[i]);
                std::printf("  mount %d %s at %.3f %.3f %.3f\n", i,
                            pose.mounted[i] ? "set" : "unset", p.x, p.y, p.z);
            }
        }
    }

    // the passes in order, each queue sorted as its draw callback sorts it
    std::array<std::vector<const PassItem*>, 17>& queues = scenery.queues;
    for (std::vector<const PassItem*>& queue : queues) {
        queue.clear();
    }
    for (const PassItem& item : items) {
        if (item.pass >= 0 && item.pass < 17) {
            queues[item.pass].push_back(&item);
        }
    }
    DrawContext context;
    context.renderer = &renderer;
    context.cache = &scenery.textures;
    context.slots = &scenery.slots;
    context.lights = &lights;
    context.view = matrices.view;
    context.projection = matrices.projection;
    context.view_projection = mat_multiply(matrices.view, matrices.projection);
    context.eye = matrices.eye;
    context.bump_light = scenery.bump_light;
    context.yurayura = scenery.yurayura;
    context.wave02 = scenery.wave02;
    context.muteki = scenery.muteki;
    std::copy(rect, rect + 4, context.viewport);
    context.water_bump_angle = static_cast<float>(updates) / 30.0f;
    bool backdrop_copied = false; // pass 5 drew, its first draw copying the backdrop
    for (int pass = 0; pass < 17; ++pass) {
        context.pass = pass;
        std::vector<const PassItem*>& queue = queues[pass];
        if (pass != 3 && pass != 4 && pass != 5 && pass != 8 && pass != 11) {
            sort_queue(queue);
        }
        if (pass == 3 || pass == 7) {
            draw_shadow_volumes(context, queue, draws);
        } else if (pass == 4) {
            draw_fighters(context, queue, draws);
        } else if (pass == 5) {
            const std::size_t first = draws.items.size();
            draw_camo_fighters(context, queue, fighters ? camo_frame_k(fighters, 6) : 0.25f, rect,
                               draws);
            backdrop_copied = draws.items.size() > first;
        } else if (pass == 6) {
            // pass_open_camo copies the backdrop once a view, at pass 5 or the first of pass 6
            draw_distortion(context, queue, !backdrop_copied, draws);
        } else if (pass == 1 || pass == 2 || pass == 8 || pass == 9) {
            draw_meshes(context, queue, draws);
        } else if (pass == 11 && !queue.empty()) {
            // pass_view_set's identity view for the HUD: only the port's z flip
            DrawContext hud = context;
            hud.view = mat_scaling(1, 1, -1);
            // in the view's own field of view, not FIELD OF VIEW's: the instruments and the
            // readout's panels are laid out on retail's screen
            if (camera.widen != 0.0f) {
                hud.projection = camera_matrices(camera.position, camera.yaw, camera.pitch, aspect,
                                                 camera.fov - camera.widen)
                                     .projection;
            }
            hud.view_projection = mat_multiply(hud.view, hud.projection);
            draw_meshes(hud, queue, draws);
        } else if (pass == 10 && scenery.effects) {
            billboards_draw(billboards, scenery.effects->assets.atlas, matrices.projection, draws);
        }
    }
}

} // namespace

bool draw_scenery_fighters(Scenery& scenery, Renderer& renderer, const CameraPose& camera,
                           const FighterPose* fighters, float ticks, float alpha,
                           const std::string& capture_path) {
    SceneryView view;
    view.camera = camera;
    if (scenery.safe_rect) {
        std::copy(std::begin(safe_rect), std::end(safe_rect), view.rect);
    }
    return draw_scenery_views(scenery, renderer, &view, 1, fighters, ticks, alpha, capture_path);
}

bool draw_scenery_views(Scenery& scenery, Renderer& renderer, const SceneryView* views,
                        int count, const FighterPose* fighters, float ticks, float alpha,
                        const std::string& capture_path) {
    const std::uint64_t scene_start = perf_now(); // the draw list's time, up to the renderer's
    // the battle's lighting for the day (battle_lighting_update, battle_task_begin)
    FrameSetup setup;
    SceneLights lights;
    battle_lighting(scenery.lighting, scenery.stage.selector, scenery.day, lights, setup);
    setup.clear[0] = setup.clear[1] = setup.clear[2] = 0; // clear_color 0xFF000000
    if (scenery.dance && scenery.dance->white) {
        setup.clear[0] = setup.clear[1] = setup.clear[2] = 1; // --dance-white
    }
    setup.clear[3] = 1;
    // the heat haze's wave advances 0.2 a composite, once a 30 Hz update, back to 0 past 2 pi
    const int updates = static_cast<int>(ticks) / 2;
    setup.wave_phase = 0.2f * static_cast<float>(updates % 32);
    // the stage's animation once a frame, then each view's queues and passes
    stage_animation_frame(scenery.stage.animation, renderer, alpha);
    FrameDraws& draws = scenery.draws;
    draws.items.clear();
    draws.uniforms.clear();
    draws.storage.clear();
    count = std::clamp(count, 1, 4);
    if (count == 1) {
        std::copy(views[0].rect, views[0].rect + 4, setup.viewport);
    } else {
        setup.view_count = count;
    }
    for (int v = 0; v < count; ++v) {
        const std::size_t first = draws.items.size();
        view_draws(scenery, renderer, views[v].camera, views[v].rect, v, fighters, ticks, alpha,
                   lights, count == 1, draws);
        FrameSetup::View& out = setup.views[v];
        std::copy(views[v].rect, views[v].rect + 4, out.viewport);
        out.first = static_cast<std::uint32_t>(first);
        out.count = static_cast<std::uint32_t>(draws.items.size() - first);
    }
    // the effects' 2D over the 3D (the hit static, the lens flare), added to the frame's 2D (the
    // HUD's, which main sets first, or none): over the one view, or into each split-screen view
    // with the static only in the view of the seat whose hit set it
    if (scenery.effects) {
        Draw2D draw;
        std::vector<SlotTexture> slot_textures;
        if (count == 1) {
            // the drawn 3D in the sprite layer's pixels, for the flare (Effects::drawn_3d)
            const SpriteFrame f = renderer_sprite_frame(renderer, -1);
            const float* d = views[0].rect;
            const bool box = renderer.options.fit_43;
            const float sx = box ? f.scale : static_cast<float>(renderer.width) / 640.0f;
            const float sy = box ? f.scale : static_cast<float>(renderer.height) / 480.0f;
            const float wx = box ? f.ox : 0.0f, wy = box ? f.oy : 0.0f;
            float* out = scenery.effects->drawn_3d;
            out[0] = (wx + (d[0] + d[2] * 0.5f) * sx - f.ox) / f.scale;
            out[1] = (wy + (d[1] + d[3] * 0.5f) * sy - f.oy) / f.scale;
            out[2] = d[2] * 0.5f * sx / f.scale;
            out[3] = d[3] * 0.5f * sy / f.scale;
            effects_screen(*scenery.effects, draw, slot_textures, nullptr);
        } else {
            for (int v = 0; v < count; ++v) {
                const ScreenView view{v, views[v].sprite_rect, views[v].rect,
                                      views[v].seat == scenery.effects->hit_seat};
                effects_screen(*scenery.effects, draw, slot_textures, &view);
            }
        }
        // the screen adjust: with the 3D, as the HUD's 2D (ui_present), the full-window layers
        // (the static) and the fade staying on the glass
        for (Quad2D& quad : draw.quads) {
            if (overlay_pass(quad.layer) != 15 && !(quad.flags & quad_full_window)) {
                quad.x += static_cast<float>(renderer.screen_shift[0]);
                quad.y += static_cast<float>(renderer.screen_shift[1]);
            }
        }
        renderer_add_sprites(renderer, draw, slot_textures);
    }
    // the late draws handed in, their blocks moved after the frame's
    if (scenery.late) {
        for (DrawItem item : scenery.late->items) {
            if (item.uniform_size) {
                item.uniform_offset = push_uniform(
                    draws, scenery.late->uniforms.data() + item.uniform_offset, item.uniform_size);
            }
            draws.items.push_back(item);
        }
    }
    perf_add(PerfPart::scene, perf_now() - scene_start);
    return renderer_draw_frame(renderer, setup, draws, capture_path);
}
