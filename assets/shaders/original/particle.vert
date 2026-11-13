#version 450
#extension GL_GOOGLE_include_directive : require
// particle.xvu, bind row 0xE (shader_bind_particle), with premultiplied.frag (particle.xpu).
// Pass 10, the billboards (billboards_draw 0xBF71D), per view, drawn back to front.
//
// The original runs in 192-constant mode: c0..c3 = Projection^T, c4 = (1, 0, 0, 0), and three registers per billboard
// from c60 (44 of them) then c7 (17), 61 a draw, over billboard_vertices whose vertices hold a corner (+-0.5), the
// corner's uv inside an atlas cell and the billboard's register number (v2.x). The port draws instances instead: one
// Billboard per instance from a storage buffer, six vertices (two triangles) per instance.
// A billboard (billboard_push 0xBF44F, decoded by billboards_prepare 0xBFA64, which moves it to view space, builds the
// streak copies, sorts and drops those behind the camera):
//   pos    view-space centre; w = the flag, 0 or 1 once the mode code (a power of ten added to w) is removed
//   color  RGBA (the 16 bytes the code calls uv)
//   cell   (atlas cell u, atlas cell v) from the table at 0x966000, then (cos a x size, sin a x size); mode 1 scales
//          the size by 3.5, modes 10 and 100 scale it relative to a parent
// The atlas (texture slot 0x190, wrap) has cells of 1/8 x 1/4, the corner uvs inset by 1/1024.
// Outputs: oPos, oD0 = colour, oT0 = cell + corner uv, oT1 = w (all four components). No oFog: fog is off in pass 10.
// Computes: the corner rotated and scaled in view space, x' = x + cx cos + cy sin, y' = y - cx sin + cy cos (cos and sin
// pre-multiplied by the size), z kept, then projected.
// Changed: the viewport epilogue is dropped; vertices come from gl_VertexIndex and the instance buffer instead of a0
// indexing into constants.

#include "include/d3d.glsl"
#include "include/frame.glsl"

struct Billboard {
    vec4 pos;
    vec4 color;
    vec4 cell;
};

layout(set = 1, binding = 0, std140) uniform ParticleDraw
{
    mat4 proj; // c0..c3
}
draw;

layout(set = 1, binding = 1, std430) readonly buffer Billboards
{
    Billboard billboards[];
};

layout(location = 0) out vec4 v_d0;
layout(location = 2) out vec4 v_t0;
layout(location = 3) out vec4 v_t1;
layout(location = 6) out float v_fog;

// billboard_vertices' corners (the table at 0x280CA8): position and uv inside a cell.
const float CELL_U0 = 1.0 / 1024.0;
const float CELL_U1 = 1.0 / 8.0 - 1.0 / 1024.0;
const float CELL_V0 = 1.0 / 1024.0;
const float CELL_V1 = 1.0 / 4.0 - 1.0 / 1024.0;
const vec4 CORNERS[4] = vec4[4](vec4(-0.5, 0.5, CELL_U0, CELL_V1), vec4(0.5, 0.5, CELL_U1, CELL_V1),
                                vec4(0.5, -0.5, CELL_U1, CELL_V0), vec4(-0.5, -0.5, CELL_U0, CELL_V0));
// the quad list's four corners as two triangles
const int QUAD_TRIANGLES[6] = int[6](0, 1, 2, 0, 2, 3);

void main()
{
    Billboard b = billboards[gl_InstanceIndex];
    vec4 corner = CORNERS[QUAD_TRIANGLES[gl_VertexIndex % 6]];

    // rotate and scale the corner in view space, then project
    vec4 pos = vec4(b.pos.xyz, 1.0);
    pos.x = b.pos.x + corner.x * b.cell.z + corner.y * b.cell.w;
    pos.y = b.pos.y - corner.x * b.cell.w + corner.y * b.cell.z;
    gl_Position = draw.proj * pos;

    v_d0 = clamp(b.color, 0.0, 1.0);
    v_t0 = vec4(corner.zw + b.cell.xy, 0.0, 1.0);
    v_t1 = vec4(b.pos.w);
    v_fog = d3d_fog_factor(NV2A_DEFAULT_FOG_COORD);
}
