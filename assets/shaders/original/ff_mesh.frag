#version 450
#extension GL_GOOGLE_include_directive : require
// Fixed-function pixel path: D3D8 texture stages 0..3 (no pixel shader in the original, SetPixelShader(0)).
//
// Pairs with ff_mesh.vert, fog.vert (row 0x17, pixel "fixed") and cockpit_uv.vert (row 0x18, pixel "fixed").
//
// The stage setup pass_draw_meshes 0xB31C0 writes per sub-mesh, encoded in DrawState.stages, 8 bits a stage:
//   bits 0-1 colour op   0 DISABLE (this stage and all later ones off)
//                        1 MODULATE texture x current      (textured stage, SubMesh.layer[s] bit 1 clear)
//                        2 ADD texture + current           (textured stage, layer bit 1 set)
//                        3 MODULATE current x TFACTOR      (first untextured stage of a mesh with flag 0x20)
//   bits 2-3 alpha op    0 current (SELECTARG2 current; sub 0x200 on the factor stage: SELECTARG1 current)
//                        1 SELECTARG1 texture              (stage 0, layer bit 0x40, mesh not 0x40000 or 0x40)
//                        2 MODULATE texture x current      (stage > 0, layer bit 0x40, mesh not 0x40000 or 0x40)
//                        3 MODULATE current x TFACTOR      (factor stage without sub 0x200)
//   Mesh 0x40 (alpha from the vertex) and layers without 0x40 select current. "current" at stage 0 is the diffuse
//   colour. A stage index >= 0x226 stops the loop and leaves that stage's state from the previous draw in the original;
//   the port treats it as DISABLE (whether any data relies on the stale state was not checked).
// Stage results clamp to 0..1 (an ADD of two colours saturates). With SPECULARENABLE (DS_SPECULAR) D3D's final combiner
// adds the specular colour after the stages; then fog (DS_FOG), alpha test, alpha kill on stage 0.
// Overrides the renderer folds into the stage setup: mesh 0x80000 puts the scene target 0x223 on stage 0 with clamp and
// texel coordinates (DrawState.texel bit 0); mesh 0x400000 binds yurayuraTest/wave02 on stages 0 and 1 for the seabump
// pixel shader (seabump.frag), not this path.
// Filters (linear, linear mips) and address modes (U: layer 0x10 wrap, 4 mirror, else clamp; V: 0x20, 8) are sampler
// state on the bound textures.

#include "include/d3d.glsl"
#include "include/finish.glsl"

layout(set = 2, binding = 0) uniform sampler2D tex0;
layout(set = 2, binding = 1) uniform sampler2D tex1;
layout(set = 2, binding = 2) uniform sampler2D tex2;
layout(set = 2, binding = 3) uniform sampler2D tex3;

layout(location = 0) in vec4 v_d0;
layout(location = 1) in vec4 v_d1;
layout(location = 2) in vec4 v_t0;
layout(location = 3) in vec4 v_t1;
layout(location = 4) in vec4 v_t2;
layout(location = 5) in vec4 v_t3;
layout(location = 6) in float v_fog;

layout(location = 0) out vec4 out_color;

const uint COLOR_DISABLE = 0u;
const uint COLOR_MODULATE = 1u;
const uint COLOR_ADD = 2u;
const uint COLOR_FACTOR = 3u;
const uint ALPHA_CURRENT = 0u;
const uint ALPHA_TEXTURE = 1u;
const uint ALPHA_MODULATE = 2u;
const uint ALPHA_FACTOR = 3u;

vec4 stage_texel(int s)
{
    if (s == 0) {
        return texture(tex0, stage_uv(0, v_t0.xy));
    }
    if (s == 1) {
        return texture(tex1, stage_uv(1, v_t1.xy));
    }
    if (s == 2) {
        return texture(tex2, stage_uv(2, v_t2.xy));
    }
    return texture(tex3, stage_uv(3, v_t3.xy));
}

void main()
{
    vec4 factor = d3d_color(ds.factor);
    vec4 current = v_d0;

    // run the stages until one is disabled
    for (int s = 0; s < 4; s++) {
        uint op = (ds.stages >> uint(8 * s)) & 0xFFu;
        uint color_op = op & 3u;
        uint alpha_op = (op >> 2) & 3u;
        if (color_op == COLOR_DISABLE) {
            break;
        }

        // only the textured ops fetch
        vec4 texel = vec4(1.0);
        if (color_op != COLOR_FACTOR) {
            texel = stage_texel(s);
            if (s == 0) {
                d3d_alpha_kill(texel);
            }
        }

        // colour
        vec3 rgb = current.rgb;
        if (color_op == COLOR_MODULATE) {
            rgb = texel.rgb * current.rgb;
        } else if (color_op == COLOR_ADD) {
            rgb = texel.rgb + current.rgb;
        } else if (color_op == COLOR_FACTOR) {
            rgb = current.rgb * factor.rgb;
        }

        // alpha
        float a = current.a;
        if (alpha_op == ALPHA_TEXTURE) {
            a = texel.a;
        } else if (alpha_op == ALPHA_MODULATE) {
            a = texel.a * current.a;
        } else if (alpha_op == ALPHA_FACTOR) {
            a = current.a * factor.a;
        }

        current = clamp(vec4(rgb, a), 0.0, 1.0);
    }

    // specular after the stages
    if (ds_on(DS_SPECULAR)) {
        current.rgb = clamp(current.rgb + v_d1.rgb, 0.0, 1.0);
    }

    out_color = d3d_finish(current, v_fog);
}
