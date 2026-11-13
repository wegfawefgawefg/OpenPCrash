// The camouflage's look: camo_update's reveal sphere, the shader's k, and pass 5 (pass_draw_camo
// with shader_constants_multi_optical). See camo_draw.hpp.
#include "render/camo_draw.hpp"

#include "render/mesh_constants.hpp"
#include "render/mesh_flags.hpp"
#include "view/view.hpp"

#include <algorithm>

namespace {

// the reflection map's slot (texture_slots_base + 0xC0), on stage 3 for this program
constexpr int reflection_slot = 0x18;
// texture_slot_vec4_set's radius, and the lift camo_update adds to its offset
constexpr float reveal_radius = 5.0f;
constexpr float reveal_lift = 10.0f;
// the k every AI task and every VS task sets, and optical_shader_k_set's start
constexpr float default_k = 0.25f;

// MultiOpticalDraw (assets/shaders/original/multi_optical.vert)
struct MultiOpticalDraw {
    Mat4 proj, view;
    Mat4 bones[19];
    float k[4], view_size[4], diffuse[4], light[4], sphere[4], fog_k[4], view_origin[4],
        ambient[4];
};
static_assert(sizeof(MultiOpticalDraw) == 1472);

void set4(float* to, float x, float y, float z, float w) {
    to[0] = x;
    to[1] = y;
    to[2] = z;
    to[3] = w;
}

Address address_u(std::uint32_t layer) {
    return layer & layer_wrap_u     ? Address::Wrap
           : layer & layer_mirror_u ? Address::Mirror
                                    : Address::Clamp;
}
Address address_v(std::uint32_t layer) {
    return layer & layer_wrap_v     ? Address::Wrap
           : layer & layer_mirror_v ? Address::Mirror
                                    : Address::Clamp;
}

int slot_of(const DrawContext& context, int slot) {
    const int texture = slot_texture(*context.renderer, *context.cache, *context.slots, slot);
    return texture >= 0 ? texture : texture_white;
}

// shader_constants_multi_optical: (0.5, k, 0, 1); the view's half size and origin in 640x480
// pixels; light 0's diffuse and ambient, -direction normalized with w 1; the mesh's reveal
// sphere and (R + 4, 1, 0.25, 0); the section's world matrix as bone 0
void optical_constants(const DrawContext& context, const MeshRecord& record, float k,
                       const float rect[4], FrameDraws& out, DrawItem& item) {
    const SceneLights& lights = *context.lights;
    MultiOpticalDraw block{};
    block.proj = context.projection;
    block.view = context.view;
    block.bones[0] = record.world;
    set4(block.k, 0.5f, k, 0, 1);
    set4(block.view_size, rect[2] / 2.0f, -rect[3] / 2.0f, rect[3] / 2.0f, 0);
    set4(block.diffuse, lights.diffuse[0], lights.diffuse[1], lights.diffuse[2], 1);
    const Vec3f d = vec_normalize(Vec3f{-lights.direction.x, -lights.direction.y,
                                        -lights.direction.z});
    set4(block.light, d.x, d.y, d.z, 1);
    std::copy(record.reveal, record.reveal + 4, block.sphere);
    set4(block.fog_k, record.reveal[3] + 4.0f, 1, 0.25f, 0);
    set4(block.view_origin, rect[0], rect[1], 0, 0);
    set4(block.ambient, lights.ambient[0], lights.ambient[1], lights.ambient[2], 1);
    item.uniform_offset = push_uniform(out, &block, sizeof block);
    item.uniform_size = sizeof block;
}

} // namespace

void camo_pose_from(const World& world, const Fighter& f, FighterPose& pose) {
    const Camo& camo = f.camo;
    pose.camo_state = camo.state;
    const float timer = fx_float(camo.timer);
    pose.camo_timer = timer;
    float lift = -reveal_lift;
    switch (camo.state) {
    case camo_pressed:
    case camo_fading_in:
        lift = timer / 60.0f * -reveal_lift;
        break;
    case camo_holding:
    case camo_hidden:
        lift = 0;
        break;
    case camo_ending:
    case camo_fading_out:
        lift = (60.0f - timer) / 60.0f * -reveal_lift;
        break;
    case camo_broken:
    case camo_broken_tail:
        lift = (20.0f - timer) / 20.0f * -reveal_lift;
        break;
    default:
        break;
    }
    const FxVec3 aim = f.combat.aim_point;
    set4(pose.camo_sphere, fx_float(aim.x), fx_float(aim.y) + lift + reveal_lift, fx_float(aim.z),
         reveal_radius);
    // optical_shader_k_set from the fighter's task, which a parked fighter doesn't run
    pose.camo_k = -1;
    if (f.present && !world.battle_state.slots[f.slot].parked) {
        pose.camo_k = f.controller != -1 && !world.vs.mode
                          ? static_cast<float>(camo.chip_shader_param) / 100.0f
                          : default_k;
    }
}

bool camo_pose_cloaked(const FighterPose& pose) {
    return pose.camo_state >= camo_pressed && pose.camo_state <= camo_broken_tail;
}

float camo_frame_k(const FighterPose* fighters, int count) {
    float k = default_k;
    for (int i = 0; i < count; ++i) {
        const float value = fighters[i].camo_k;
        if (fighters[i].present && value >= 0.0f && value <= 1.0f) {
            k = value;
        }
    }
    return k;
}

void draw_camo_fighters(const DrawContext& context, const std::vector<const PassItem*>& queue,
                        float k, const float rect[4], FrameDraws& out) {
    // pass_open_camo: the backdrop copy, fog, z on and written, no blending, stencil pass ZERO
    // as pass 4; the backdrop on stage 1 point filtered and clamped
    bool first = true;
    for (const PassItem* entry : queue) {
        const MeshRecord& record = entry->item;
        for (int j = 0; j < record.section_count; ++j) {
            const ModelSection& section = record.sections[j];
            DrawItem item;
            item.program = Program::fighter_camo;
            item.mesh = record.mesh;
            item.first_index = section.first_triangle * 3;
            item.index_count = section.triangle_count * 3;
            item.blend = Blend::none;
            item.stencil = Stencil::zero_on_pass;
            item.cull = section.flags & sub_two_sided ? Cull::none : Cull::back;
            item.state.flags = ds_fog;
            item.backdrop = first;
            first = false;
            // stage 0 only from the section (pass_draw_camo's loop runs one stage)
            if (section.tex[0] >= 0 && section.tex[0] < texture_stage_end) {
                item.stages[0].texture = record.paint >= 0
                                             ? record.paint
                                             : slot_of(context, section.tex[0] + record.texture_base);
                item.stages[0].u = address_u(section.layer[0]);
                item.stages[0].v = address_v(section.layer[0]);
            }
            item.stages[1].texture = texture_backdrop;
            item.stages[1].u = item.stages[1].v = Address::Clamp;
            item.stages[1].point = true;
            item.stages[3].texture = slot_of(context, reflection_slot);
            optical_constants(context, record, k, rect, out, item);
            out.items.push_back(item);
        }
    }
}
