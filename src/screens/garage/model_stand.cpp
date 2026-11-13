// The model stand (decomp model_viewer_* 0x83621.., garage_stand_load_loadout,
// model_viewer_build_slot, model_viewer_actor_update).
#include "screens/garage/model_stand.hpp"

#include "cook/pack_library.hpp"
#include "mech/mech_pose.hpp"
#include "render/mesh_draw.hpp"
#include "screens/ui/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr float pi = 3.14159265f;
// the texture set model_viewer_start loads (0x615, the battle's)
constexpr int viewer_texture_set = 0x615;
// the idle motion slot and its length in ticks (as the stage viewer's)
constexpr int idle_slot = 28;
constexpr int idle_ticks = 44;

// The per-class points D_00242648 names, by leg class 0..20 (body class without legs). They are
// code, not data: func_0011DC90, _11DEB0, _11E0D0 and _11E2E0 write them at boot.
constexpr const char* point_tables[4] = {"model_viewer_points_a", "model_viewer_points_b",
                                         "model_viewer_points_c", "model_viewer_points_d"};
constexpr float class_points[4][21][3] = {
    // D_0098C618, the dealer's (func_0011DC90)
    {{1.3f, 0.6f, -6.0f}, {1.3f, 0.4f, -5.5f}, {1.2f, 0.7f, -6.0f},  {1.2f, 0.5f, -5.8f},
     {1.2f, 0.7f, -6.0f}, {1.4f, 0.2f, -5.0f}, {1.5f, -0.2f, -5.0f}, {1.5f, -0.2f, -5.0f},
     {1.3f, 0.2f, -5.0f}, {1.5f, 0.3f, -5.0f}, {1.3f, 0.6f, -5.0f},  {1.4f, 0.4f, -5.3f},
     {1.4f, 0.5f, -5.0f}, {1.4f, 0.5f, -5.3f}, {1.4f, 0.4f, -5.3f},  {1.3f, 0.6f, -5.0f},
     {1.3f, 0.7f, -5.3f}, {1.4f, 0.5f, -5.0f}, {1.4f, 0.5f, -5.3f},  {1.4f, 0.4f, -5.3f},
     {1.3f, 0.6f, -5.0f}},
    // D_0098C818, Wild Machines' (func_0011DEB0)
    {{2.0f, 0.4f, -5.69f}, {2.3f, 0.2f, -4.5f},  {2.0f, 0.4f, -5.69f}, {2.3f, 0.2f, -4.5f},
     {2.0f, 0.2f, -5.3f},  {2.2f, -0.3f, -3.8f}, {2.3f, -0.1f, -4.0f}, {2.3f, -0.1f, -4.0f},
     {2.3f, 0.1f, -4.2f},  {2.4f, 0.3f, -3.9f},  {2.4f, 0.5f, -3.9f},  {2.5f, 0.2f, -4.8f},
     {2.4f, 0.1f, -4.3f},  {2.5f, 0.2f, -4.8f},  {2.4f, 0.1f, -4.3f},  {2.5f, 0.2f, -3.8f},
     {2.5f, 0.2f, -3.8f},  {2.37f, 0.4f, -3.7f}, {2.1f, 0.3f, -3.5f},  {2.1f, 0.3f, -3.5f},
     {2.0f, 0.4f, -5.69f}},
    // D_0098C518, the spec page's (func_0011E0D0)
    {{3.3f, 0.4f, -3.09f}, {3.3f, 0.4f, -3.09f}, {3.3f, 0.4f, -3.09f}, {3.3f, 0.4f, -3.09f},
     {3.3f, 0.4f, -3.09f}, {3.5f, 0.3f, -2.3f},  {3.5f, 0.0f, -2.3f},  {3.5f, 0.0f, -2.3f},
     {3.5f, 0.3f, -2.3f},  {3.5f, 0.3f, -2.3f},  {3.5f, 0.3f, -2.3f},  {3.6f, 0.3f, -2.3f},
     {3.6f, 0.3f, -2.3f},  {3.6f, 0.3f, -2.3f},  {3.6f, 0.3f, -2.3f},  {3.6f, 0.3f, -2.3f},
     {3.6f, 0.3f, -2.3f},  {3.6f, 0.3f, -2.3f},  {3.6f, 0.3f, -2.3f},  {3.6f, 0.3f, -2.3f},
     {3.6f, 0.3f, -2.3f}},
    // D_0098C718, the garage's (func_0011E2E0)
    {{3.1f, 1.0f, -3.0f}, {3.1f, 1.0f, -3.0f}, {3.1f, 1.0f, -3.0f}, {3.1f, 1.0f, -3.0f},
     {3.1f, 1.0f, -3.0f}, {2.9f, 0.4f, -2.5f}, {2.9f, 0.4f, -2.5f}, {2.9f, 0.4f, -2.5f},
     {2.9f, 0.8f, -2.5f}, {2.9f, 0.8f, -2.5f}, {2.9f, 0.8f, -2.5f}, {3.0f, 0.9f, -2.4f},
     {3.0f, 0.9f, -2.4f}, {3.0f, 0.9f, -2.4f}, {3.0f, 0.9f, -2.4f}, {3.0f, 0.9f, -2.4f},
     {3.0f, 0.9f, -2.4f}, {3.0f, 1.1f, -2.3f}, {3.0f, 0.9f, -2.4f}, {3.0f, 1.0f, -2.4f},
     {3.1f, 1.0f, -3.0f}},
};

// a float table's values, false when the data has none (an old extraction)
bool floats(Ui& ui, const char* name, float* out, std::size_t count, std::size_t skip = 0) {
    const UiTable* t = ui_table(ui.data, "model_viewer", name);
    if (!t || t->values.size() < skip + count) {
        std::fprintf(stderr, "model stand: no %s in ui/layouts/model_viewer.sexp (re-extract ui)\n",
                     name);
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = static_cast<float>(t->values[skip + i]);
    }
    return true;
}

bool ints(Ui& ui, const char* name, std::vector<int>& out) {
    const UiTable* t = ui_table(ui.data, "model_viewer", name);
    if (!t || t->values.empty()) {
        std::fprintf(stderr, "model stand: no %s in ui/layouts/model_viewer.sexp (re-extract ui)\n",
                     name);
        return false;
    }
    out.clear();
    for (double v : t->values) {
        out.push_back(static_cast<int>(v));
    }
    return true;
}

// the original's tables (src/data/sv_attach.c) out of the ui data
bool tables_read(Ui& ui, StandTables& t) {
    float cameras[70] = {};
    std::vector<int> unused;
    bool ok = floats(ui, "model_viewer_places", &t.places[0][0], 35) &&
              floats(ui, "model_viewer_cameras", cameras, 70) &&
              floats(ui, "model_viewer_spread_a", &t.spread[0][0][0], 36) &&
              floats(ui, "model_viewer_spread_b", &t.spread[1][0][0], 36, 6 * 36) &&
              ints(ui, "model_viewer_fit_body", t.by_body) &&
              ints(ui, "model_viewer_fit_leg", t.by_leg) &&
              ints(ui, "model_viewer_fit_arm", t.by_arm) &&
              ints(ui, "model_viewer_fit_back", t.by_back);
    if (!ok) {
        return false;
    }
    // each mode's light: the camera row's last three (eye, target, fov come first)
    for (int m = 0; m < 7; ++m) {
        for (int k = 0; k < 3; ++k) {
            t.lights[m][k] = cameras[m * 10 + 7 + k];
        }
    }
    const UiTable* places = ui_table(ui.data, "model_viewer", "model_viewer_part_places");
    const UiTable* points = ui_table(ui.data, "model_viewer", "model_viewer_class_points");
    if (!places || !points || points->strings.size() < 7) {
        std::fprintf(stderr, "model stand: no D_00242038 or D_00242648 (re-extract ui)\n");
        return false;
    }
    t.part_places.clear();
    for (std::size_t i = 0; i + 6 <= places->values.size(); i += 6) {
        std::array<float, 6> row;
        for (int k = 0; k < 6; ++k) {
            row[static_cast<std::size_t>(k)] = static_cast<float>(places->values[i + k]);
        }
        t.part_places.push_back(row);
    }
    // which modes stand machines by class: the symbol each mode's word names
    for (int m = 0; m < 7; ++m) {
        t.class_points[m] = -1;
        for (int k = 0; k < 4; ++k) {
            if (points->strings[static_cast<std::size_t>(m)] == point_tables[k]) {
                t.class_points[m] = k;
            }
        }
    }
    return true;
}

// the mode's place row's point
void place_point(const ModelStand& stand, float out[3]) {
    const float* row = stand.tables.places[stand.mode];
    out[0] = row[0], out[1] = row[1], out[2] = row[2];
}

float place_heading(const ModelStand& stand) {
    return stand.tables.places[stand.mode][3];
}

// model_viewer_build_slot: a loaded machine onto the stand. A complete one stands at its class's
// point and turns, from the last heading in the garage (mode 0) or the row's elsewhere; an
// incomplete one is laid out at the row's place (the class point in the garage), facing the
// row's heading, and stays still
void swap_in(ModelStand& stand, MechParts* mech, bool complete) {
    stand.shown = mech;
    stand.shown_spread = mech && !complete;
    stand.turn = stand.tables.places[stand.mode][4];
    if (!mech) {
        return;
    }
    if (complete) {
        stand_offset(stand, *mech, false, stand.point);
        if (stand.mode != 0) {
            stand.heading = place_heading(stand);
        }
        return;
    }
    if (stand.mode == 0) {
        stand_offset(stand, *mech, false, stand.point);
    } else {
        place_point(stand, stand.point);
    }
    stand.heading = place_heading(stand);
}

} // namespace

void stand_start(Ui& ui, ModelStand& stand, int mode) {
    stand.mode = mode >= 0 && mode < 7 ? mode : 0;
    stand.shown = nullptr;
    stand.part = nullptr;
    stand.want = false;
    stand.want_kind = -1;
    stand.dragging = false;
    stand.line_shown = -1;
    for (StandSlot& slot : stand.line) {
        slot = StandSlot{};
    }
    stand.area = PointerRect{340, 96, 640, 330};
    if (!stand.failed && !tables_read(ui, stand.tables)) {
        stand.failed = true;
    }
    if (stand.failed) {
        return;
    }
    // model_viewer_thread_start: heading 0; the first machine takes the row's
    stand.heading = place_heading(stand);
    stand.turn = stand.tables.places[stand.mode][4];
    place_point(stand, stand.point);
    if (stand.started) {
        return;
    }
    PackLibrary& packs = *ui.data.packs;
    std::string error;
    if (!load_manifest(packs.core, stand.manifest, error) ||
        !load_fighter_tables(packs.core, stand.fighters, error)) {
        std::fprintf(stderr, "model stand: %s\n", error.c_str());
        stand.failed = true;
        return;
    }
    build_texture_slots(stand.slots, stand.manifest, {viewer_texture_set});
    if (const Pack* pack = pack_open(packs, "battle", error)) {
        load_slot_textures(*ui.renderer, stand.textures, stand.slots, *pack);
        pack_close(packs, "battle");
    }
    stand.fighters.builds.push_back(FighterBuild{});
    stand.started = true;
}

void stand_offset(const ModelStand& stand, const MechParts& mech, bool shifted, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    const int table = stand.tables.class_points[stand.mode];
    if (table >= 0) {
        const int klass = mech.leg_class >= 0 ? mech.leg_class : mech.body_class;
        if (klass >= 0 && klass < 21) {
            out[0] = class_points[table][klass][0];
            out[1] = class_points[table][klass][1];
            out[2] = class_points[table][klass][2];
        }
    } else {
        place_point(stand, out);
    }
    if (shifted && (stand.mode == 2 || stand.mode == 3)) {
        out[0] += 8.0f;
    }
}

void stand_show(Ui&, ModelStand& stand, const ShopBuild& b) {
    stand.want = true;
    stand.wanted = b;
}

void stand_hide(ModelStand& stand) {
    stand.shown = nullptr;
    stand.part = nullptr;
    stand.want = false;
    stand.want_kind = -1;
    stand.dragging = false;
    stand_line_hide(stand);
}

void stand_fit_part(Ui&, ModelStand& stand, int kind, int id) {
    if (kind < 0 || kind > 3 || id < 0) {
        stand.part = nullptr;
        stand.want_kind = -1;
        return;
    }
    stand.want_kind = kind;
    stand.want_id = id;
}

void stand_line_set(ModelStand& stand, const ShopBuild* builds, int count) {
    stand.line_shown = -1;
    for (int i = 0; i < 6; ++i) {
        stand.line[i] = StandSlot{};
        if (i < count) {
            stand.line[i].build = builds[i];
            stand.line[i].used = true;
        }
    }
}

void stand_line_hide(ModelStand& stand) {
    for (StandSlot& slot : stand.line) {
        slot.visible = false;
    }
    stand.line_shown = -1;
}

void stand_line_follow(Ui& ui, ModelStand& stand, int row) {
    if (!stand.started) {
        return;
    }
    // _shown_machine_update: the cursor's machine once built, its task shown
    const bool on = row >= 0 && row < 6 && stand.line[row].used;
    if (on && stand.line[row].mech) {
        stand.line[row].visible = true;
        stand.line_shown = row;
    } else {
        stand.line_shown = -1;
    }
    // _model_slot_pick: while nothing loads, the cursor's machine, else (the dealer) the first of
    // rows 0..2 not built or (Wild Machines) the last
    if (!stand_idle(stand)) {
        return;
    }
    int pick = on && !stand.line[row].mech ? row : -1;
    for (int i = 0; i < 3 && pick < 0; ++i) {
        const int r = stand.mode == 3 ? 2 - i : i;
        if (stand.line[r].used && !stand.line[r].mech) {
            pick = r;
        }
    }
    if (pick < 0) {
        return;
    }
    StandSlot& slot = stand.line[pick];
    bool complete = false, loading = false;
    MechParts* mech = stand_load(ui, stand, slot.build, stand.mode != 3, &complete, &loading);
    if (loading) {
        return;
    }
    if (!mech || !complete) {
        slot.used = false; // nothing to stand up: retail lays it out, no line-up machine does
        return;
    }
    // built: it starts beside the stand, facing the row's heading
    slot.mech = mech;
    stand_offset(stand, *mech, true, slot.point);
    slot.heading = place_heading(stand);
}

void stand_update(Ui& ui, ModelStand& stand) {
    if (!stand.started) {
        return;
    }
    stand_service(ui, stand);
    // the swaps: the asked machine or part once loaded (the old one stays until then)
    if (stand.want) {
        bool complete = false, loading = false;
        MechParts* mech = stand_load(ui, stand, stand.wanted, stand.mode != 3 && stand.mode != 5,
                                     &complete, &loading);
        if (!loading) {
            stand.want = false;
            stand_parts_present(stand.wanted, stand.shown_present);
            swap_in(stand, mech, complete);
        }
    }
    if (stand.want_kind >= 0) {
        // a build of that one part: arms on mount A, backs on back A
        ShopBuild b;
        b.sticker = 0xcd;
        if (stand.want_kind == 0) {
            b.body = stand.want_id;
        } else if (stand.want_kind == 1) {
            b.leg = stand.want_id;
        } else {
            b.weapon[stand.want_kind == 2 ? 0 : 2] = stand.want_id;
        }
        bool loading = false;
        MechParts* mech = stand_load(ui, stand, b, false, nullptr, &loading);
        if (!loading) {
            stand.part_place = mech ? stand_part_place(stand.tables, stand.want_kind, *mech) : -1;
            stand.part = stand.part_place >= 0 ? mech : nullptr;
            stand.want_kind = -1;
            stand.grow = 0;
            stand.part_heading = place_heading(stand);
        }
    }

    // PC: the mouse or the keys turning the machine; the turn waits while they hold it
    stand_turn_input(ui, stand);
    const float turn = stand.user_held ? 0.0f : stand.turn * pi / 180.0f;
    // model_viewer_actor_update: a standing machine turns (the parts laid out and a fitted part
    // have no running actor and keep still)
    if (stand.shown && !stand.shown_spread) {
        stand.heading += turn;
    }
    if (stand.part) {
        stand.part_heading += stand.user_turn;
    } else if (stand.shown) {
        stand.heading += stand.user_turn;
    }
    // the line-up: each shown machine turns, and slides 5% of the way an update to the stand
    // (the cursor's) or 8 to the side (the others)
    for (int i = 0; i < 6; ++i) {
        StandSlot& slot = stand.line[i];
        if (!slot.visible || !slot.mech) {
            continue;
        }
        slot.heading += turn;
        if (i == stand.line_shown) {
            slot.heading += stand.user_turn;
        }
        float target[3];
        stand_offset(stand, *slot.mech, i != stand.line_shown, target);
        for (int k = 0; k < 3; ++k) {
            slot.point[k] = 0.95f * slot.point[k] + 0.050000012f * target[k];
        }
    }
    // model_viewer_draw_grow: a fitted part grows in over 15 updates
    if (stand.part) {
        stand.grow = std::min(stand.grow + 1, 15);
    }
    ++stand.ticks;
}

void stand_draw(Ui& ui, ModelStand& stand) {
    StandPose poses[7];
    int count = 0;
    if (stand.part) {
        // the fitted part at the mode's place (the actor's), growing in
        StandPose& pose = poses[count++];
        pose.mech = stand.part;
        place_point(stand, pose.point);
        pose.heading = stand.part_heading;
        pose.part_place = stand.part_place;
        pose.scale = static_cast<float>(stand.grow) / 15.0f;
    } else if (stand.shown) {
        StandPose& pose = poses[count++];
        pose.mech = stand.shown;
        std::copy(stand.point, stand.point + 3, pose.point);
        pose.heading = stand.heading;
        if (stand.shown_spread) {
            pose.spread = 0;
            std::copy(stand.shown_present, stand.shown_present + 6, pose.present);
        }
    }
    for (const StandSlot& slot : stand.line) {
        if (slot.visible && slot.mech) {
            StandPose& pose = poses[count++];
            pose.mech = slot.mech;
            std::copy(slot.point, slot.point + 3, pose.point);
            pose.heading = slot.heading;
        }
    }
    stand_draw_poses(ui, stand, poses, count);
}

void stand_draw_poses(Ui& ui, ModelStand& stand, const StandPose* poses, int count) {
    stand_service(ui, stand);
    if (count <= 0 || !ui.renderer || !stand.started) {
        return;
    }
    Renderer& renderer = *ui.renderer;

    // the original's frame to the port's: z negated, turns about y reversed
    const float eye[3] = {0.0f, 2.0f, 12.0f};
    // the whole window's shape, the original's vertical field of view (docs/screens.md
    // "Widescreen")
    const float whole[4] = {0, 0, 640, 480};
    const float aspect = renderer_scene_aspect(renderer, whole);
    const CameraMatrices matrices = camera_matrices(eye, pi, 0.0f, aspect);
    SceneLights lights;
    const float* light = stand.tables.lights[stand.mode];
    const float lx = light[0], ly = light[1], lz = -light[2];
    const float length = std::sqrt(lx * lx + ly * ly + lz * lz);
    lights.direction = Vec3f{lx / length, ly / length, lz / length};
    lights.stage = &stand.menu_stage;
    lights.table = &stand.menu_table;

    // whole machines in the fighter pass, parts laid out or fitted in pass 2
    std::vector<PassItem> items, parts;
    for (int i = 0; i < count; ++i) {
        const StandPose& p = poses[i];
        if (!p.mech) {
            continue;
        }
        if (p.spread >= 0 || p.part_place >= 0) {
            stand_parts_items(stand.tables, p, parts);
            continue;
        }
        // the fighter: heading h faces (sin h, 0, cos h) in the original, pi - h in the port
        const float heading = pi - p.heading;
        const Mat4 world = mat_multiply(mat_rotation_y(heading),
                                        mat_translation(p.point[0], p.point[1], -p.point[2]));
        MechPose pose;
        build_mech_pose(mech_pose_input(*p.mech, idle_slot,
                                        static_cast<float>(stand.ticks % idle_ticks), world),
                        pose);
        const std::size_t first = items.size();
        mech_draw_items(*p.mech, pose, eye, items);
        for (std::size_t k = first; k < items.size(); ++k) {
            items[k].item.heading = heading;
        }
        // its sticker (actor_draw, pass 2)
        mech_sticker_items(*p.mech, pose, 2, 0x400, 0xFFFFFFFF, parts);
    }
    std::vector<const PassItem*> queue;
    for (const PassItem& item : items) {
        queue.push_back(&item);
    }

    DrawContext context;
    context.renderer = &renderer;
    context.cache = &stand.textures;
    context.slots = &stand.slots;
    context.lights = &lights;
    context.view = matrices.view;
    context.projection = matrices.projection;
    context.view_projection = mat_multiply(matrices.view, matrices.projection);
    context.eye = matrices.eye;
    context.pass = 4;
    context.battle = false;
    draw_fighters(context, queue, ui.scene);
    if (!parts.empty()) {
        std::vector<const PassItem*> part_queue;
        for (const PassItem& item : parts) {
            part_queue.push_back(&item);
        }
        context.pass = 2;
        draw_meshes(context, part_queue, ui.scene);
    }
    ui.scene_drawn = true;
}
