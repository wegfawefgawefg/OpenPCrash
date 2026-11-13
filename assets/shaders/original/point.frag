#version 450
#extension GL_GOOGLE_include_directive : require
// point.xpu (ALPHA_FROM_VERTEX false) and pointalpha.xpu (ALPHA_FROM_VERTEX true), bind rows 0x10..0x13, with
// point.vert. PS c1..c3 = the three lights' colours (include/point_draw.glsl).
//
// Texture modes: t0 project2d (the colour map), t1..t3 passthru (clamp01 of oT1..oT3).
// Combiners (point.xpu; pointalpha.xpu adds stage 7):
//   stage 0  t1.rgb = dot(exp(t1.rgb), exp(t1.rgb))                    (expanded back to (Pw - L0) / r0, clamped
//                                                                       -1..1 per component, squared length)
//   stage 1  t2.rgb = dot(exp(t2.rgb), exp(t2.rgb)), t3 the same       (blue copied to alpha; never used)
//   stage 2  r1.rgb = inv(t1.rgb) * c1.rgb + inv(t2.rgb) * c2.rgb      r1.a = ... x c1.a, c2.a = 0
//   stage 3  r1.rgb = inv(t3.rgb) * c3.rgb + r1.rgb
//   stage 4                                                            r1.a = 0
//   stage 5  r1.rgb = r1.rgb + v0.rgb                                  r1.a = r1.a + v0.a
//   stage 6  r0.rgb = t0.rgb * r1.rgb
//   stage 7  (pointalpha only)                                         r0.a = v0.a
// r0.a is never written by point.xpu, so it keeps its start value t0.a.
// So: colour map x clamp(sum of (1 - min(d_i^2, 1)) x colour_i + vertex colour), every partial sum clamped to 1: the
// point lights can't brighten past white. Alpha: the map's (point) or the vertex's (pointalpha). Fog by D3D's final
// combiner.

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"
#include "include/point_draw.glsl"

layout(constant_id = 1) const bool ALPHA_FROM_VERTEX = false;

layout(set = 2, binding = 0) uniform sampler2D tex0;

layout(location = 0) in vec4 v_d0;
layout(location = 2) in vec4 v_t0;
layout(location = 3) in vec4 v_t1;
layout(location = 4) in vec4 v_t2;
layout(location = 5) in vec4 v_t3;
layout(location = 6) in float v_fog;

layout(location = 0) out vec4 out_color;

// A light's squared distance over its range, from its PASSTHRU stage.
float light_d2(vec4 texcoord)
{
    vec3 d = nv_exp(nv_passthru(texcoord).rgb);
    return nv_clamp(dot(d, d));
}

void main()
{
    vec4 t0 = texture(tex0, stage_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);
    vec4 r0 = t0; // r0.a starts as t0.a

    // stages 0 and 1: squared distances
    float d0 = light_d2(v_t1);
    float d1 = light_d2(v_t2);
    float d2 = light_d2(v_t3);

    // stages 2..5: sum the lights and the vertex colour
    vec4 r1;
    r1.rgb = nv_clamp(nv_inv(d0) * draw.light_color[0].rgb + nv_inv(d1) * draw.light_color[1].rgb);
    r1.rgb = nv_clamp(nv_inv(d2) * draw.light_color[2].rgb + r1.rgb);
    r1.a = 0.0;
    r1 = nv_clamp(r1 + v_d0);

    // stage 6: times the colour map
    r0.rgb = nv_clamp(t0.rgb * r1.rgb);

    // stage 7, pointalpha only
    if (ALPHA_FROM_VERTEX) {
        r0.a = v_d0.a;
    }

    out_color = d3d_finish(r0, v_fog);
}
