// The per-draw block of the screen-effect fragment shaders (blur, dof, dofblur, shimmer).
//
// The original addresses its linear targets in texels, and each stage's texel space is the size of the texture it
// samples in the original (640x480 scene and depth, 320x240 and 160x120 blur targets; the wave texture is swizzled and
// takes 0..1). texel_scale[s] turns stage s's coordinate into 0..1: (1/width, 1/height) of that original size, or
// (1, 1). Using the original's sizes, not the port's, keeps every offset (the blur taps, the bump offsets) the same
// fraction of the screen at any resolution.
//
// PS constants (screen_effect_dof 0xB5F30): c0 = scene_tint, c1 = 0 (its color2 argument), c2 = (0, 0, 0, 0.95 +
// 0.0005 x dof_level), each packed to 8 bits by the runtime (nv_q8) before upload. bump_env is the heat haze's bump
// matrix, the same on stages 0..2: (M00, M01, M10, M11) = (bump_scale, bump_shear, bump_scale, bump_shear).

layout(set = 1, binding = 0, std140) uniform ScreenDraw
{
    vec4 texel_scale[4];
    vec4 c0;       // PS c0
    vec4 c1;       // PS c1
    vec4 c2;       // PS c2
    vec4 bump_env; // stages 1 and 2 (dofblur)
}
draw;

vec2 screen_uv(int s, vec2 texel)
{
    return texel * draw.texel_scale[s].xy;
}
