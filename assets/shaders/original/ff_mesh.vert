#version 450
#extension GL_GOOGLE_include_directive : require
// Fixed-function vertex path, FVF 0x152 (no program in the original: SetVertexShader(0x152)).
//
// Used by: lit meshes outside battle and every lit mesh the point shaders don't take (pass_draw_meshes "lit" arm), mesh
// flag 0x200 meshes, pass 11 instruments without a texture and uv-override quads (mesh 0x800000), row 22 vision_eff
// (mesh 0x200000, with vision_eff.frag), the result trophies of pass 13 (light 1), and the static shadow volumes of
// pass 7 (position only, no fragment stage).
//
// What D3D8 computes here, with the states the game leaves (render_device_setup and render_cache_init):
//   - position: P x WORLD x VIEW x PROJECTION.
//   - lighting (LIGHTING on, DS_LIGHTING): one directional light (light 0, or light 1 for pass 13) in view space,
//     NORMALIZENORMALS on (set at device setup), LOCALVIEWER on (D3D default), COLORVERTEX on with the default material
//     sources: diffuse from the vertex colour, ambient from the material (= the sub-mesh colour), specular from the
//     material (the vertex has none, so the material's 1), emissive 0, global AMBIENT 0.
//       diffuse  = Ma x La + Cv x Ld x max(N.L, 0), alpha = vertex alpha, clamped 0..1
//       specular = Ls x max(N.H, 0)^power, only where N.L > 0 (D3D reference behaviour, assumed), clamped 0..1
//   - LIGHTING off: diffuse = the vertex colour, specular = 0.
//   - fog coordinate: view-space depth (clip w) through the table mode (assumed, see frame.glsl).
//   - texture coordinates: TEXCOORDINDEX(s) = s, but the vertex has one set, so stages 1..3 get the default texcoord
//     (assumed (0, 0, 0, 1): such a stage samples one texel). Unverified; matters only for fixed-function sub-meshes with
//     more than one texture.
//
// Changed: nothing beyond writing it out; the viewport transform is the renderer's.
//
// Vertex format A (0x24): pos float3 @0, normal float3 @12, colour D3DCOLOR @24 (B8G8R8A8_UNORM), uv float2 @28.

#include "include/d3d.glsl"
#include "include/draw_state.glsl"

layout(location = 0) in vec4 in_pos;    // v0, w = 1 from the vertex fetch
layout(location = 1) in vec3 in_normal; // v1
layout(location = 2) in vec4 in_color;  // v2
layout(location = 3) in vec2 in_uv;     // v3

layout(set = 1, binding = 0, std140) uniform FixedDraw
{
    mat4 world;            // SetTransform(6): Mesh matrix (pass 9 mesh 0x200: the camera-facing billboard matrix)
    mat4 view;             // pass_views[view][pass]
    mat4 proj;             // PerspectiveFovLH(fov, w / h, 0.1, 10000) of the view's rectangle
    vec4 light_dir;        // D3DLIGHT8.Direction, world space (the way the light travels)
    vec4 light_diffuse;    // D3DLIGHT8.Diffuse
    vec4 light_ambient;    // D3DLIGHT8.Ambient
    vec4 light_specular;   // D3DLIGHT8.Specular
    vec4 material_ambient; // D3DMATERIAL8.Ambient: SubMesh.color (pass_draw_meshes), 1 for screen quads
    vec4 material;         // x D3DMATERIAL8.Power (SubMesh.power), yzw unused
}
draw;

layout(location = 0) out vec4 v_d0;
layout(location = 1) out vec4 v_d1;
layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 4) out vec4 v_t2;
layout(location = 5) out vec4 v_t3;
layout(location = 6) out float v_fog;

void main()
{
    // transform to view space and clip space
    mat4 world_view = draw.view * draw.world;
    vec4 pos_view = world_view * in_pos;
    vec4 clip = draw.proj * pos_view;
    gl_Position = clip;

    // texture coordinates: one set, stages 1..3 read a set the vertex doesn't have
    v_t0 = vec4(in_uv, 0.0, 1.0);
    v_t1 = NV2A_DEFAULT_TEXCOORD;
    v_t2 = NV2A_DEFAULT_TEXCOORD;
    v_t3 = NV2A_DEFAULT_TEXCOORD;

    // fog from eye depth
    v_fog = d3d_fog_factor(clip.w);

    // unlit: the vertex colour as is
    if (!ds_on(DS_LIGHTING)) {
        v_d0 = clamp(in_color, 0.0, 1.0);
        v_d1 = vec4(0.0);
        return;
    }

    // lit: light 0 in view space, normals by the inverse transpose of world x view, normalized
    vec3 n = normalize(inverse(transpose(mat3(world_view))) * in_normal);
    vec3 l = normalize(-(mat3(draw.view) * draw.light_dir.xyz));
    float n_dot_l = dot(n, l);
    vec3 diffuse = draw.material_ambient.rgb * draw.light_ambient.rgb +
                   in_color.rgb * draw.light_diffuse.rgb * max(n_dot_l, 0.0);
    v_d0 = clamp(vec4(diffuse, in_color.a), 0.0, 1.0);

    // specular with a local viewer, only on the lit side (power 0 taken as x^0 = 1, D3D's handling not checked)
    vec3 specular = vec3(0.0);
    if (n_dot_l > 0.0) {
        vec3 h = normalize(normalize(-pos_view.xyz) + l);
        float n_dot_h = max(dot(n, h), 0.0);
        float highlight = (draw.material.x > 0.0) ? pow(n_dot_h, draw.material.x) : 1.0;
        specular = draw.light_specular.rgb * highlight;
    }
    v_d1 = clamp(vec4(specular, 0.0), 0.0, 1.0);
}
