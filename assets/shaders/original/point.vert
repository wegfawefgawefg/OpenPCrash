#version 450
#extension GL_GOOGLE_include_directive : require
// point.xvu (UNLIT false) and pointconst.xvu (UNLIT true), bind rows 0x10..0x13, with point.frag:
//   0x10 point + point.xpu            lit, translucent       0x11 point + pointalpha.xpu        lit, opaque
//   0x12 pointconst + point.xpu       unlit, translucent     0x13 pointconst + pointalpha.xpu   unlit, opaque
// Battle only (lit_shader_alt), pass 2, textured sub-meshes of meshes without a texture factor (flag 0x20), when no row
// earlier in pass_draw_meshes' list matched. "Opaque" is pass_draw_meshes' flag: the last textured stage had no
// layer bit 0x40, or the mesh has 0x40000.
//
// Constants: include/point_draw.glsl.
// Inputs: format A, v0 position, v1 normal, v2 colour (pointconst), v3 uv.
// Outputs: oPos, oD0, oT0 = uv, oT1..oT3 = (Pw - Li) / (2 ri) + 0.5 per light, oT1.w = max(N . -(Pw - L0), 0) (never
// read by the pixel side), oFog.
// Computes:
//   point:      oD0 = diffuse x max(Nw . light, 0) + ambient, alpha 1 (Nw = the normal through World, not normalized)
//               oFog = clip z x f0 x f1 x f2, fi = min(|Pw - Li| / range 0, 1): every light divided by the first one's
//               range, as written, so a lit mesh stays out of the fog near the lights
//   pointconst: oD0 = the vertex colour
//               oFog = clip z x (1 - clamp01(P.y x ranges.w)): height fog on the object-space y
// Changed: the viewport epilogue is dropped; oT2.w and oT3.w (never written) are the assumed defaults.

#include "include/d3d.glsl"
#include "include/frame.glsl"
#include "include/point_draw.glsl"

layout(constant_id = 0) const bool UNLIT = false;

layout(location = 0) in vec4 in_pos;    // v0
layout(location = 1) in vec3 in_normal; // v1
layout(location = 2) in vec4 in_color;  // v2
layout(location = 3) in vec2 in_uv;     // v3

layout(location = 0) out vec4 v_d0;
layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 4) out vec4 v_t2;
layout(location = 5) out vec4 v_t3;
layout(location = 6) out float v_fog;

void main()
{
    vec4 clip = draw.world_view_proj * in_pos;
    gl_Position = clip;
    v_t0 = vec4(in_uv, 0.0, 1.0);

    // world position and normal
    vec3 pos_world = (draw.world * in_pos).xyz;
    vec3 normal_world = mat3(draw.world) * in_normal;

    // each light's offset scaled into 0..1 around 0.5 for the PASSTHRU stages
    vec3 d0 = pos_world - draw.light_pos[0].xyz;
    vec3 d1 = pos_world - draw.light_pos[1].xyz;
    vec3 d2 = pos_world - draw.light_pos[2].xyz;
    float n_dot_d0 = max(dot(normal_world, -d0), draw.k.x);
    v_t1 = vec4(d0 * (1.0 / (2.0 * draw.ranges.x)) + draw.k.y, n_dot_d0);
    v_t2 = vec4(d1 * (1.0 / (2.0 * draw.ranges.y)) + draw.k.y, NV2A_DEFAULT_TEXCOORD.w);
    v_t3 = vec4(d2 * (1.0 / (2.0 * draw.ranges.z)) + draw.k.y, NV2A_DEFAULT_TEXCOORD.w);

    // pointconst: vertex colour, height fog
    if (UNLIT) {
        v_d0 = clamp(in_color, 0.0, 1.0);
        float height = min(max(in_pos.y * draw.ranges.w, draw.k.x), draw.k.z);
        v_fog = d3d_fog_factor(clip.z * (draw.k.z - height));
        return;
    }

    // point: lit by light 0, fog thinned near the lights (all by range 0)
    float n_dot_l = max(dot(normal_world, draw.light.xyz), draw.k.x);
    v_d0 = clamp(vec4(draw.diffuse.rgb * n_dot_l + draw.ambient.rgb, draw.k.z), 0.0, 1.0);
    float f0 = min(length(d0) * (1.0 / draw.ranges.x), draw.k.z);
    float f1 = min(length(d1) * (1.0 / draw.ranges.x), draw.k.z);
    float f2 = min(length(d2) * (1.0 / draw.ranges.x), draw.k.z);
    v_fog = d3d_fog_factor(f0 * f1 * f2 * clip.z);
}
