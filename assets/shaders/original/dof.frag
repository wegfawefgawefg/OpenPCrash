#version 450
#extension GL_GOOGLE_include_directive : require
// dof.xpu, bind row 8 (shader_bind_dof), with screen_quad.vert (DOF_XVU): the battle composite of the scene to the
// final target while heat_wave_on is clear (screen_effect_dof 0xB5F30, quad 0x25B440). PS constants: see
// include/screen_draw.glsl (c0 scene_tint, c1 0, c2.a the depth threshold).
//
// Texture modes:
//   t0 project2d   the 160x120 blur (blur160_texture)
//   t1 project2d   the scene 0x223
//   t2 project2d   the depth buffer 0x224 read as A8R8G8B8: alpha = the top byte of the depth (include/depth_byte.glsl)
// Combiners (c0 of stage 0 is mapped to PS c2; of stage 7 to PS c0, c1 to PS c1):
//   stage 0     r1 = t2.a - c2.a
//   stages 1-5  r1 = r1 + r1                 clamped to -1..1 each time: clamp(32 x (depth byte - threshold), -1, 1)
//   stage 6     r0 = u(r1) * t0 + t1 * inv(r1)   lerp(scene, blur, clamp01(r1))
//   stage 7     r0 = r0 * c0 + c1             times the tint
// So: everything whose depth byte is past the threshold blurs, with a ramp 1/32 of a depth step wide: a far-distance
// blur, not a lens model. The hardware does this in fixed point (about 9 bits), which matters at that 32x gain.
// Fog off. Stage 0's alpha kill is inherited from the last mesh pass (DrawState); see docs/shaders.md.

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"
#include "include/screen_draw.glsl"
#include "include/depth_byte.glsl"

layout(set = 2, binding = 0) uniform sampler2D tex0; // blur160
layout(set = 2, binding = 1) uniform sampler2D tex1; // scene 0x223
layout(set = 2, binding = 2) uniform sampler2D tex2; // depth 0x224, depth aspect

layout(location = 2) in vec4 v_t0;
layout(location = 3) in vec4 v_t1;
layout(location = 4) in vec4 v_t2;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 t0 = texture(tex0, screen_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);
    vec4 t1 = texture(tex1, screen_uv(1, v_t1.xy));
    float depth_byte = depth_alpha(tex2, screen_uv(2, v_t2.xy));

    // stages 0..5: the depth step amplified 32 times
    float r1 = nv_clamp(depth_byte - draw.c2.a);
    for (int i = 0; i < 5; i++) {
        r1 = nv_clamp(r1 + r1);
    }

    // stage 6: scene to blur; stage 7: tint
    vec4 r0;
    r0.rgb = nv_clamp(nv_u(r1) * t0.rgb + t1.rgb * nv_inv(r1));
    r0.a = nv_clamp(nv_u(r1) * t0.a + t1.a * nv_inv(r1));
    r0 = nv_clamp(r0 * draw.c0 + draw.c1);

    out_color = d3d_finish(r0, 1.0);
}
