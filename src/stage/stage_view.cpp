#include "stage/stage_view.hpp"

#include "cook/pack.hpp"
#include "render/mesh_flags.hpp"
#include "render/mesh_queue.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr int grid_model_asset = 214; // grid_mdl: the quad textured_quad_draw_full draws (0x4DB)
constexpr int grid_slot = 0x4DB;
constexpr int beacon_slot = 0x4AE;      // supply_craft_draw's night beacon
constexpr int crate_glow_texture = 0x13F;
// the beacon: -0.14 a draw (an update) -> -0.07 a step, +3.5 below -2.5 (cosmetic)
constexpr float beacon_step = 0.14f / 2;

// M W M, M the z flip: a matrix built in the original's frame, as the port's
Mat4 mirrored(Mat4 m) {
    m.m[0][2] = -m.m[0][2];
    m.m[1][2] = -m.m[1][2];
    m.m[3][2] = -m.m[3][2];
    m.m[2][0] = -m.m[2][0];
    m.m[2][1] = -m.m[2][1];
    return m;
}

// matrix_billboard_at 0x65973 with the camera at eye: z from the eye to the point, x = up x z,
// y = z x x, placed at the point moved by the offsets along them. Worked in the original's frame
Mat4 billboard_at(const float at[3], const float eye[3], float off_x, float off_y, float off_z) {
    const Vec3f p{at[0], at[1], -at[2]};
    const Vec3f b = vec_sub(p, Vec3f{eye[0], eye[1], -eye[2]});
    const Vec3f x = vec_normalize(vec_cross(Vec3f{0, 1, 0}, b));
    const Vec3f y = vec_normalize(vec_cross(b, vec_cross(Vec3f{0, 1, 0}, b)));
    const Vec3f z = vec_normalize(b);
    Mat4 m;
    m.m[0][0] = x.x, m.m[0][1] = x.y, m.m[0][2] = x.z;
    m.m[1][0] = y.x, m.m[1][1] = y.y, m.m[1][2] = y.z;
    m.m[2][0] = z.x, m.m[2][1] = z.y, m.m[2][2] = z.z;
    m.m[3][0] = x.x * off_x + p.x + y.x * off_y + z.x * off_z;
    m.m[3][1] = x.y * off_x + p.y + y.y * off_y + z.y * off_z;
    m.m[3][2] = x.z * off_x + p.z + y.z * off_y + z.z * off_z;
    return mirrored(m);
}

// textured_quad_draw_full 0xAFFD3: grid_mdl with flags | mesh_factor, the texture slot as its base
// (tex 0), the colour as its texture factor
void draw_quad(const StageView& view, const Mat4& world, int slot, std::uint32_t flags,
               std::uint32_t factor, const Mat4& view_matrix, std::vector<PassItem>& out) {
    const std::size_t first = out.size();
    queue_object(view.quad, view.quad_mesh, slot, world, 9, flags | mesh_factor, view_matrix, out);
    for (std::size_t i = first; i < out.size(); ++i) {
        out[i].item.factor = factor;
    }
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

bool load_set(StageView& view, int asset, const AssetManifest& manifest, PackLibrary& packs,
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
    loaded.texture_base =
        texture_set != manifest.assets.end() ? texture_set->second.first_slot : 0;
    textures.pack = pack;
    const bool ok = upload_model_set(renderer, textures, loaded.set, loaded.gpu);
    // the rest of its texture set (texture_set_load loads it whole): the pictures only a later
    // stage names, such as the gates' bump map on Shinjuku day (set 1575's 07.png, slot 328),
    // which the draw finds by slot after the pack is closed
    if (texture_set != manifest.assets.end()) {
        for (int k = 0; k < texture_set->second.slot_count; ++k) {
            char name[16];
            std::snprintf(name, sizeof name, "/%02d.png", k);
            const std::string path = texture_set->second.file + name;
            if (pack_find(*pack, PackKind::texture, path)) {
                texture_for(renderer, textures, path);
            }
        }
    }
    textures.pack = nullptr;
    pack_close(packs, entry->second.pack);
    const int index = static_cast<int>(view.sets.size());
    for (int o = 0; o < static_cast<int>(loaded.set.objects.size()); ++o) {
        view.slots[loaded.set.first_slot + o] = {index, o};
    }
    view.sets.push_back(std::move(loaded));
    return ok;
}

const ModelObject* object_of(const StageView& view, int slot) {
    const auto found = view.slots.find(slot);
    if (found == view.slots.end()) {
        return nullptr;
    }
    return &view.sets[found->second.first].set.objects[found->second.second];
}

// the crate's glow colour by kind (pickups_draw): blue score, grey repair, green refill
std::uint32_t crate_colour(int kind) {
    return kind <= 3 ? 0x7070D0 : kind <= 6 ? 0xA0A0A0 : 0x70D070;
}

} // namespace

Mat4 mat_of(const FxMat4& m) {
    Mat4 out;
    const FxVec3* rows[4] = {&m.x, &m.y, &m.z, &m.w};
    for (int r = 0; r < 4; ++r) {
        out.m[r][0] = fx_float(rows[r]->x);
        out.m[r][1] = fx_float(rows[r]->y);
        out.m[r][2] = fx_float(rows[r]->z);
        out.m[r][3] = r == 3 ? 1.0f : 0.0f;
    }
    return out;
}

Mat4 mat_lerp(const Mat4& a, const Mat4& b, float t) {
    Mat4 out;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out.m[r][c] = lerp(a.m[r][c], b.m[r][c], t);
        }
    }
    return out;
}

void draw_slot(const StageView& view, int slot, const Mat4& world, int pass, std::uint32_t flags,
               const Mat4& view_matrix, std::vector<PassItem>& out, int mesh, int paint,
               std::uint32_t factor) {
    const auto found = view.slots.find(slot);
    if (found == view.slots.end()) {
        return;
    }
    const ViewModelSet& loaded = view.sets[found->second.first];
    const int object = found->second.second;
    const std::size_t first = out.size();
    queue_object(loaded.set.objects[object], mesh >= 0 ? mesh : loaded.gpu.object_mesh[object],
                 loaded.texture_base, world, pass, flags, view_matrix, out);
    for (std::size_t i = first; i < out.size(); ++i) {
        out[i].item.paint = paint;
        out[i].item.factor = factor;
    }
}

int texture_by_slot(const AssetManifest& manifest, PackLibrary& packs, Renderer& renderer,
                    TextureCache& textures, int slot) {
    for (const auto& [id, entry] : manifest.assets) {
        if (entry.kind != "texture-set" || slot < entry.first_slot ||
            slot >= entry.first_slot + entry.slot_count || entry.pack.empty()) {
            continue;
        }
        char name[16];
        std::snprintf(name, sizeof name, "/%02d.png", slot - entry.first_slot);
        std::string error;
        const Pack* pack = pack_open(packs, entry.pack, error);
        if (!pack) {
            std::fprintf(stderr, "stage: %s\n", error.c_str());
            return -1;
        }
        textures.pack = pack;
        const int texture = texture_for(renderer, textures, entry.file + name);
        textures.pack = nullptr;
        pack_close(packs, entry.pack);
        return texture;
    }
    return -1;
}

bool load_stage_view(StageView& view, const StageData& data, int selector, int day,
                     const AssetManifest& manifest, PackLibrary& packs, Renderer& renderer,
                     TextureCache& textures, std::string& error) {
    view = StageView{};
    view.data = &data;
    view.selector = selector;
    view.night = selector % 2;
    view.day = day;
    // what stage_props_enter loads for a battle that this draws: the props by selector,
    // the supply craft and the sky craft by night, and grid_mdl for the billboards
    if (!load_set(view, data.prop_assets[selector], manifest, packs, renderer, textures, error) ||
        !load_set(view, data.craft_assets[view.night], manifest, packs, renderer, textures,
                  error) ||
        !load_set(view, data.sky_assets[view.night], manifest, packs, renderer, textures, error) ||
        !load_set(view, grid_model_asset, manifest, packs, renderer, textures, error)) {
        return false;
    }
    // frame_cadence_init sets up slot 0x4DB at boot: uvs (0,0) (0,1) (1,0) (1,1) (the
    // file's are all 0), the texture's alpha counting (layer 0x40); textured_quad_draw_full makes
    // its section flags 0xA and its texture 0, the base being the picture's slot
    if (const ModelObject* grid = object_of(view, grid_slot)) {
        view.quad = *grid;
        const float uvs[4][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
        for (std::size_t v = 0; v < view.quad.vertices.size() && v < 4; ++v) {
            view.quad.vertices[v].uv[0] = uvs[v][0];
            view.quad.vertices[v].uv[1] = uvs[v][1];
        }
        for (ModelSection& section : view.quad.sections) {
            section.layer[0] = layer_texture_alpha;
            section.tex[0] = 0;
            section.flags = 0x8 | sub_indexed;
        }
        view.quad_mesh = renderer_upload_mesh(renderer, view.quad.vertices, view.quad.indices);
    }
    // the pictures drawn by slot: the crates' glow, the moon's phases, the adverts
    texture_by_slot(manifest, packs, renderer, textures, crate_glow_texture);
    for (int phase : data.moon_textures) {
        texture_by_slot(manifest, packs, renderer, textures, phase);
    }
    for (int i = 0; i < 5; ++i) {
        view.screen_textures[i] =
            texture_by_slot(manifest, packs, renderer, textures, data.sky_screen_textures[i]);
    }
    // the sky craft's two panels, rewritten as they scroll
    const int screens[2][2] = {{0x4C2, 0x4C6}, {0x4D1, 0x4D5}};
    for (int i = 0; i < 2; ++i) {
        const ModelObject* screen = object_of(view, screens[view.night][i]);
        view.screen_meshes[i] =
            screen ? renderer_upload_mesh(renderer, screen->vertices, screen->indices) : -1;
    }
    sky_craft_start(view.sky, data, selector);
    return true;
}

void stage_view_step(StageView& view, const StageWorld& world) {
    // the props as the step left them; a new list starts without a previous pose
    const auto placed = static_cast<std::size_t>(world.prop_count);
    const bool fresh = view.props.size() != placed;
    view.props.resize(placed);
    for (std::size_t i = 0; i < placed; ++i) {
        StageView::PropPose& pose = view.props[i];
        pose.previous = pose.current;
        pose.current = mat_of(world.props[i].transform);
        if (fresh) {
            pose.previous = pose.current;
        }
        pose.model = world.props[i].model;
        pose.drawn = !(world.props[i].flags & prop_hidden);
    }
    // the craft: a new route or a crash starts from where it is
    const SupplyCraft& craft = world.craft;
    view.craft_previous = view.craft_current;
    view.craft_current = mat_of(craft.matrix);
    if (craft.state != view.craft_state && (view.craft_state == craft_parked ||
                                            view.craft_state == craft_off)) {
        view.craft_previous = view.craft_current;
    }
    view.hatch_previous = view.hatch_current;
    view.hatch_current = fx_float(craft.hatch);
    view.rotor_previous = view.rotor_current;
    view.rotor_current = fx_float(craft.rotor);
    if (view.rotor_current < view.rotor_previous) {
        view.rotor_previous -= 6.2831855f;
    }
    view.craft_state = craft.state;
    for (int i = 0; i < 3; ++i) {
        const Crate& crate = world.crates[i];
        const float at[3] = {fx_float(crate.pos.x), fx_float(crate.pos.y), fx_float(crate.pos.z)};
        const bool shown = crate.state != crate_aboard && crate.state != crate_gone &&
                           i <= craft.crate_count;
        for (int k = 0; k < 3; ++k) {
            view.crate_previous[i][k] = view.crate_shown[i] ? view.crate_current[i][k] : at[k];
            view.crate_current[i][k] = at[k];
        }
        view.crate_shown[i] = shown;
        view.crate_kind[i] = crate.kind;
        view.crate_glow[i] = fx_float(crate.glow);
    }
    // the beacon steps once a draw of the flying craft at night
    if (view.night && craft.state != craft_off && craft.state != craft_parked &&
        craft.state != craft_paused && craft.state != craft_crashing) {
        view.beacon -= beacon_step;
        if (view.beacon < -2.5f) {
            view.beacon += 3.5f;
        }
    }
    if (!world.setup.ending) {
        sky_craft_step(view.sky, *view.data);
    }
}

void stage_view_draw(StageView& view, Renderer& renderer, const Mat4& view_matrix,
                     const float eye[3], float alpha, std::vector<PassItem>& out) {
    const StageData& data = *view.data;
    // the props (stage_prop_render): pass 2 at their transform
    for (const StageView::PropPose& pose : view.props) {
        if (pose.drawn) {
            draw_slot(view, pose.model, mat_lerp(pose.previous, pose.current, alpha), 2, 0,
                      view_matrix, out);
        }
    }

    // the supply craft (supply_craft_draw): pass 8 without fog (bit 25); the body with
    // mesh 0x1000000, the reflect shader (supply_craft_update)
    const int n = view.night;
    const int state = view.craft_state;
    if (state != craft_off && state != craft_parked && state != craft_paused) {
        const Mat4 m = mat_lerp(view.craft_previous, view.craft_current, alpha);
        if (state == craft_crashing) {
            draw_slot(view, data.craft_wreck[n], m, 8, mesh_no_fog, view_matrix, out);
        } else {
            // the hatch doors turn about z at their pivots by the hatch angle while it drops
            // (the pivots' z negated for the port)
            const float hatch =
                state == craft_hatch ? lerp(view.hatch_previous, view.hatch_current, alpha) : 0;
            draw_slot(view, data.craft_body[n], m, 8, mesh_no_fog | mesh_reflect, view_matrix, out);
            MatrixStack stack;
            stack_set(stack, m);
            stack_translate(stack, -0.735223f, 0.163461f, 2.73584f);
            stack_rotate_z(stack, -hatch);
            stack_translate(stack, 0.735223f, -0.163461f, -2.73584f);
            draw_slot(view, data.craft_hatch_a[n], stack_top(stack), 8, mesh_no_fog, view_matrix,
                      out);
            stack_set(stack, m);
            stack_translate(stack, 0.735223f, 0.163461f, -2.73584f);
            stack_rotate_z(stack, hatch);
            stack_translate(stack, -0.735223f, -0.163461f, 2.73584f);
            draw_slot(view, data.craft_hatch_b[n], stack_top(stack), 8, mesh_no_fog, view_matrix,
                      out);
            if (n) {
                // the beacon: white by the pulse, no alpha in the colour (as retail's)
                const float level = std::max(view.beacon, 0.0f);
                const std::uint32_t grey = static_cast<std::uint32_t>(255.0f * level);
                draw_slot(view, beacon_slot, m, 8, mesh_no_fog | mesh_factor, view_matrix, out, -1,
                          -1, grey << 16 | grey << 8 | grey);
            }
            // the rotor turns about y (the port's turn negated)
            stack_set(stack, m);
            stack_rotate_y(stack, -lerp(view.rotor_previous, view.rotor_current, alpha));
            draw_slot(view, data.craft_rotor[n], stack_top(stack), 8, mesh_no_fog, view_matrix,
                      out);
        }
    }

    // the crates (pickups_draw): the crate x 2.5, and a glow billboard x 3.7 pulled 1.8 toward
    // the camera, additive, alpha |sin(glow)| x 0x60
    for (int i = 0; i < 3; ++i) {
        if (!view.crate_shown[i]) {
            continue;
        }
        float at[3];
        for (int k = 0; k < 3; ++k) {
            at[k] = lerp(view.crate_previous[i][k], view.crate_current[i][k], alpha);
        }
        const Mat4 crate =
            mat_multiply(mat_scaling(2.5f, 2.5f, 2.5f), mat_translation(at[0], at[1], at[2]));
        draw_slot(view, data.crate_models[view.crate_kind[i]][n], crate, 2, 0, view_matrix, out);
        const std::uint32_t glow =
            static_cast<std::uint32_t>(std::fabs(std::sin(view.crate_glow[i])) * 0x60);
        const Mat4 board =
            mat_multiply(mat_scaling(3.7f, 3.7f, 3.7f), billboard_at(at, eye, 0, 0, -1.8f));
        draw_quad(view, board, crate_glow_texture, mesh_additive,
                  crate_colour(view.crate_kind[i]) + (glow << 24), view_matrix, out);
    }

    // the night's moon (stage_render_lods, odd selectors): a billboard x 80 at the stage's point,
    // its phase by the day
    if (n) {
        const Mat4 board = mat_multiply(
            mat_scaling(80, 80, 80), billboard_at(data.moon_points[view.selector / 2], eye, 0, 0, 0));
        draw_quad(view, board, data.moon_textures[view.day % 30], mesh_no_fog | mesh_additive,
                  0xFFFFFFFF, view_matrix, out);
    }

    sky_craft_draw(view, renderer, view_matrix, alpha, out);
}
