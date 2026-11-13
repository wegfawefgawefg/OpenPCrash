// Conventions shared by every shader in assets/shaders/original/. docs/shaders.md has the whole layout.
//
// Matrices. Every mat4 holds the D3D matrix exactly as the game's C code builds it (row-vector convention, rows in
// memory order), before the transpose the C code does for upload. GLSL reads memory column by column, so `m * v` is
// D3D's `v * M`: the product a program computes when the C code uploaded M^T ("X^T" in the decomp's render.md).
// Where the C code uploaded a matrix untransposed, the program dots v with M's rows instead, which is `v * m` here and
// drops the translation row. Those spots are marked "untransposed upload, kept".
//
// Clip space. Vertex shaders write D3D clip coordinates unchanged (left-handed, y up, z 0..w). The renderer flips y with
// a negative viewport height (Vulkan 1.1) and uses front face = clockwise, so D3D's CULL_CCW is VK_CULL_MODE_BACK_BIT.
// Every original program ended with the NV2A viewport epilogue (mul oPos.xyz, c58; rcc; mad c59); it is dropped and
// gl_Position is the clip position from before it.
//
// Interpolant locations, the same in every shader so vertex and fragment stages from different rows can be paired:
//   0 v_d0  oD0, diffuse colour, clamped to 0..1 by the vertex stage as NV2A does
//   1 v_d1  oD1, specular colour (fixed function only), clamped to 0..1
//   2 v_t0  oT0      3 v_t1  oT1      4 v_t2  oT2      5 v_t3  oT3
//   6 v_fog the fog factor (1 = no fog), computed per vertex from oFog, see fog.glsl
// An output the original program never writes is written as NV2A_DEFAULT_TEXCOORD / zero colour here (assumed values,
// unverified on hardware), so a fragment stage reading it sees what the original would have.

#ifndef D3D_GLSL
#define D3D_GLSL

// Assumed value of an oTn register (or component) the vertex program never writes. Unverified: check in xemu.
const vec4 NV2A_DEFAULT_TEXCOORD = vec4(0.0, 0.0, 0.0, 1.0);

// Assumed oFog of a program that never writes it (rainbow, reflect, particle, spritemat): 0, which the exp and linear
// tables turn into "no fog". Unverified.
const float NV2A_DEFAULT_FOG_COORD = 0.0;

// Unpack a D3DCOLOR (0xAARRGGBB) handed over as a uint.
vec4 d3d_color(uint argb)
{
    return vec4(float((argb >> 16) & 0xFFu), float((argb >> 8) & 0xFFu), float(argb & 0xFFu), float(argb >> 24)) /
           255.0;
}

// ARL: a0.x = floor(x + 0.001). The skinned programs read c[a0 + base] with a0 = bone index x 4.
int nv2a_arl(float x)
{
    return int(floor(x + 0.001));
}

// The bone a skinned vertex uses: its selector v4.x is bone x 4 (part_batch_build), each bone four registers.
int bone_index(float selector)
{
    return nv2a_arl(selector) / 4;
}

#endif
