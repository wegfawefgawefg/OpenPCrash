#version 450
#extension GL_GOOGLE_include_directive : require
// vision_eff.xpu, bind row 0x16 (shader_bind_vision_eff): vertex side fixed function FVF 0x152 (ff_mesh.vert).
// Meshes with flag 0x200000. The row sets no constants (it has no handler), and this program reads none.
//
// Texture modes: t0 project2d (the sub-mesh's stage 0 texture), t1..t3 none.
// Combiners:
//   stage 0  r0.rgb = v0.rgb
//   stage 1  r0.a   = t0.a * v0.a
// So the texture only shapes alpha: the vertex (or lit) colour, cut out by the texture's alpha.
// v0 is whatever ff_mesh.vert computes for the sub-mesh's LIGHTING state. Then D3D's final combiner (fog).

#include "include/d3d.glsl"
#include "include/finish.glsl"
#include "include/nv2a.glsl"

layout(set = 2, binding = 0) uniform sampler2D tex0;

layout(location = 0) in vec4 v_d0;
layout(location = 2) in vec4 v_t0;
layout(location = 6) in float v_fog;

layout(location = 0) out vec4 out_color;

void main()
{
    vec4 t0 = texture(tex0, stage_uv(0, v_t0.xy));
    d3d_alpha_kill(t0);

    // stage 0: colour from v0; stage 1: alpha = t0.a x v0.a
    vec4 r0;
    r0.rgb = v_d0.rgb;
    r0.a = nv_clamp(t0.a * v_d0.a);
    out_color = d3d_finish(r0, v_fog);
}
