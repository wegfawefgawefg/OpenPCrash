#version 450
#extension GL_GOOGLE_include_directive : require
// bump.xpu, bind row 2, with bump.vert. No PS constants are set.
//
// Texture modes:
//   t0 project2d    the sub-mesh's stage 0 texture (the colour map) at oT0
//   t1 project2d    the sub-mesh's stage 1 texture (a normal map, gloss in alpha) at oT1
//   t2 dotproduct   s = oT2.xyz . n, n = t1.rgb through MINUS1_TO_1_D3D; no fetch
//   t3 dot_st       t = oT3.xyz . n; fetch bump_light.tga (stage 3, clamp, linear) at (s, t)
// Combiners:
//   stage 0  r0.rgb = t3.rgb               r0.a = t3.a
//   stage 1                                r1.a = t3.a * t1.a
//   stage 2  r0.rgb = r0.rgb * t0.rgb + r1.a   r0.a = r0.a * t0.a + r1.a
//   stage 3                                r0.a = t0.a
// So: colour = lookup(H.N, L.N).rgb x colour map + lookup.a x gloss, alpha = the colour map's. No vertex colour; fog by
// D3D's final combiner.

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"

layout(set = 2, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 1) uniform sampler2D tex1;
layout(set = 2, binding = 3) uniform sampler2D tex3; // bump_light.tga

layout(location = 2) in vec4 v_t0;
layout(location = 3) in vec4 v_t1;
layout(location = 4) in vec4 v_t2;
layout(location = 5) in vec4 v_t3;
layout(location = 6) in float v_fog;

layout(location = 0) out vec4 out_color;

void main()
{
    // texture stages
    vec4 t0 = texture(tex0, stage_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);
    vec4 t1 = texture(tex1, stage_uv(1, v_t1.xy));
    vec3 n = nv_dotmap_d3d(t1.rgb);
    float s = dot(v_t2.xyz, n);
    float t = dot(v_t3.xyz, n);
    vec4 t3 = texture(tex3, vec2(s, t));

    // combiner stages 0..3
    vec4 r0 = t3;
    float r1a = nv_clamp(t3.a * t1.a);
    r0.rgb = nv_clamp(r0.rgb * t0.rgb + r1a);
    r0.a = nv_clamp(r0.a * t0.a + r1a);
    r0.a = t0.a;

    out_color = d3d_finish(r0, v_fog);
}
