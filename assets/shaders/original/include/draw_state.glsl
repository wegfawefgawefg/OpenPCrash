// Per-draw render state the original leaves to D3D (push constants, 16 bytes). finish.glsl applies the fragment part
// (alpha kill, alpha test, fog). Blending, z, stencil, cull and fill stay pipeline state.
//
// Which passes set what (decomp docs/port/render.md, "Mesh drawing"):
//   DS_FOG          FOGENABLE: passes 2 and 8 unless mesh 0x2000000, passes 4, 5, 6; off elsewhere.
//   DS_ALPHA_KILL   stage 0 ALPHAKILL (texture stage state 11 = 4): meshes unless mesh 0x40000, overlay sprites; off for
//                   fighters (pass_open_fighters), the heat-haze composite. Kills pixels whose stage 0 texel alpha is 0.
//   DS_ALPHA_TEST   ALPHATESTENABLE with ALPHAFUNC GEQUAL (set at device setup, the only function used outside
//                   billboards_draw's ALWAYS) against DS_ALPHA_REF: 0x80 for cutouts (sub 0x4000, mesh 0x400), 0 else.
//   DS_SPECULAR     SPECULARENABLE: sub 0x40 unless mesh 0x8000000 (fixed function adds oD1 after the stages).
//   DS_LIGHTING     LIGHTING for the fixed-function vertex path: on unless sub 0x100, pass 11 or wireframe; in pass 9
//                   only with mesh 0x10000.
// stages holds the fixed-function texture stage setup, 8 bits a stage (ff_mesh.frag); factor is TEXTUREFACTOR.

#ifndef DRAW_STATE_GLSL
#define DRAW_STATE_GLSL

#include "frame.glsl"

layout(push_constant, std430) uniform DrawState
{
    uint flags;   // DS_* bits, alpha ref in bits 8..15
    uint stages;  // fixed-function stage ops, stage s in bits 8s..8s+7 (ff_mesh.frag)
    uint factor;  // TEXTUREFACTOR as D3DCOLOR 0xAARRGGBB
    uint texel;   // bit s: stage s samples a linear target with texel coordinates (divide by Frame.linear_size)
}
ds;

const uint DS_FOG = 0x1u;
const uint DS_ALPHA_KILL = 0x2u;
const uint DS_ALPHA_TEST = 0x4u;
const uint DS_SPECULAR = 0x8u;
const uint DS_LIGHTING = 0x10u;

bool ds_on(uint bit)
{
    return (ds.flags & bit) != 0u;
}

// A stage's texture coordinate: the Xbox addresses linear (render target) textures in texels.
vec2 stage_uv(int s, vec2 uv)
{
    if (((ds.texel >> uint(s)) & 1u) != 0u) {
        return uv * frame.linear_size.zw;
    }
    return uv;
}

#endif
