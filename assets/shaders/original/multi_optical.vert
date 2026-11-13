#version 450
#extension GL_GOOGLE_include_directive : require
// multi_optical.xvu, bind row 5 (shader_bind_multi_optical, bound by pass_open_camo), with blend.frag.
// Pass 5, fighter part batches while camouflaged (camo states 1..8), drawn over the copy of the backdrop.
//
// Constants (shader_constants_multi_optical 0xC215B):
//   c96..c99    proj              Projection^T
//   c100        k                 (0.5, optical k, 0, 1); optical k from optical_shader_k_set, default 0.25
//   c101        view_size         (viewport width / 2, -height / 2, height / 2, 0), 640x480 pixels
//   c102        diffuse           Light.Diffuse, w 1
//   c103        light             -Light.Direction, normalized, world space, w 1
//   c104        sphere            the reveal sphere: the vec4 at 0xA94520[Mesh.texture_slot] (texture_slot_vec4_set),
//                                 world centre and radius R
//   c105        fog_k             (R + 4, 1, 0.25, 0)
//   c106        view_origin       (viewport x, viewport y, 0, 0)
//   c107..c110  view              View^T
//   c111        ambient           Light.Ambient, w 1
//   c112..c187  bones[19]         bone^T per bone: bone to world
// The handler also binds texture slot 0x18 on stage 3 with wrap; pass_open_camo binds the backdrop 0x222 on stage 1
// (point filtered, clamp).
// Inputs: format B, v0 position, v1 normal, v3 uv, v4.x bone x 4.
// Outputs: oPos, oFog, oD0, oT0.xy = uv, oT1.xy = backdrop texel, oT2.w = s, oT3.xy = (Nv.x, Nv.z).
// Computes: skinning in world space; distance from the reveal sphere centre;
//   s = clamp01(R - distance)                    1 inside the sphere: the fighter shows there
//   t = clamp(0.25 x (R + 4 - distance), k, 1)   how far the backdrop lookup is pushed
//   oT1 = the clip position pushed along the view-space normal (through the projection's x and y columns) by t,
//         divided by w and mapped to backdrop texels: x_ndc x W/2 + W/2 + X, y_ndc x -H/2 + H/2 + Y
//   oD0 = diffuse x max(Nw . light, 0) + ambient + (1 - s), alpha 1: white (after the clamp) wherever s = 0
//   oFog = clip z x s
// Changed: the viewport epilogue is dropped; texel coordinates are divided by the target size in blend.frag;
// unwritten oT0.zw, oT1.zw, oT2.xyz and oT3.zw are the assumed defaults.

#include "include/d3d.glsl"
#include "include/frame.glsl"

layout(location = 0) in vec4 in_pos;    // v0
layout(location = 1) in vec3 in_normal; // v1
layout(location = 3) in vec2 in_uv;     // v3
layout(location = 4) in float in_bone;  // v4.x

layout(set = 1, binding = 0, std140) uniform MultiOpticalDraw
{
    mat4 proj;        // c96..c99
    mat4 view;        // c107..c110
    mat4 bones[19];   // c112..c187
    vec4 k;           // c100
    vec4 view_size;   // c101
    vec4 diffuse;     // c102
    vec4 light;       // c103
    vec4 sphere;      // c104
    vec4 fog_k;       // c105
    vec4 view_origin; // c106
    vec4 ambient;     // c111
}
draw;

layout(location = 0) out vec4 v_d0;
layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 4) out vec4 v_t2;
layout(location = 5) out vec4 v_t3;
layout(location = 6) out float v_fog;

void main()
{
    // skin into world space, then view and clip
    mat4 bone = draw.bones[bone_index(in_bone)];
    vec4 pos_world = bone * in_pos;
    vec4 clip = draw.proj * (draw.view * pos_world);
    gl_Position = clip;

    // reveal sphere: s shows the fighter, t pushes the lookup
    float dist = length(draw.sphere.xyz - pos_world.xyz);
    float s = min(max((draw.sphere.w - dist) * draw.fog_k.y, draw.k.z), draw.k.w);
    float t = min(max((draw.fog_k.x - dist) * draw.fog_k.z, draw.k.y), draw.k.w);
    v_t2 = vec4(NV2A_DEFAULT_TEXCOORD.xyz, s);
    v_fog = d3d_fog_factor(clip.z * s);

    // normals: world, view, and the view normal through the projection's x and y
    vec3 normal_world = mat3(bone) * in_normal;
    vec3 normal_view = mat3(draw.view) * normal_world;
    vec2 normal_clip = (mat3(draw.proj) * normal_view).xy;

    // backdrop texel: the displaced clip position through the viewport transform
    vec2 pushed = normal_clip * t + clip.xy;
    vec2 ndc = pushed * (1.0 / clip.w);
    vec2 texel = ndc * draw.view_size.xy + draw.view_size.xz + draw.view_origin.xy;
    v_t1 = vec4(texel, NV2A_DEFAULT_TEXCOORD.zw);

    // shaded colour, saturated outside the sphere
    float n_dot_l = max(dot(normal_world, draw.light.xyz), draw.k.z);
    vec3 color = draw.diffuse.rgb * n_dot_l + draw.ambient.rgb + (draw.k.w - s);
    v_d0 = clamp(vec4(color, draw.k.w), 0.0, 1.0);

    // uv and the reflection coordinate (view x and z of the world normal)
    v_t0 = vec4(in_uv, NV2A_DEFAULT_TEXCOORD.zw);
    v_t3 = vec4(normal_view.x, normal_view.z, NV2A_DEFAULT_TEXCOORD.zw);
}
