#version 450
#extension GL_GOOGLE_include_directive : require
// particle.xpu (row 0xE, with particle.vert) and spritemat.xpu (row 0xF, with spritemat.vert): the two files hold the
// same texture modes and combiners. No PS constants.
//
// Texture modes: t0 project2d (the atlas, or the sprite's texture), t1 passthru (oT1 = the billboard's w flag or the
// sprite's k, clamped 0..1), t2, t3 none.
// Combiners:
//   stage 0  r0.rgb = v0.rgb * t0.rgb       r0.a = v0.a * t0.a
//   stage 1  r0.rgb = r0.rgb * r0.a         r0.a = r0.a * t1.a
// So: colour premultiplied by alpha, alpha times the flag. Both draw with SRC = ONE, DEST = INV_SRC_ALPHA, so flag 1
// is ordinary alpha blending and flag 0 is additive (billboard_push stores w = 0 when its flag argument is set; a
// sprite's k is 0 with its flag 1).
// States: z test on and no z write for particles, z off for sprites; fog off; sprites have stage 0's alpha kill on
// (pass_open_overlay), particles inherit it; alpha test ALWAYS for particles. Sprites with flag 2 or 0x1000 (the scene
// 0x223) take texel coordinates (DrawState.texel bit 0); filtering is point unless sprite flag 0x10 (sampler state).

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"

layout(set = 2, binding = 0) uniform sampler2D tex0;

layout(location = 0) in vec4 v_d0;
layout(location = 2) in vec4 v_t0;
layout(location = 3) in vec4 v_t1;
layout(location = 6) in float v_fog;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 t0 = texture(tex0, stage_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);
    vec4 t1 = nv_passthru(v_t1);

    vec4 r0 = nv_clamp(v_d0 * t0);
    r0.rgb = nv_clamp(r0.rgb * r0.a);
    r0.a = nv_clamp(r0.a * t1.a);

    out_color = d3d_finish(r0, v_fog);
}
