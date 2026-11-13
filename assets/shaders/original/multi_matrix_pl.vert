#version 450
#extension GL_GOOGLE_include_directive : require
// multi_matrixPL.xvu, bind row 4 (shader_bind_multi_matrix_pl), with multi_matrix_pl.frag.
// Pass 4, fighter part batches in battle (lit_shader_alt).
//
// Constants (shader_constants_multi_matrix_pl 0xC0F8D), per draw:
//   c96..c99    proj              Projection^T
//   c100..c175  bone_view[19]     (bone x View)^T per bone
//   c176        light             -Light.Direction through View, normalized, w 0
//   c177        k                 (point range, 0, 0.5, 1): the range of the point light nearest the batch's first
//                                 bone (of the stage's six), or D_00280CF8 with none
//   c178        diffuse           Light.Diffuse, w 1
//   c179        ambient           Light.Ambient, w 1
//   c180        point_pos         that point light's world position, w 1 (default D_00280CE8.. with none)
//   c181..c184  view              View^T (the code calls it proj_t)
//   c185        lamp_pos          the nearest lamp (a spot) of the stage, world, w 1; on stages 2 and 3 the lamps of the
//                                 zone the batch is in (shibuya_zone_at)
//   c186        lamp_dir          lamp_colors[n].first: the lamp's direction, its length the lamp's range, w 1
//   c187        zero, never read
//   c188..c191  heading_inverse   (RotationY(actor heading) x View, translation zeroed)^-1, uploaded untransposed
//   PS c1       point_color       the point light's colour, a 0
//   PS c2       lamp_color        lamp_colors[n].second, a 0
// The handler also binds texture slot 0x18 on stage 1 with wrap.
// Inputs: format B, v0 position, v1 normal, v3 uv, v4.x bone x 4.
// Outputs: oPos, oFog, oD0, oT0 = uv, oT1.xy = (Nv.x, Nv.z), oT2.xyz = point term, oT3.xyz = lamp term.
// Computes: as multi_matrix, plus
//   Nh   = the unskinned normal dotted with heading_inverse's rows (untransposed upload, kept), used for both terms
//   d    = Pv - point light in view space; point = (1 - min(|d| / range, 1)) x max(Nh . -d, 0)   (d not normalized)
//   e    = Pv - lamp in view space, a = the lamp direction in view space
//   lamp = (1 - min(|e| / |a|, 1)) x max(cos(e, a), 0) x max(Nh . -e, 0)
//   oFog = clip z x (1 - point) x (1 - lamp): a lit fighter stays out of the fog.
// The two terms reach the pixel side through PASSTHRU stages (clamped 0..1 there).
// Changed: the viewport epilogue is dropped; oT1.zw, oT2.w, oT3.w (never written) are the assumed defaults.

#include "include/d3d.glsl"
#include "include/frame.glsl"

layout(location = 0) in vec4 in_pos;    // v0
layout(location = 1) in vec3 in_normal; // v1
layout(location = 3) in vec2 in_uv;     // v3
layout(location = 4) in float in_bone;  // v4.x

layout(set = 1, binding = 0, std140) uniform MultiMatrixPlDraw
{
    mat4 proj;            // c96..c99
    mat4 bone_view[19];   // c100..c175
    mat4 view;            // c181..c184
    mat4 heading_inverse; // c188..c191, untransposed
    vec4 light;           // c176
    vec4 k;               // c177
    vec4 diffuse;         // c178
    vec4 ambient;         // c179
    vec4 point_pos;       // c180
    vec4 lamp_pos;        // c185
    vec4 lamp_dir;        // c186
    vec4 point_color;     // PS c1
    vec4 lamp_color;      // PS c2
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
    // skin into view space, project
    mat4 bone = draw.bone_view[bone_index(in_bone)];
    vec4 pos_view = bone * in_pos;
    vec3 normal_view = mat3(bone) * in_normal;
    vec4 clip = draw.proj * pos_view;
    gl_Position = clip;

    // the normal the two light terms use: rows of the untransposed heading inverse
    vec3 normal_heading = in_normal * mat3(draw.heading_inverse);

    // point light term
    vec3 point_view = (draw.view * draw.point_pos).xyz;
    vec3 d = pos_view.xyz - point_view;
    float point_ndl = max(dot(normal_heading, -d), draw.k.y);
    float point_fall = draw.k.w - min(length(d) * (1.0 / draw.k.x), draw.k.w);
    float point = point_fall * point_ndl;
    v_t2 = vec4(point, point, point, NV2A_DEFAULT_TEXCOORD.w);

    // lamp (spot) term: falloff over the direction's length, cone by the cosine
    vec3 lamp_view = (draw.view * draw.lamp_pos).xyz;
    vec3 axis = mat3(draw.view) * draw.lamp_dir.xyz;
    vec3 e = pos_view.xyz - lamp_view;
    // a stage without lamps hands a zero direction: RSQ of 0 kept finite so the term is 0, not
    // NaN (the NV2A's own result for it is not checked)
    float inv_axis_len = inversesqrt(max(dot(axis, axis), 1e-30));
    float inv_e_len = inversesqrt(max(dot(e, e), 1e-30));
    float cone = max(dot(e, axis) * inv_e_len * inv_axis_len, draw.k.y);
    float lamp_ndl = max(dot(normal_heading, -e), draw.k.y);
    float lamp_fall = draw.k.w - min((1.0 / inv_e_len) * inv_axis_len, draw.k.w);
    float lamp = lamp_fall * cone * lamp_ndl;
    v_t3 = vec4(lamp, lamp, lamp, NV2A_DEFAULT_TEXCOORD.w);

    // fog thinned by both terms
    v_fog = d3d_fog_factor(clip.z * ((draw.k.w - point) * (draw.k.w - lamp)));

    // light 0 on the skinned normal, as multi_matrix
    float n_dot_l = max(dot(normal_view, draw.light.xyz), draw.k.y);
    v_d0 = clamp(vec4(draw.diffuse.rgb * n_dot_l + draw.ambient.rgb, draw.k.w), 0.0, 1.0);

    // uv and the reflection coordinate
    v_t0 = vec4(in_uv, 0.0, 1.0);
    v_t1 = vec4(normal_view.x, normal_view.z, NV2A_DEFAULT_TEXCOORD.zw);
}
