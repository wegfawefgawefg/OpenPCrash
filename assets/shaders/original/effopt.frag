#version 450
#extension GL_GOOGLE_include_directive : require
// effopt.xpu, bind row 0, with effopt.vert: the shockwave decals. PS c0 = (0, 0, 0, glow), packed to 8 bits by the
// runtime (so a glow above 1 saturates here while the vertex side uses it as is).
//
// Texture modes:
//   t0 passthru     clamp01(oT0): the packed tangent-space normal; the decal's own stage 0 texture is never sampled
//   t1 project2d    the backdrop 0x222 (linear target, texel coordinates, point filter, clamp) at oT1
//   t2 dotproduct   s = oT2.xyz . t0.rgb (ZERO_TO_ONE: the packed normal is used as stored, not expanded)
//   t3 dot_st       t = oT3.xyz . t0.rgb; fetch bump_light.tga (stage 3, clamp, linear) at (s, t)
// Combiners:
//   stage 0  r1.rgb = t1.rgb * v0.rgb                 r1.a = t1.a * v0.a
//   stage 1  r0.rgb = t3.rgb                          r0.a = t3.a
//   stage 2  r0.rgb = t3.a * c0.a + r1.rgb            r0.a = t3.a * c0.a + r1.a
//   stage 3                                           r0.a = t1.a
// So: backdrop x vertex colour + highlight x glow, alpha the backdrop's. Pass 6 draws without blending, no z write, fog
// on, stencil pass ZERO. Stage 0 has no texture, so its alpha kill has nothing to test.

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"

layout(set = 1, binding = 0, std140) uniform EffoptDraw
{
    mat4 world_view_proj;
    mat4 inverse_world;
    mat4 world;
    vec4 k;
    vec4 view_size;
    vec4 material;
    vec4 light;
    vec4 sphere;
    vec4 fog_k;
    vec4 view_origin;
    vec4 eye;
    vec4 glow; // PS c0
}
draw;

layout(set = 2, binding = 1) uniform sampler2D tex1; // backdrop 0x222
layout(set = 2, binding = 3) uniform sampler2D tex3; // bump_light.tga

layout(location = 0) in vec4 v_d0;
layout(location = 2) in vec4 v_t0;
layout(location = 3) in vec4 v_t1;
layout(location = 4) in vec4 v_t2;
layout(location = 5) in vec4 v_t3;
layout(location = 6) in float v_fog;

layout(location = 0) out vec4 out_color;

void main()
{
    // texture stages
    vec4 t0 = nv_passthru(v_t0);
    vec4 t1 = texture(tex1, v_t1.xy * frame.linear_size.zw);
    float s = dot(v_t2.xyz, t0.rgb);
    float t = dot(v_t3.xyz, t0.rgb);
    vec4 t3 = texture(tex3, vec2(s, t));
    float c0_alpha = nv_q8(draw.glow).a;

    // combiner stages 0..3
    vec4 r1 = nv_clamp(t1 * v_d0);
    vec4 r0 = t3;
    r0.rgb = nv_clamp(t3.a * c0_alpha + r1.rgb);
    r0.a = nv_clamp(t3.a * c0_alpha + r1.a);
    r0.a = t1.a;

    out_color = d3d_finish(r0, v_fog);
}
