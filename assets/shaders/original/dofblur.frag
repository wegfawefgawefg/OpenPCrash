#version 450
#extension GL_GOOGLE_include_directive : require
// dofblur.xpu, bind row 0x1C (shader_bind_dof_blur), with screen_quad.vert (DOF_XVU: the second loaded copy of
// dof.xvu): the battle composite while heat_wave_on is set (per stage, table 0x203760), quad 0x25B500. PS constants
// and the bump matrix: include/screen_draw.glsl.
//
// Texture modes:
//   t0 project2d    the wave texture (one of two 64x64 V8U8 textures rewritten every composite by wave_texture_update,
//                   swizzled, 0..1 coordinates, clamp)
//   t1 bumpenvmap   (input t0) the 160x120 blur at oT1 + M x du/dv
//   t2 bumpenvmap   (input t0) the scene 0x223 at oT2 + M x du/dv
//   t3 project2d    the depth buffer read as colour, at oT3
// The bump offsets are added to texel coordinates, so they move the lookups by up to (1 + k) texels of each target
// (xemu adds them before scaling rect textures the same way; assumed).
// Combiners: dof.frag's, with t3 for the depth, t1 for the blur and t2 for the scene:
//   stage 0 r1 = t3.a - c2.a; stages 1-5 doubled; stage 6 r0 = u(r1) * t1 + t2 * inv(r1); stage 7 r0 = r0 * c0 + c1.
// dof.xvu writes no oT3, so the depth lookup reads one texel at the default texcoord: with the assumed (0, 0, 0, 1) the
// top-left texel of the depth buffer, outside the 3D safe rectangle and so the cleared depth 1.0 unless something
// covered it. That makes the whole screen take one blend factor. DEPTH_FROM_DEFAULT_TEXCOORD = false reads the depth at
// the scene's unbumped coordinate instead (the quad's fourth uv holds the same values: the evident intent); true is the
// original as written. What real hardware gives is unknown.
// Fog off. The composite sets stage 0's alpha kill off for this draw.

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"
#include "include/screen_draw.glsl"
#include "include/depth_byte.glsl"

layout(constant_id = 1) const bool DEPTH_FROM_DEFAULT_TEXCOORD = true;

layout(set = 2, binding = 0) uniform sampler2D tex0; // wave, R8G8_SNORM
layout(set = 2, binding = 1) uniform sampler2D tex1; // blur160
layout(set = 2, binding = 2) uniform sampler2D tex2; // scene 0x223
layout(set = 2, binding = 3) uniform sampler2D tex3; // depth 0x224, depth aspect

layout(location = 2) in vec4 v_t0;
layout(location = 3) in vec4 v_t1;
layout(location = 4) in vec4 v_t2;
layout(location = 5) in vec4 v_t3;

layout(location = 0) out vec4 out_color;

void main()
{
    // the wave's du/dv offsets the blur and scene lookups
    vec4 t0 = texture(tex0, screen_uv(0, v_t0.xy));
    vec2 dudv = nv_dudv(t0, true);
    vec4 t1 = texture(tex1, screen_uv(1, nv_bumpenv(v_t1.xy, dudv, draw.bump_env)));
    vec4 t2 = texture(tex2, screen_uv(2, nv_bumpenv(v_t2.xy, dudv, draw.bump_env)));
    vec2 depth_texel = v_t3.xy;
    if (!DEPTH_FROM_DEFAULT_TEXCOORD) {
        depth_texel = v_t2.xy;
    }
    float depth_byte = depth_alpha(tex3, screen_uv(3, depth_texel));

    // stages 0..5: the depth step amplified 32 times
    float r1 = nv_clamp(depth_byte - draw.c2.a);
    for (int i = 0; i < 5; i++) {
        r1 = nv_clamp(r1 + r1);
    }

    // stage 6: scene to blur; stage 7: tint
    vec4 r0;
    r0.rgb = nv_clamp(nv_u(r1) * t1.rgb + t2.rgb * nv_inv(r1));
    r0.a = nv_clamp(nv_u(r1) * t1.a + t2.a * nv_inv(r1));
    r0 = nv_clamp(r0 * draw.c0 + draw.c1);

    out_color = d3d_finish(r0, 1.0);
}
