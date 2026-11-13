#version 450
#extension GL_GOOGLE_include_directive : require
// Fixed-function texture stages of the two screen quads (no pixel shader in the original), with screen_ff.vert.
//
// SHADOW_QUAD false, fullscreen_quad 0xB6E80: stage 0 colour = texture x TFACTOR (MODULATE, arg1 texture, arg2
//   factor), alpha = TFACTOR (SELECTARG2), stages 1..3 off; the texture in texels (DrawState.texel bit 0). The factor
//   is 0xFFFFFFFF for the copies and scene_tint (scene_tint_set, per stage) for the pass 11 composite, which tints the
//   whole 3D frame including pass 0's sprites. No blending, z off, stencil off, fog off, lighting off. Stage 0's alpha
//   kill is not touched, so it keeps what the last mesh pass left (DrawState).
// SHADOW_QUAD true, shadow_screen_quad 0xB6B00 (the end of pass 7): all stages disabled, so the output is the diffuse
//   colour, shadow_color (per stage, ARGB). Pipeline: blend SRC_ALPHA, INV_SRC_ALPHA, z off, stencil test LEQUAL
//   against ref 1 (stencil >= 1: some volume counted the pixel, negative counts wrapped to 255 included) with pass
//   REPLACE, cull CCW, dither on. With shadow_shader set (only ever cleared) it would go through fogshadow.xpu
//   (r0 = v0, r0.a = v0.a x c0.a, c0.a = shadow_shader_alpha); not translated beyond this note.

#include "include/d3d.glsl"
#include "include/finish.glsl"

layout(constant_id = 0) const bool SHADOW_QUAD = false;

layout(set = 2, binding = 0) uniform sampler2D tex0;

layout(location = 0) in vec4 v_d0;
layout(location = 2) in vec4 v_t0;

layout(location = 0) out vec4 out_color;

void main()
{
    if (SHADOW_QUAD) {
        out_color = d3d_finish(v_d0, 1.0);
        return;
    }

    vec4 factor = d3d_color(ds.factor);
    vec4 t0 = texture(tex0, stage_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);
    out_color = d3d_finish(vec4(t0.rgb * factor.rgb, factor.a), 1.0);
}
