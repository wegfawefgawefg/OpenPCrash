// The depth buffer read as colour (screen_effect_dof binds texture 0x224, LIN_D24S8, with 0x223's format word, so the
// D24S8 word reads as A8R8G8B8 and alpha is the top 8 bits of the 24-bit depth). The stage filters linearly, so the
// alpha bytes of the four texels around the sample are blended.
//
// The port samples the depth aspect (a depth-only view, nearest filtering) and rebuilds that byte. With
// D24_UNORM_S8_UINT the byte is exact; with D32_SFLOAT_S8_UINT (where D24 isn't supported) it is the same 0..1 depth
// quantized the same way, which matches unless depth precision near a byte step differs.

float depth_top_byte(float depth)
{
    uint d24 = uint(floor(clamp(depth, 0.0, 1.0) * 16777215.0 + 0.5));
    return float(d24 >> 16) / 255.0;
}

// Bilinear blend of the top bytes at a 0..1 coordinate, as the original's linear filter on the alpha channel.
float depth_alpha(sampler2D depth_tex, vec2 uv)
{
    vec2 size = vec2(textureSize(depth_tex, 0));
    vec2 f = fract(uv * size - 0.5);
    vec4 d = textureGather(depth_tex, uv, 0); // (i0 j1), (i1 j1), (i1 j0), (i0 j0)
    float b00 = depth_top_byte(d.w);
    float b10 = depth_top_byte(d.z);
    float b01 = depth_top_byte(d.x);
    float b11 = depth_top_byte(d.y);
    return mix(mix(b00, b10, f.x), mix(b01, b11, f.x), f.y);
}
