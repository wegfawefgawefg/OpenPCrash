// The per-draw block of bind rows 0x10..0x13 (point.vert, point.frag), filled by shader_constants_point 0xC2E7B for
// all four rows.
//
//   c96..c99   world_view_proj   (World x View x Proj)^T
//   c100       k                 (0, 0.5, 1, 1)
//   c101       light             -Light.Direction, normalized, world space, w 0
//   c102       diffuse           Light.Diffuse, w 1
//   c103       ambient           Light.Ambient, w 1
//   c104..c107 world             World^T
//   c108..c110 light_pos[i]      the three point lights nearest Mesh.center (of the stage's six, point_light_add),
//                                w 1; with no lights all three are the default position D_00280CE8..
//   c111       ranges            (range 0, range 1, range 2, 1 / D_00280604): the three ranges, then the height scale
//                                pointconst's fog uses (1/100; with no lights 1 / D_002805D8.f_2c)
//   PS c1..c3  light_color[i]    the three lights' colours, a = 0 (bytes / 255, so already 8-bit exact)
// Nearest is by distance from the mesh centre to each light, ties taking the later index, an index out of 0..5 read as 0.

layout(set = 1, binding = 0, std140) uniform PointDraw
{
    mat4 world_view_proj; // c96..c99
    mat4 world;           // c104..c107
    vec4 k;               // c100
    vec4 light;           // c101
    vec4 diffuse;         // c102
    vec4 ambient;         // c103
    vec4 light_pos[3];    // c108..c110
    vec4 ranges;          // c111
    vec4 light_color[3];  // PS c1..c3
}
draw;
