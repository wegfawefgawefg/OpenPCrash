#include "render/mesh_constants.hpp"

#include "stage/stage_animate.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// the blocks, std140, in each shader's member order (docs/shaders.md, "Per-draw blocks")
struct FixedDraw {
    Mat4 world, view, proj;
    float light_dir[4], light_diffuse[4], light_ambient[4], light_specular[4];
    float material_ambient[4], material[4];
};
struct FogDraw {
    Mat4 world_view_proj, world;
    float k[4];
};
struct CockpitDraw {
    Mat4 world_view_proj;
    float uv[4];
};
struct PointDraw {
    Mat4 world_view_proj, world;
    float k[4], light[4], diffuse[4], ambient[4];
    float light_pos[3][4];
    float ranges[4];
    float light_color[3][4];
};
struct BumpDraw {
    Mat4 world_view_proj, inverse_world, world;
    float k[4], light[4], eye[4];
};
struct ReflectDraw {
    Mat4 world_view_proj, world_view;
    float light[4], diffuse[4], ambient[4], k[4];
};
struct SeabumpDraw {
    Mat4 world_view_proj;
    float bump_env[4];
    std::uint32_t dudv_signed[4];
};
struct MultiMatrixPlDraw {
    Mat4 proj;
    Mat4 bone_view[19];
    Mat4 view, heading_inverse;
    float light[4], k[4], diffuse[4], ambient[4];
    float point_pos[4], lamp_pos[4], lamp_dir[4], point_color[4], lamp_color[4];
};
struct RainbowDraw {
    Mat4 world_view_proj;
    float light[4], diffuse[4], ambient[4], k[4], phase[4], strength[4];
};
struct EffoptDraw {
    Mat4 world_view_proj, inverse_world, world;
    float k[4], view_size[4], material[4], light[4], sphere[4], fog_k[4], view_origin[4], eye[4],
        glow[4];
};
struct MultiShadowDraw {
    Mat4 view_proj;
    Mat4 bones[19];
    float k[4], light_dir[4];
};
static_assert(sizeof(FixedDraw) == 288 && sizeof(FogDraw) == 144 && sizeof(PointDraw) == 304 &&
              sizeof(BumpDraw) == 240 && sizeof(ReflectDraw) == 192 && sizeof(SeabumpDraw) == 96 &&
              sizeof(MultiMatrixPlDraw) == 1552 && sizeof(MultiShadowDraw) == 1312 &&
              sizeof(RainbowDraw) == 160 && sizeof(EffoptDraw) == 336);

template <typename Block>
void put(const Block& block, FrameDraws& out, DrawItem& item) {
    item.uniform_offset = push_uniform(out, &block, sizeof block);
    item.uniform_size = sizeof block;
}

void set4(float* to, float x, float y, float z, float w) {
    to[0] = x;
    to[1] = y;
    to[2] = z;
    to[3] = w;
}

void set_rgb(float* to, const float* rgb, float w) {
    set4(to, rgb[0], rgb[1], rgb[2], w);
}

void set_argb(float* to, std::uint32_t colour, float w) {
    set4(to, static_cast<float>((colour >> 16) & 0xFF) / 255.0f,
         static_cast<float>((colour >> 8) & 0xFF) / 255.0f,
         static_cast<float>(colour & 0xFF) / 255.0f, w);
}

// -Light.Direction, normalized, w 0
Vec3f toward_light(const DrawContext& context) {
    const Vec3f d = context.lights->direction;
    return vec_normalize(Vec3f{-d.x, -d.y, -d.z});
}

Mat4 world_view_proj(const DrawContext& context, const MeshRecord& record) {
    return mat_multiply(mat_multiply(record.world, context.view), context.projection);
}

float distance(const float* a, const float* b) {
    return vec_length(Vec3f{a[0] - b[0], a[1] - b[1], a[2] - b[2]});
}

} // namespace

void fixed_constants(const DrawContext& context, const MeshRecord& record,
                     const ModelSection& section, FrameDraws& out, DrawItem& item) {
    const SceneLights& lights = *context.lights;
    FixedDraw block{};
    block.world = record.world;
    block.view = context.view;
    block.proj = context.projection;
    set4(block.light_dir, lights.direction.x, lights.direction.y, lights.direction.z, 0);
    std::memcpy(block.light_diffuse, lights.diffuse, sizeof block.light_diffuse);
    std::memcpy(block.light_ambient, lights.ambient, sizeof block.light_ambient);
    std::memcpy(block.light_specular, lights.specular, sizeof block.light_specular);
    set_argb(block.material_ambient, section.ambient, 1);
    block.material[0] = section.power;
    put(block, out, item);
}

// decomp: shader_constants_fog 0xC3E12
void fog_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                   DrawItem& item) {
    FogDraw block{};
    block.world_view_proj = world_view_proj(context, record);
    block.world = record.world;
    set4(block.k, 1.0f / context.lights->table->height_scale, 0, 1, 0.5f);
    put(block, out, item);
}

// decomp: shader_constants_cockpit_uv 0xC4051
void cockpit_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                       DrawItem& item) {
    CockpitDraw block{};
    block.world_view_proj = world_view_proj(context, record);
    set4(block.uv, 0, 1, record.effect[0], record.effect[1]);
    put(block, out, item);
}

// decomp: shader_constants_point 0xC2E7B
void point_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                     DrawItem& item) {
    const SceneLights& lights = *context.lights;
    PointDraw block{};
    block.world_view_proj = world_view_proj(context, record);
    block.world = record.world;
    set4(block.k, 0, 0.5f, 1, 1);
    const Vec3f light = toward_light(context);
    set4(block.light, light.x, light.y, light.z, 0);
    set_rgb(block.diffuse, lights.diffuse, 1);
    set_rgb(block.ambient, lights.ambient, 1);

    // the three nearest the mesh centre, one light each (the original's rounds marked every light
    // at the least distance at once and left later picks unset when lights tied; docs/bugs.md)
    float distances[6];
    for (int i = 0; i < 6; ++i) {
        distances[i] = distance(lights.points[i].position, record.center);
    }
    int near[3] = {0, 0, 0};
    for (int round = 0; round < 3; ++round) {
        int best = -1;
        for (int i = 0; i < 6; ++i) {
            if (distances[i] < 1e4f && (best < 0 || distances[i] < distances[best])) {
                best = i;
            }
        }
        if (best >= 0) {
            near[round] = best;
            distances[best] = 1e30f;
        }
    }
    for (int i = 0; i < 3; ++i) {
        const PointLight& point = lights.points[near[i]];
        set4(block.light_pos[i], point.position[0], point.position[1], point.position[2], 1);
        set_argb(block.light_color[i], point.colour, 0);
    }
    set4(block.ranges, lights.points[near[0]].range, lights.points[near[1]].range,
         lights.points[near[2]].range, 1.0f / lights.table->height_scale);
    put(block, out, item);
}

// decomp: shader_constants_bump 0xC0C3E
void bump_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                    DrawItem& item) {
    BumpDraw block{};
    block.world_view_proj = world_view_proj(context, record);
    block.world = record.world;
    block.inverse_world = mat_inverse(record.world);
    set4(block.k, 0.001f, 0.5f, 0, 1);
    // the light into object space (a w-0 vector through World^-1), normalized, w 0.25
    const Vec3f light = vec_normalize(transform_normal(toward_light(context), block.inverse_world));
    set4(block.light, light.x, light.y, light.z, 0.25f);
    const Mat4 camera = mat_inverse(context.view);
    set4(block.eye, camera.m[3][0], camera.m[3][1], camera.m[3][2], 0);
    put(block, out, item);
}

// decomp: shader_constants_reflect 0xC4625
void reflect_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                       DrawItem& item) {
    const SceneLights& lights = *context.lights;
    ReflectDraw block{};
    block.world_view = mat_multiply(record.world, context.view);
    block.world_view_proj = mat_multiply(block.world_view, context.projection);
    const Vec3f light = toward_light(context);
    set4(block.light, light.x, light.y, light.z, 0);
    set_rgb(block.diffuse, lights.diffuse, 1);
    set_rgb(block.ambient, lights.ambient, 1);
    set4(block.k, 1, 0, 5, 0.5f);
    put(block, out, item);
}

// decomp: shader_constants_seabump 0xC3C2B
void seabump_constants(const DrawContext& context, const MeshRecord& record, bool water,
                       FrameDraws& out, DrawItem& item) {
    SeabumpDraw block{};
    block.world_view_proj = world_view_proj(context, record);
    // the water's matrix turns with water_bump_angle x 3, scaled 0.05; other seabump meshes keep
    // whatever the last water left, taken here as the same matrix
    (void)water;
    const float a = context.water_bump_angle * 3.0f;
    set4(block.bump_env, std::cos(a) * 0.05f, -std::sin(a) * 0.05f, std::sin(a) * 0.05f,
         std::cos(a) * 0.05f);
    put(block, out, item);
}

// decomp: shader_constants_multi_matrix_pl 0xC0F8D
void fighter_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                       DrawItem& item) {
    const SceneLights& lights = *context.lights;
    const StageLighting& stage = *lights.stage;
    const StageLightingTable& table = *lights.table;
    MultiMatrixPlDraw block{};
    block.proj = context.projection;
    block.bone_view[0] = mat_multiply(record.world, context.view);
    block.view = context.view;
    // (RotationY(heading) x View, translation zeroed)^-1, uploaded untransposed
    Mat4 heading = mat_multiply(mat_rotation_y(record.heading), context.view);
    heading.m[3][0] = heading.m[3][1] = heading.m[3][2] = 0;
    block.heading_inverse = mat_inverse(heading);
    // -Light.Direction through View (w 0), normalized
    const Vec3f light = vec_normalize(transform_normal(toward_light(context), context.view));
    set4(block.light, light.x, light.y, light.z, 0);
    set_rgb(block.diffuse, lights.diffuse, 1);
    set_rgb(block.ambient, lights.ambient, 1);

    // the point light nearest the batch's first bone, within 500
    const float at[3] = {record.world.m[3][0], record.world.m[3][1], record.world.m[3][2]};
    int nearest = -1;
    float best = 500;
    for (int i = 0; i < 6; ++i) {
        const float d = distance(lights.points[i].position, at);
        if (d < best) {
            nearest = i;
            best = d;
        }
    }
    const PointLight& point = nearest >= 0 ? lights.points[nearest] : table.point_default;
    set4(block.k, point.range, 0, 0.5f, 1);
    set4(block.point_pos, point.position[0], point.position[1], point.position[2], 1);
    set_argb(block.point_color, point.colour, 0);

    // the nearest lamp of the zone's range, within 500; none: the default position with a zero
    // direction and colour. The zone is shibuya_zone_at's at the first bone on Shibuya (selectors
    // 2 and 3, game_get_tournament_day_mode), else 0
    int lamp = -1;
    if (stage.lamp_count) {
        best = 500;
        const bool shibuya = lights.selector == 2 || lights.selector == 3;
        const int zone = shibuya ? std::clamp(stage_zone_at(lights.selector, at), 0, 4) : 0;
        for (int k = stage.lamp_ranges[zone][0];
             k < stage.lamp_ranges[zone][1] && k < static_cast<int>(stage.lamps.size()); ++k) {
            const float d = distance(stage.lamps[k].position, at);
            if (d < best) {
                lamp = k;
                best = d;
            }
        }
    }
    if (lamp >= 0) {
        const StageLamp& use = stage.lamps[lamp];
        set_rgb(block.lamp_pos, use.position, 1);
        set_rgb(block.lamp_dir, use.direction, 1);
        set_rgb(block.lamp_color, use.colour, 0);
    } else {
        set_rgb(block.lamp_pos, table.lamp_default, 1);
        set4(block.lamp_dir, 0, 0, 0, 1);
    }
    put(block, out, item);
}

// decomp: shader_constants_multi_shadow 0xC25B7
void fighter_shadow_constants(const DrawContext& context, const MeshRecord& record,
                              FrameDraws& out, DrawItem& item) {
    MultiShadowDraw block{};
    block.view_proj = context.view_projection;
    block.bones[0] = record.world;
    set4(block.k, 50, 0.5f, 0, 1);
    const Vec3f d = vec_normalize(context.lights->direction);
    set4(block.light_dir, d.x, d.y, d.z, 0);
    put(block, out, item);
}

// decomp: shader_constants_rainbow 0xC432E
void rainbow_constants(const DrawContext& context, const MeshRecord& record, FrameDraws& out,
                       DrawItem& item) {
    const SceneLights& lights = *context.lights;
    RainbowDraw block{};
    block.world_view_proj = world_view_proj(context, record);
    const Vec3f light = toward_light(context);
    set4(block.light, light.x, light.y, light.z, 0);
    set_rgb(block.diffuse, lights.diffuse, 1);
    set_rgb(block.ambient, lights.ambient, 1);
    // (1, 0, scroll, 0.5); the bands a third apart about the phase; the strength (0..1)
    set4(block.k, 1, 0, record.effect[1], 0.5f);
    const float p = record.effect[2];
    set4(block.phase, p - 0.6666f, p, p + 0.6666f, 2);
    set4(block.strength, std::clamp(record.effect[0], 0.0f, 1.0f), 0, 0, 0);
    put(block, out, item);
}

// decomp: shader_constants_effopt 0xC0542
void effopt_constants(const DrawContext& context, const MeshRecord& record,
                      const ModelSection& section, FrameDraws& out, DrawItem& item) {
    const SceneLights& lights = *context.lights;
    EffoptDraw block{};
    block.world_view_proj = world_view_proj(context, record);
    block.world = record.world;
    block.inverse_world = mat_inverse(record.world);
    set4(block.k, 0.5f, 0.2f, 0, 1);
    const float* v = context.viewport;
    set4(block.view_size, v[2] / 2.0f, -v[3] / 2.0f, v[3] / 2.0f, 0);
    set4(block.view_origin, v[0], v[1], 0, 0);
    set_argb(block.material, section.ambient, 1);
    // -Light.Direction (w 0) through World^-1, normalized, times the light's diffuse red
    const Vec3f d = lights.direction;
    Vec3f light = transform_normal(Vec3f{-d.x, -d.y, -d.z}, block.inverse_world);
    light = vec_scale(vec_normalize(light), lights.diffuse[0]);
    set4(block.light, light.x, light.y, light.z, 0);
    // the reveal sphere's centre through World^-1 (as a point), its radius kept
    const Vec3f centre = transform_coord(
        Vec3f{record.effect[0], record.effect[1], record.effect[2]}, block.inverse_world);
    set4(block.sphere, centre.x, centre.y, centre.z, record.effect[3]);
    set4(block.fog_k, record.effect[3] + 4.0f, 1, 0.25f, 0);
    const Mat4 camera = mat_inverse(context.view);
    set4(block.eye, camera.m[3][0], camera.m[3][1], camera.m[3][2], 0);
    set4(block.glow, 0, 0, 0, record.effect[4]);
    put(block, out, item);
}
