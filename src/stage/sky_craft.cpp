// The second craft, the advertising airship that crosses the sky (sky_craft_route_step,
// sky_craft_draw, sky_craft_screen_update). Nothing tests shots against it or
// reads where it is, so it is presentation: stepped once per world step with the per-update
// numbers halved (docs/sixty.md, "second craft"), drawn interpolated.
#include "render/mesh_flags.hpp"
#include "stage/stage_view.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float route_step = 0.003f / 2;    // Bezier parameter, per update -> step (R1)
constexpr float rotor_step = 0.03f / 2;     // rad (R5)
constexpr float wing_step = 0.002f / 2;     // rad (R5)
constexpr float glow_step = 0.05f / 2;      // sky_glow_phase
constexpr float screen_step = 0.0005f / 2;  // per panel call, two calls a step
constexpr float two_pi = 6.2831855f;

Vec3f bezier(const RouteSegment& s, float t) {
    const float u = 1 - t;
    const float w[4] = {u * u * u, 3 * t * u * u, 3 * t * t * u, t * t * t};
    Vec3f out;
    for (int p = 0; p < 4; ++p) {
        out.x += fx_float(s.p[p].x) * w[p];
        out.y += fx_float(s.p[p].y) * w[p];
        out.z += fx_float(s.p[p].z) * w[p];
    }
    return out;
}

// matrix_orthonormalize on the flat velocity, worked in the original's frame and
// mirrored into the port's
// decomp: matrix_orthonormalize 0x697CA
Mat4 craft_matrix(const float velocity[3], const float at[3]) {
    const Vec3f z = vec_normalize(Vec3f{velocity[0], 0, -velocity[2]});
    const Vec3f x = vec_normalize(Vec3f{z.z, 0, -z.x});
    const Vec3f y = vec_normalize(vec_cross(z, x));
    Mat4 m;
    m.m[0][0] = x.x, m.m[0][1] = x.y, m.m[0][2] = -x.z;
    m.m[1][0] = y.x, m.m[1][1] = y.y, m.m[1][2] = -y.z;
    m.m[2][0] = -z.x, m.m[2][1] = -z.y, m.m[2][2] = z.z;
    m.m[3][0] = at[0], m.m[3][1] = at[1], m.m[3][2] = at[2];
    return m;
}

// the panel's four uvs as sky_craft_screen_update writes them: an advert scrolled in a 0.1-wide
// window, the last two whole; the game's own frame (scene texture) between the adverts
std::vector<MeshVertex> screen_vertices(const ModelObject& object, const SkyCraft& sky) {
    std::vector<MeshVertex> vertices = object.vertices;
    if (vertices.size() < 4) {
        return vertices;
    }
    float u0 = 0, u1 = 1;
    if (sky.slideshow && sky.image <= 2) {
        u0 = 0.0f + sky.scroll;
        u1 = 0.1f + sky.scroll;
    }
    const float uv[4][2] = {{u0, 0}, {u0, 1}, {u1, 0}, {u1, 1}};
    for (int i = 0; i < 4; ++i) {
        vertices[i].uv[0] = uv[i][0];
        vertices[i].uv[1] = uv[i][1];
    }
    return vertices;
}

const ModelObject* slot_object(const StageView& view, int slot) {
    const auto found = view.slots.find(slot);
    return found == view.slots.end()
               ? nullptr
               : &view.sets[found->second.first].set.objects[found->second.second];
}

} // namespace

void sky_craft_start(SkyCraft& sky, const StageData& data, int selector) {
    // stage_route_choose's second half. Retail never resets the wings, the glow, the beacon or
    // the panels between battles (docs/bugs.md); the port starts each battle fresh
    sky = SkyCraft{};
    sky.route = data.sky_route[selector];
    sky.last_segment = data.sky_count[selector];
    const RouteSegment& first = data.routes[sky.route][0];
    sky.pos[0] = sky.previous[0] = fx_float(first.p[0].x);
    sky.pos[1] = sky.previous[1] = fx_float(first.p[0].y);
    sky.pos[2] = sky.previous[2] = fx_float(first.p[0].z);
}

void sky_craft_step(SkyCraft& sky, const StageData& data) {
    sky.matrix_previous = sky.matrix;
    sky.wing_previous = sky.wing;
    sky.rotor_previous = sky.rotor;
    if (sky.wing_state == 5) {
        return;
    }
    const std::vector<RouteSegment>& route = data.routes[sky.route];
    std::copy(sky.pos, sky.pos + 3, sky.previous);
    const Vec3f at = bezier(route[std::min<std::size_t>(sky.segment, route.size() - 1)], sky.t);
    sky.pos[0] = at.x, sky.pos[1] = at.y, sky.pos[2] = at.z;
    sky.t += route_step;
    if (sky.t > 1) {
        sky.t = 0; // not t - 1, as retail
        sky.segment += 1;
        const int s = sky.segment;
        if (s > sky.last_segment) {
            sky.segment = 0;
        } else if (s == 12 || s == 22) {
            sky.wing_state = 1;
        } else if (s == 15 || s == 27) {
            sky.wing_state = 2;
        } else if (s == 35 || s == 53) {
            sky.wing_state = 3;
        } else if (s == 39 || s == 4) {
            sky.wing_state = 4;
        }
    }
    const float velocity[3] = {sky.pos[0] - sky.previous[0], sky.pos[1] - sky.previous[1],
                               sky.pos[2] - sky.previous[2]};
    sky.rotor += rotor_step;
    if (sky.rotor > two_pi) {
        sky.rotor -= two_pi;
        sky.rotor_previous -= two_pi;
    }
    if (sky.wing_state == 1) {
        sky.wing += wing_step;
    } else if (sky.wing_state == 2) {
        sky.wing -= wing_step;
    }
    if ((sky.wing_state == 1 && sky.wing >= 0.5235988f) ||
        (sky.wing_state == 3 && sky.wing >= 0.2617994f) || sky.wing <= 0) {
        sky.wing_state = 0;
    }
    sky.matrix = craft_matrix(velocity, sky.pos);

    // what retail advances while drawing: the glow and the two panels
    sky.glow = std::sin(sky.glow_phase); // the draw uses last update's
    sky.glow_phase += glow_step;
    if (sky.glow_phase > two_pi) {
        sky.glow_phase -= two_pi;
    }
    sky.screens_dirty = true;
    for (int call = 0; call < 2; ++call) {
        sky.scroll += screen_step;
        if (sky.scroll > 1) {
            sky.scroll -= 1;
            sky.slideshow = !sky.slideshow;
            if (sky.slideshow) {
                sky.image = (sky.image + 1) % 5;
            }
        }
    }
}

// decomp: sky_craft_draw 0x6A52C
void sky_craft_draw(StageView& view, Renderer& renderer, const Mat4& view_matrix, float alpha,
                    std::vector<PassItem>& out) {
    SkyCraft& sky = view.sky;
    if (sky.wing_state == 5) {
        return;
    }
    const Mat4 m = mat_lerp(sky.matrix_previous, sky.matrix, alpha);
    const float wing = sky.wing_previous + (sky.wing - sky.wing_previous) * alpha;
    // the glow parts: white, alpha by last update's sin of the glow phase
    const float glow = std::max(sky.glow, 0.0f);
    const std::uint32_t colour = (static_cast<std::uint32_t>(255 * glow) << 24) + 0xFFFFFF;
    const bool day = view.night == 0;
    // the panels: an advert, or the game's own frame (mesh 0x80000); section flag 0x400 either way
    const int screen_slots[2] = {day ? 0x4C2 : 0x4D1, day ? 0x4C6 : 0x4D5};
    const int advert = sky.slideshow ? view.screen_textures[sky.image] : -1;
    const std::uint32_t screen_flags = mesh_no_fog | (sky.slideshow ? 0 : mesh_scene_texture);
    for (int i = 0; i < 2 && sky.screens_dirty; ++i) {
        const ModelObject* object = slot_object(view, screen_slots[i]);
        if (object && view.screen_meshes[i] >= 0) {
            renderer_update_mesh(renderer, view.screen_meshes[i], screen_vertices(*object, sky));
        }
    }
    sky.screens_dirty = false;
    const auto part = [&](int slot, const Mat4& world, int pass, std::uint32_t flags) {
        draw_slot(view, slot, world, pass, flags, view_matrix, out);
    };
    const auto glowing = [&](int slot, const Mat4& world, int pass) {
        draw_slot(view, slot, world, pass, mesh_no_fog | mesh_factor, view_matrix, out, -1, -1,
                  colour);
    };
    const auto panel = [&](int i, const Mat4& world) {
        draw_slot(view, screen_slots[i], world, 8, screen_flags, view_matrix, out,
                  view.screen_meshes[i], advert);
    };

    MatrixStack stack;
    part(day ? 0x4BE : 0x4CD, m, 2, mesh_no_fog);
    glowing(day ? 0x4C8 : 0x4D7, m, 2);
    // one wing, hinged at x 40.2 (x is the same in both frames)
    stack_set(stack, m);
    stack_translate(stack, 40.2f, 0, 0);
    stack_rotate_z(stack, -wing);
    stack_translate(stack, -40.2f, 0, 0);
    const Mat4 left = stack_top(stack);
    part(day ? 0x4C0 : 0x4CF, left, 2, mesh_no_fog);
    glowing(day ? 0x4C3 : 0x4D2, left, 8);
    panel(0, left);
    if (day) {
        part(0x4C1, left, 2, mesh_no_fog);
    }
    // the other wing, hinged at -40.2
    stack_set(stack, m);
    stack_translate(stack, -40.2f, 0, 0);
    stack_rotate_z(stack, wing);
    stack_translate(stack, 40.2f, 0, 0);
    const Mat4 right = stack_top(stack);
    part(day ? 0x4C4 : 0x4D3, right, 2, mesh_no_fog);
    glowing(day ? 0x4C7 : 0x4D6, right, 8);
    panel(1, right);
    if (day) {
        part(0x4C5, right, 8, mesh_no_fog);
        part(0x4BF, m, 9, 0);
        return;
    }
    // at night four rotors under the hull, turning about y (z and the turn negated for the port)
    const float rotor = sky.rotor_previous + (sky.rotor - sky.rotor_previous) * alpha;
    const float rotors[4][3] = {
        {3.06f, -9.16f, -13.33f}, {-2.67f, -9.16f, 3.96f}, {3.24f, -9.16f, 20.97f},
        {-4.54f, -9.16f, 38.55f}};
    for (int r = 0; r < 4; ++r) {
        stack_set(stack, m);
        stack_translate(stack, rotors[r][0], rotors[r][1], rotors[r][2]);
        stack_rotate_y(stack, -rotor);
        part(0x4C9 + r, stack_top(stack), 8, r < 2 ? 0 : mesh_no_fog);
    }
    part(0x4CE, m, 9, 0);
    part(0x4D0, m, 8, mesh_no_fog);
}
