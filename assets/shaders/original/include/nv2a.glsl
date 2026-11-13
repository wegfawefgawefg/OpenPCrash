// NV2A pixel arithmetic the .xpu programs rely on (decomp docs/port/render.md, "NV2A details"), written once so the
// fragment shaders can follow the combiner stages line by line.
//
// - Every combiner register write is clamped to -1..1 (nv_clamp). The hardware is fixed point (about 9 bits signed);
//   floats here are more precise, which matters only for dof's 32x amplification of a depth byte.
// - Input mappings: UNSIGNED_IDENTITY max(x, 0), UNSIGNED_INVERT 1 - clamp01(x), EXPAND_NORMAL 2 max(x, 0) - 1.
// - r0.a starts as t0.a (1 when stage 0 has no texture). Texture registers hold the fetched texel, 0..1.
// - PASSTHRU stages hand clamp01(texcoord) to the combiners instead of sampling.
// - DOTPRODUCT / DOT_ST dot a texcoord with a texel mapped by the dot map (ZERO_TO_ONE as stored, MINUS1_TO_1_D3D
//   (255x - 128) / 127) and DOT_ST fetches at the two dots.
// - BUMPENVMAP adds the 2x2 bump matrix (stage states 22, 23, 25, 24 = M00, M01, M10, M11) times the signed du/dv texel
//   to the stage's own coordinate.
// - Combiner constants are 8 bits a channel: the renderer uploads PS constants already quantized (nv_q8), as the D3D
//   runtime packs them into D3DCOLORs. A constant the game never sets keeps the value stored in the .xpu file; those
//   are written as literals in the shaders.

#ifndef NV2A_GLSL
#define NV2A_GLSL

float nv_clamp(float x)
{
    return clamp(x, -1.0, 1.0);
}
vec3 nv_clamp(vec3 x)
{
    return clamp(x, -1.0, 1.0);
}
vec4 nv_clamp(vec4 x)
{
    return clamp(x, -1.0, 1.0);
}

// UNSIGNED_IDENTITY
float nv_u(float x)
{
    return max(x, 0.0);
}
vec3 nv_u(vec3 x)
{
    return max(x, 0.0);
}

// UNSIGNED_INVERT
float nv_inv(float x)
{
    return 1.0 - clamp(x, 0.0, 1.0);
}
vec3 nv_inv(vec3 x)
{
    return 1.0 - clamp(x, 0.0, 1.0);
}

// EXPAND_NORMAL
vec3 nv_exp(vec3 x)
{
    return 2.0 * max(x, 0.0) - 1.0;
}

// PASSTHRU texture stage
vec4 nv_passthru(vec4 texcoord)
{
    return clamp(texcoord, 0.0, 1.0);
}

// MINUS1_TO_1_D3D dot map
vec3 nv_dotmap_d3d(vec3 texel)
{
    return (255.0 * texel - 128.0) / 127.0;
}

// A combiner constant as the hardware holds it, for constants computed in a shader rather than uploaded.
vec4 nv_q8(vec4 c)
{
    return floor(clamp(c, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}

// BUMPENVMAP: coordinate + M x (du, dv). m = (M00, M01, M10, M11).
vec2 nv_bumpenv(vec2 uv, vec2 dudv, vec4 m)
{
    return vec2(uv.x + m.x * dudv.x + m.z * dudv.y, uv.y + m.y * dudv.x + m.w * dudv.y);
}

// du/dv from a bump texel. dudv_signed: the texture is V8U8 (the heat-haze wave textures), viewed as R8G8_SNORM, so
// r = du (low byte) and g = dv. Otherwise (yurayuraTest.bmp loads as an ordinary unsigned ARGB texture) what the NV2A
// makes of it is not known: taking the two low bytes (blue, green) expanded 2x - 1 is a guess to check against xemu or
// hardware before the water is judged.
vec2 nv_dudv(vec4 texel, bool dudv_signed)
{
    if (dudv_signed) {
        return texel.rg;
    }
    return texel.bg * 2.0 - 1.0;
}

#endif
