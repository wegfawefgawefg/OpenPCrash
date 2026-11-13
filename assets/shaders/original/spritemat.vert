#version 450
#extension GL_GOOGLE_include_directive : require
// spritemat.xvu, bind row 0xF (shader_bind_spritemat), with premultiplied.frag (spritemat.xpu).
// The overlay passes 0, 12, 14, 15, 16 (pass_draw_overlay 0xB4FF0): every 2D thing (menus, HUD, text glyphs, fades,
// movies, the lens flare) is a sprite.
//
// The original runs in 192-constant mode: c0 = (1, 0, 0.5, W/2 - 0.5), c1 = (0, 0, H/2 - 0.5, 0) with W, H the view's
// viewport size (576x448 for one view, 640x480 for pass 15), and per vertex three registers from c60 (44 vertices)
// then c4 (16): (x, y, u, v), (r, g, b, a), (k, 0, 0, 0). sprite_batch_vb's vertices hold only the register number
// (v2.x; v0 and v1 are zero). The CPU builds each corner: positions in viewport pixels ((x - 32, y - 16) plus the scaled
// size, rotated about the pivot, floored unless flag 0x10), uv from the texel rectangle (divided by the texture size
// unless flag 2, swapped for 0x400 and 0x800), k = 0 for flag 1, else 1.
// The port puts the corners in a storage buffer, four SpriteVertex per sprite in the original's corner order, and
// draws six vertices per sprite.
// Outputs: oPos, oD0 = colour, oT0 = uv, oT1 = k (all four components). No oFog: fog is off in the overlay passes.
// Computes: x_ndc = (x - (W/2 - 0.5)) / (W/2 - 0.5), y_ndc = ((H/2 - 0.5) - y) / (H/2 - 0.5), z 0.5, w 1. So pixel
// 0 lands on the viewport's left edge and pixel W - 1 on its right edge (a stretch of W / (W - 1), kept).
// Changed: the viewport epilogue is dropped; vertices come from the buffer instead of a0 indexing into constants.

#include "include/d3d.glsl"
#include "include/frame.glsl"

struct SpriteVertex {
    vec4 xyuv;  // x, y in viewport pixels, u, v
    vec4 color; // r, g, b, a
    vec4 k;     // k, 0, 0, 0
};

layout(set = 1, binding = 0, std140) uniform SpriteDraw
{
    vec4 c0; // (1, 0, 0.5, W/2 - 0.5)
    vec4 c1; // (0, 0, H/2 - 0.5, 0)
}
draw;

layout(set = 1, binding = 1, std430) readonly buffer SpriteVertices
{
    SpriteVertex sprite_vertices[];
};

layout(location = 0) out vec4 v_d0;
layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 6) out float v_fog;

const int QUAD_TRIANGLES[6] = int[6](0, 1, 2, 0, 2, 3);

void main()
{
    int sprite = gl_VertexIndex / 6;
    SpriteVertex sv = sprite_vertices[sprite * 4 + QUAD_TRIANGLES[gl_VertexIndex % 6]];

    // viewport pixels to the clip square
    float x = (sv.xyuv.x - draw.c0.w) * (1.0 / draw.c0.w);
    float y = (draw.c1.z - sv.xyuv.y) * (1.0 / draw.c1.z);
    gl_Position = vec4(x, y, draw.c0.z, draw.c0.x);

    v_d0 = clamp(sv.color, 0.0, 1.0);
    v_t0 = vec4(sv.xyuv.zw, 0.0, 1.0);
    v_t1 = vec4(sv.k.x);
    v_fog = d3d_fog_factor(NV2A_DEFAULT_FOG_COORD);
}
