#version 450
#extension GL_GOOGLE_include_directive : require
// shimmer.xpu, bind row 0xD (shader_bind_shimmer), with screen_quad.vert. DEAD in retail: its only caller,
// screen_effect_scroll 0xB65E0, runs only while shimmer_on is set, and nothing writes shimmer_on. Translated for
// completeness; the renderer never needs it.
//
// What screen_effect_scroll binds (after the same two downsamples as the dof path): stage 0 the scene 0x223, stage 1
// no texture, stage 2 the 160x120 blur, stage 3 still the 320x240 blur from the downsample; quad +0x240, whose fourth
// uv scrolls by shimmer_scroll (+0.002 a frame). PS c0 = scene_tint, c1 = 0. shader_constants_shimmer is empty.
// Texture modes: t0..t3 project2d.
// Combiners:
//   stage 0  r1 = t1 * t3
//   stage 1  r0 = u(r1) * t2 + t0 * inv(r1)      lerp(scene, blur, t1 x t3)
//   stage 2  r0 = r0 * c0
//   stage 3  r0 = r0 + c1
// With nothing on stage 1, t1 is whatever the NV2A returns for an empty stage (assumed 0 here, which leaves the scene).

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"
#include "include/screen_draw.glsl"

layout(set = 2, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 2) uniform sampler2D tex2;
layout(set = 2, binding = 3) uniform sampler2D tex3;

layout(location = 2) in vec4 v_t0;
layout(location = 4) in vec4 v_t2;
layout(location = 5) in vec4 v_t3;

layout(location = 0) out vec4 out_color;

const vec4 EMPTY_STAGE = vec4(0.0); // assumed

void main()
{
    vec4 t0 = texture(tex0, screen_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);
    vec4 t1 = EMPTY_STAGE;
    vec4 t2 = texture(tex2, screen_uv(2, v_t2.xy));
    vec4 t3 = texture(tex3, screen_uv(3, v_t3.xy));

    vec4 r1 = nv_clamp(t1 * t3);
    vec4 r0;
    r0.rgb = nv_clamp(nv_u(r1.rgb) * t2.rgb + t0.rgb * nv_inv(r1.rgb));
    r0.a = nv_clamp(nv_u(r1.a) * t2.a + t0.a * nv_inv(r1.a));
    r0 = nv_clamp(r0 * draw.c0);
    r0 = nv_clamp(r0 + draw.c1);

    out_color = d3d_finish(r0, 1.0);
}
