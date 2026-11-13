#include "stage/stage_animate.hpp"

#include "assets/stage_tables.hpp"
#include "render/mesh_flags.hpp"
#include "render/scene_draw.hpp"

#include <cmath>

namespace {

// per world step: the original's per-update steps halved (docs/sixty.md, "The stage's own
// animation"; R5 for the turn, R1 for the rest)
constexpr float sky_turn_step = 0.0005f / 2;    // stage_render_lods, 0x96CE40
constexpr float lamp_step = 0.02f / 2;          // stage_render_lods, 0x96CE48
constexpr float scroll_slow_step = 0.0005f / 2; // stage_object_animate kind 3, 0x96CE50
constexpr float scroll_fast_step = 0.001f / 2;  // kind 4, 0x96CE4C
constexpr float two_pi = 6.2831855f;

} // namespace

int stage_zone_at(int selector, const float at[3]) {
    // the zone functions read the original's frame: z is the port's negated
    const float x = at[0], y = at[1], z = -at[2];
    switch (selector / 2) {
    case 1: {
        // shibuya_zone_at: inside the round plaza's 39.295 circle by height, outside by depth
        const float dx = x - 30.522f, dz = z - 59.5941f;
        if (std::sqrt(dx * dx + dz * dz) < 39.295f) {
            return y < -25.0f ? 2 : y < -0.5f ? 0 : 1;
        }
        return y > -2.5f ? 1 : y > -40.0f ? 3 : 4;
    }
    case 2:
        // aqualine_zone_at: the island's box by height, the far end, else outside
        if (-92.0f < x && x < 60.5f && -32.0f < z && z < 32.0f) {
            return y < 12.0f ? 1 : y < 23.0f ? 2 : 3;
        }
        return x < -142.0f ? 4 : 0;
    default:
        return 0; // stage_zone_none, stage_zone_none_b
    }
}

void stage_animation_load(StageAnimation& animation, const StageInfo& info, const ModelSet& set,
                          const GpuModelSet& gpu, Renderer& renderer) {
    animation = StageAnimation{};
    for (const StageObject& row : info.objects) {
        if (row.animate < 3) {
            continue; // 0 still, 1 and 2 only turn
        }
        const int object = row.lods[0] - set.first_slot;
        if (object < 0 || object >= static_cast<int>(set.objects.size()) ||
            stage_animated(animation, object)) {
            continue;
        }
        StageAnimatedObject animated;
        animated.object = object;
        animated.kind = row.animate;
        animated.drawn = set.objects[object];
        animated.base = animated.drawn.vertices;
        animated.mesh = gpu.object_mesh[object];
        switch (row.animate) {
        case 3:
        case 4:
        case 7:
            // its own copy to rewrite
            animated.mesh = renderer_upload_mesh(renderer, animated.base, animated.drawn.indices);
            if (row.animate == 7) {
                animated.mesh_flags = mesh_vision | mesh_additive; // set, not or'd
            }
            break;
        case 5:
            // a big screen showing the game: mesh 0x80000 puts the frame being drawn on stage 0
            // (render.md "Mesh drawing"), the first section's texture 0 and flag 0x400. The
            // original multiplies the first four uvs by 1000 once, into the 640x480 linear
            // target's texel units; the port's scene texture takes 0..1: x 1000 / 640, / 480.
            // Shinjuku's screens run 0..0.64 by 0..0.52: the whole width, and 40 texels past the
            // bottom, clamped (layer 0) to the last row
            animated.mesh_flags = mesh_scene_texture;
            if (!animated.drawn.sections.empty()) {
                animated.drawn.sections[0].flags |= 0x400; // the panel section
                animated.drawn.sections[0].tex[0] = 0;
            }
            for (std::size_t v = 0; v < animated.drawn.vertices.size() && v < 4; ++v) {
                animated.drawn.vertices[v].uv[0] *= 1000.0f / 640.0f;
                animated.drawn.vertices[v].uv[1] *= 1000.0f / 480.0f;
            }
            animated.mesh = renderer_upload_mesh(renderer, animated.drawn.vertices,
                                                 animated.drawn.indices);
            break;
        case 6: {
            // the sea: mesh 0x400000 binds the water bump (yurayura on stage 0, wave02 on 1), the
            // first section's layer 0x800 picks seabump, its texture 0, the first four uvs 0..50
            animated.mesh_flags = mesh_water_bump;
            if (!animated.drawn.sections.empty()) {
                animated.drawn.sections[0].layer[0] = layer_seabump;
                animated.drawn.sections[0].tex[0] = 0;
            }
            const float uvs[4][2] = {{0, 0}, {0, 50}, {50, 0}, {50, 50}};
            for (std::size_t v = 0; v < animated.drawn.vertices.size() && v < 4; ++v) {
                animated.drawn.vertices[v].uv[0] = uvs[v][0];
                animated.drawn.vertices[v].uv[1] = uvs[v][1];
            }
            animated.mesh = renderer_upload_mesh(renderer, animated.drawn.vertices,
                                                 animated.drawn.indices);
            break;
        }
        default:
            break;
        }
        animation.objects.push_back(animated);
    }
}

const StageAnimatedObject* stage_animated(const StageAnimation& animation, int object) {
    for (const StageAnimatedObject& animated : animation.objects) {
        if (animated.object == object) {
            return &animated;
        }
    }
    return nullptr;
}

void stage_animation_step(StageAnimation& animation) {
    animation.sky_turn_previous = animation.sky_turn;
    animation.sky_turn += sky_turn_step;
    if (animation.sky_turn >= two_pi) {
        animation.sky_turn -= two_pi;
        animation.sky_turn_previous -= two_pi;
    }
    animation.lamp -= lamp_step;
    if (animation.lamp < 0) {
        animation.lamp = 1;
    }
    // the scrolls move every vertex's v (retail wraps only the second vertex of the object when
    // its counter passes 1, which smears the texture over time: docs/bugs.md); a whole turn of v
    // looks the same on a wrapping texture, so the offset wraps at 1
    animation.scroll_slow = std::fmod(animation.scroll_slow + scroll_slow_step, 1.0f);
    animation.scroll_fast = std::fmod(animation.scroll_fast + scroll_fast_step, 1.0f);
    animation.dirty = true;
}

void stage_animation_frame(StageAnimation& animation, Renderer& renderer, float alpha) {
    animation.sky_turn_drawn =
        animation.sky_turn_previous + (animation.sky_turn - animation.sky_turn_previous) * alpha;
    if (!animation.dirty) {
        return;
    }
    animation.dirty = false;
    for (StageAnimatedObject& animated : animation.objects) {
        if (animated.kind != 3 && animated.kind != 4 && animated.kind != 7) {
            continue;
        }
        std::vector<MeshVertex> vertices = animated.base;
        for (MeshVertex& vertex : vertices) {
            if (animated.kind == 7) {
                // ARGB (lamp x 255) << 24 | 0xFF0000: red, fading
                vertex.colour[0] = 255;
                vertex.colour[1] = 0;
                vertex.colour[2] = 0;
                vertex.colour[3] = static_cast<std::uint8_t>(animation.lamp * 255.0f);
            } else {
                vertex.uv[1] += animated.kind == 3 ? animation.scroll_slow : animation.scroll_fast;
            }
        }
        renderer_update_mesh(renderer, animated.mesh, vertices);
    }
}
