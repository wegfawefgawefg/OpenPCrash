#version 450
#extension GL_GOOGLE_include_directive : require
// multi_matrix.xvu, bind row 3 (shader_bind_multi_matrix), with multi_matrix.frag.
// Pass 4, fighter part batches outside battle (pass_draw_fighters; in battle row 4 takes over).
//
// Constants (shader_constants_multi_matrix 0xC1DE2), per draw:
//   c96..c99    proj              Projection^T
//   c100..c175  bone_view[19]     (bone x View)^T per bone of the part batch (PartBatch.bones, up to 19)
//   c180        light             -Light.Direction through View, normalized, w 1
//   c181        k                 (0, 1, 0.5, 0)
//   c182        diffuse           Light.Diffuse, w 1
//   c183        ambient           Light.Ambient, w 1
// The handler also binds texture slot 0x18 on stage 1 with wrap: the reflection map.
// Inputs: format B (0x2C): format A plus v4 at +0x24, v4.x = bone x 4; the float at +0x28 is never read.
// Outputs: oPos, oFog = clip z, oD0, oT0 = uv, oT1.xy.
// Computes: position and normal skinned straight into view space by c[a0 + 100]; colour = diffuse x max(Nv . L, 0) +
// ambient with the skinned normal not normalized; oT1 = (Nv.x, Nv.z), a reflection coordinate.
// Changed: the viewport epilogue is dropped; oT1.zw (never written) are the assumed defaults. The bone palette is a
// mat4 array in the per-draw block instead of constant registers addressed through a0.

#include "include/d3d.glsl"
#include "include/frame.glsl"

layout(location = 0) in vec4 in_pos;    // v0
layout(location = 1) in vec3 in_normal; // v1
layout(location = 3) in vec2 in_uv;     // v3
layout(location = 4) in float in_bone;  // v4.x

layout(set = 1, binding = 0, std140) uniform MultiMatrixDraw
{
    mat4 proj;           // c96..c99
    mat4 bone_view[19];  // c100..c175
    vec4 light;          // c180
    vec4 k;              // c181
    vec4 diffuse;        // c182
    vec4 ambient;        // c183
}
draw;

layout(location = 0) out vec4 v_d0;
layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 6) out float v_fog;

void main()
{
    // skin into view space
    mat4 bone = draw.bone_view[bone_index(in_bone)];
    vec4 pos_view = bone * in_pos;
    vec3 normal_view = mat3(bone) * in_normal;

    // project, fog from clip z
    vec4 clip = draw.proj * pos_view;
    gl_Position = clip;
    v_fog = d3d_fog_factor(clip.z);

    // lit colour
    float n_dot_l = max(dot(normal_view, draw.light.xyz), draw.k.x);
    v_d0 = clamp(vec4(draw.diffuse.rgb * n_dot_l + draw.ambient.rgb, draw.k.y), 0.0, 1.0);

    // uv and the reflection coordinate
    v_t0 = vec4(in_uv, 0.0, 1.0);
    v_t1 = vec4(normal_view.x, normal_view.z, NV2A_DEFAULT_TEXCOORD.zw);
}
