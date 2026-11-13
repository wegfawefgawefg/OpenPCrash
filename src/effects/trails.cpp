// Ribbon trails (decomp src/hand/u_06eda0.c, docs/port/effects.md "Trails"): trail_start,
// trail_start_segments, trail_push, trail_release, trail_age, and
// trail_draw rebuilt per frame with the newest point between the last two updates and the
// ribbon turned to this frame's eye.
#include "effects/effects.hpp"
#include "render/mesh_queue.hpp"

namespace {

constexpr int point_count = 33;

// cross(point - eye, segment); a segment under 0.1 long takes the one before
// decomp: trail_side_at 0x720EB
Vec3f side_at(const Vec3f* points, int index, const Vec3f& eye) {
    const Vec3f a = vec_sub(points[index], eye);
    Vec3f b = vec_sub(points[index], points[index + 1 < point_count ? index + 1 : index]);
    if (index && vec_length(b) < 0.1f) {
        b = vec_sub(points[index - 1], points[index]);
    }
    return vec_cross(a, b);
}

std::uint8_t byte_of(float alpha) {
    const int a = static_cast<int>(alpha);
    return static_cast<std::uint8_t>(a & 0xFF);
}

} // namespace

// decomp: trail_start 0x70258
int trail_start(Effects& fx, const Vec3f& at, int segments, int key) {
    for (int i = 0; i < 50; ++i) {
        Trail& t = fx.trails[i];
        if (t.active) {
            continue;
        }
        for (Vec3f& p : t.points) {
            p = at;
        }
        t.before_head = at;
        t.active = true;
        t.length = 0;
        t.released = false;
        t.age = 0;
        t.segments = segments;
        t.key = key;
        return i;
    }
    return -1;
}

// decomp: trail_push 0x70451
void trail_push(Effects& fx, int trail, const Vec3f& at, int texture, int mode,
                std::uint32_t colour, float width_start, float width_end, float alpha_start,
                float alpha_end) {
    if (trail < 0 || trail >= 50) {
        return;
    }
    Trail& t = fx.trails[trail];
    t.before_head = t.points[0];
    for (int i = point_count - 1; i > 0; --i) {
        t.points[i] = t.points[i - 1];
    }
    // the unused tail sits on the head
    for (int j = 0; j <= 32 - t.segments; ++j) {
        t.points[j] = at;
    }
    if (t.length < point_count) {
        t.length += 1;
    }
    t.mode = mode;
    if (mode == 2) {
        t.factor = colour;
    }
    t.alpha_start = alpha_start;
    t.alpha_end = alpha_end;
    t.width_start = width_start;
    t.width_end = width_end;
    t.texture = texture;
}

// decomp: trail_push_default 0x70425
void trail_push_default(Effects& fx, int trail, const Vec3f& at) {
    // trail_push_default: texture 0x16D, 0.1 wide at the head to 1.5 at the tail, 255 to 0
    trail_push(fx, trail, at, -1, 0, 0xFFFFFFFF, 0.1f, 1.5f, 255.0f, 0.0f);
}

// decomp: trail_release 0x705D2
void trail_release(Effects& fx, int trail) {
    if (trail >= 0 && trail < 50) {
        fx.trails[trail].released = true;
    }
}

void trails_update(Effects& fx) {
    // trail_age: 30 updates after release, then free
    for (Trail& t : fx.trails) {
        if (t.active && t.released) {
            t.age += 1;
            if (static_cast<float>(t.age) > 30.0f) {
                t.active = false;
                t.key = -1;
            }
        }
    }
}

void trails_queue(Effects& fx, Renderer& renderer, float lerp, const Mat4& view, const Vec3f& eye,
                  int view_index, std::vector<PassItem>& items) {
    const EffectAssets& assets = fx.assets;
    if (assets.trail_vertices.empty()) {
        return;
    }
    // the view's own meshes (retail builds every trail for view 0's eye, docs/bugs.md)
    const int* meshes = assets.trail_mesh[view_index >= 0 && view_index < 4 ? view_index : 0];
    std::vector<MeshVertex> vertices = assets.trail_vertices;
    const int spans = static_cast<int>(vertices.size()) / 4;
    for (int index = 0; index < 50; ++index) {
        const Trail& t = fx.trails[index];
        if (!t.active || meshes[index] < 0) {
            continue;
        }
        // the points with the newest between its last two pushes
        Vec3f points[point_count];
        for (int i = 0; i < point_count; ++i) {
            points[i] = t.points[i];
        }
        const Vec3f head =
            vec_add(t.before_head, vec_scale(vec_sub(t.points[0], t.before_head), lerp));
        for (int j = 0; j <= 32 - t.segments; ++j) {
            points[j] = head;
        }
        const float length = static_cast<float>(t.length > 0 ? t.length : 1);
        const float width_end = t.width_end * static_cast<float>(t.length) / 33.0f;
        const float fade = (30.0f - static_cast<float>(t.age)) / 30.0f;
        for (int span = 0; span < spans && span + 1 < point_count; ++span) {
            MeshVertex* v = &vertices[static_cast<std::size_t>(span) * 4];
            if (span < 32 - t.segments) {
                // collapsed on the head, clear
                for (int j = 0; j < 4; ++j) {
                    v[j].position[0] = points[0].x;
                    v[j].position[1] = points[0].y;
                    v[j].position[2] = -points[0].z;
                    v[j].colour[0] = v[j].colour[1] = v[j].colour[2] = v[j].colour[3] = 0;
                }
                continue;
            }
            Vec3f near_side = vec_normalize(side_at(points, span, eye));
            Vec3f far_side =
                span != spans - 1 ? vec_normalize(side_at(points, span + 1, eye)) : near_side;
            // width and alpha linear from head to tail, the tail's end at 0
            const float width_step = (t.width_start - width_end) / length;
            const float alpha_step = (t.alpha_start - t.alpha_end) / length;
            float near_w = 0, far_w = 0, near_a = 0, far_a = 0;
            if (span < t.length - 1) {
                near_w = t.width_start - static_cast<float>(span) * width_step;
                far_w = t.width_start - static_cast<float>(span + 1) * width_step;
                near_a = t.alpha_start - static_cast<float>(span) * alpha_step;
                far_a = t.alpha_start - static_cast<float>(span + 1) * alpha_step;
            } else if (span == t.length - 1) {
                near_w = t.width_start - static_cast<float>(span) * width_step;
                near_a = t.alpha_start - static_cast<float>(span) * alpha_step;
            }
            near_a *= fade;
            far_a *= fade;
            const Vec3f corners[4] = {vec_add(points[span], vec_scale(near_side, near_w)),
                                      vec_sub(points[span], vec_scale(near_side, near_w)),
                                      vec_add(points[span + 1], vec_scale(far_side, far_w)),
                                      vec_sub(points[span + 1], vec_scale(far_side, far_w))};
            for (int j = 0; j < 4; ++j) {
                v[j].position[0] = corners[j].x;
                v[j].position[1] = corners[j].y;
                v[j].position[2] = -corners[j].z;
                v[j].colour[0] = v[j].colour[1] = v[j].colour[2] = 0xFF;
                v[j].colour[3] = byte_of(j < 2 ? near_a : far_a);
            }
        }
        renderer_update_mesh(renderer, meshes[index], vertices);

        // mode 0 texture 0x16D, 1 the given texture (vision_eff, mesh 0x200000); 2 a colour
        const int mode = t.mode >= 0 && t.mode < 3 ? t.mode : 0;
        const int base = mode == 0 ? 0x16D : mode == 1 ? t.texture : -1;
        const std::size_t first = items.size();
        queue_object(assets.trail_objects[mode], meshes[index], base, mat_identity(), 9,
                     mode == 2 ? 0x8020u : 0x200000u, view, items);
        for (std::size_t i = first; i < items.size(); ++i) {
            items[i].item.factor = mode == 2 ? t.factor : 0xFFFFFFFF;
        }
    }
}
