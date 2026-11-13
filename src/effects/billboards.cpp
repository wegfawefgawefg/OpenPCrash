#include "effects/billboards.hpp"

#include "effects/effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float pi = 3.1415927f;

// the atlas cell table at 0x966000 (func_0011CFC0 fills 22 of them): 1/8 by 1/4 cells in rows
void atlas_cell(int index, float& u, float& v) {
    if (index < 0 || index >= 22) {
        u = v = 0;
        return;
    }
    u = static_cast<float>(index % 8) * 0.125f;
    v = static_cast<float>(index / 8) * 0.25f;
}

// a point of the original's world to the port's view space (the port's view takes z negated)
Vec3f to_view(const Vec3f& world, const Mat4& view) {
    return transform_coord({world.x, world.y, -world.z}, view);
}

// a billboard's view z beside its index, so the sort reads its keys in a row
struct DepthKey {
    float z;
    int index;
};

// a merge sort of indices by view z, farthest first; on a tie the right
// half's goes first, as retail's merge takes it
// decomp: billboards_sort 0xC03DD
void sort_far_first(DepthKey* items, DepthKey* left, int n) {
    if (n <= 1) {
        return;
    }
    const int half = n / 2;
    sort_far_first(items, left, half);
    sort_far_first(items + half, left, n - half);
    std::copy(items, items + half, left);
    int l = 0, r = half, out = 0;
    while (l < half && r < n) {
        if (left[l].z > items[r].z) {
            items[out++] = left[l++];
        } else {
            items[out++] = items[r++];
        }
    }
    while (l < half) {
        items[out++] = left[l++];
    }
}

} // namespace

// decomp: billboards_prepare 0xBFA64
void billboards_prepare(const std::vector<BillboardPush>& pushes, float t, const Mat4& view,
                        std::vector<Billboard>& out) {
    out.clear();
    std::vector<Billboard> all;
    all.reserve(pushes.size());
    Vec3f anchor{};       // billboard_streak_anchor: the last mode 3, 5 or 7 head
    Vec3f parent{};       // the last dome parent's view position
    float dome_scale = 1; // its view z, clamped 10..30, x 0.1
    const int count = static_cast<int>(pushes.size()) < billboard_max
                          ? static_cast<int>(pushes.size())
                          : billboard_max;
    int extra = 0;
    std::vector<Billboard> copies;
    for (int i = 0; i < count; ++i) {
        const BillboardPush& p = pushes[static_cast<std::size_t>(i)];
        // billboard_push: the position (between the updates), angle, size, cell, flag
        Billboard b{};
        Vec3f at = p.mode == 2 ? p.position
                               : vec_add(p.before, vec_scale(vec_sub(p.position, p.before), t));
        float angle = p.angle;
        while (angle < -pi || angle > pi) {
            angle += angle < -pi ? 2 * pi : -2 * pi;
        }
        float c = std::cos(angle), s = std::sin(angle);
        if (p.mode == 2) {
            at = vec_scale(at, p.size);
        }
        const float size = p.mode == 1 ? p.size * 3.5f : p.size;
        std::memcpy(b.color, p.rgba, sizeof b.color);
        atlas_cell(p.cell, b.cell[0], b.cell[1]);
        b.cell[2] = c * size;
        b.cell[3] = s * size;
        b.pos[3] = p.additive ? 0.0f : 1.0f;

        // billboards_prepare: the modes in view space
        Vec3f v;
        switch (p.mode) {
        case 9:
            v = to_view(at, view);
            v.z -= 2;
            break;
        case 3:
        case 5:
        case 7:
            v = to_view(at, view);
            anchor = v;
            break;
        case 4:
        case 6:
        case 8: {
            // the tail, and 15 (mode 4) or 3 copies stepping back to the head
            v = to_view(at, view);
            const int n = p.mode == 4 ? 15 : 3;
            if (count + extra + n < billboard_max) {
                const Vec3f step = vec_scale(vec_sub(v, anchor), 1.0f / static_cast<float>(n));
                Billboard copy = b;
                Vec3f cv = vec_sub(v, step);
                for (int k = 0; k < n; ++k) {
                    copy.pos[0] = cv.x, copy.pos[1] = cv.y, copy.pos[2] = cv.z;
                    copies.push_back(copy);
                    cv = vec_sub(cv, step);
                }
                extra += n;
            }
            break;
        }
        case 1: {
            // a dome's parent: its children are placed against it, scaled by its distance
            parent = to_view(at, view);
            v = parent;
            float z = v.z;
            z = z > 30 ? 30 : z < 10 ? 10 : z;
            dome_scale = z * 0.1f;
            v.z -= 1;
            break;
        }
        case 2:
            v = vec_add(vec_scale(at, dome_scale), parent);
            b.cell[2] *= dome_scale;
            b.cell[3] *= dome_scale;
            break;
        default:
            v = to_view(at, view);
            break;
        }
        b.pos[0] = v.x, b.pos[1] = v.y, b.pos[2] = v.z;
        all.push_back(b);
    }
    // the copies go after every pushed one, as retail appends them
    all.insert(all.end(), copies.begin(), copies.end());

    // back to front; behind the camera dropped
    std::vector<DepthKey> order(all.size()), scratch(all.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = {all[i].pos[2], static_cast<int>(i)};
    }
    sort_far_first(order.data(), scratch.data(), static_cast<int>(order.size()));
    out.reserve(all.size());
    for (const DepthKey& key : order) {
        if (key.z >= 0) {
            out.push_back(all[static_cast<std::size_t>(key.index)]);
        }
    }
}

// decomp: billboards_draw 0xBF71D
void billboards_draw(const std::vector<Billboard>& billboards, int atlas, const Mat4& projection,
                     FrameDraws& draws) {
    if (billboards.empty()) {
        return;
    }
    // the instances after whatever the frame's storage holds already
    const std::size_t first = (draws.storage.size() + sizeof(Billboard) - 1) / sizeof(Billboard);
    draws.storage.resize((first + billboards.size()) * sizeof(Billboard));
    std::memcpy(draws.storage.data() + first * sizeof(Billboard), billboards.data(),
                billboards.size() * sizeof(Billboard));
    // billboards_draw's states: z test, no z write, ONE / INV_SRC_ALPHA, alpha test always, fog
    // off, cull CCW, the atlas with wrap
    DrawItem item;
    item.program = Program::particle;
    item.blend = Blend::premultiplied;
    item.depth_test = true;
    item.depth_write = false;
    item.cull = Cull::back;
    item.first_index = static_cast<std::uint32_t>(first);
    item.index_count = static_cast<std::uint32_t>(billboards.size());
    item.stages[0].texture = atlas >= 0 ? atlas : texture_white;
    item.stages[0].u = item.stages[0].v = Address::Wrap;
    item.uniform_size = sizeof(Mat4);
    item.uniform_offset = push_uniform(draws, &projection, sizeof(Mat4));
    draws.items.push_back(item);
}

Vec3f original_of(const FxVec3& v) {
    return {fx_float(v.x), fx_float(v.y), -fx_float(v.z)};
}

Mat4 port_matrix(const Mat4& m) {
    Mat4 out = m;
    out.m[0][2] = -out.m[0][2];
    out.m[1][2] = -out.m[1][2];
    out.m[3][2] = -out.m[3][2];
    out.m[2][0] = -out.m[2][0];
    out.m[2][1] = -out.m[2][1];
    return out;
}

// decomp: matrix_billboard_at 0x661A3
Mat4 billboard_matrix(const Vec3f& at, const Vec3f& eye, float off_x, float off_y, float off_z) {
    const Vec3f b = vec_sub(at, eye);
    const Vec3f x = vec_normalize(vec_cross(Vec3f{0, 1, 0}, b));
    const Vec3f y = vec_normalize(vec_cross(b, vec_cross(Vec3f{0, 1, 0}, b)));
    const Vec3f z = vec_normalize(b);
    Mat4 m;
    m.m[0][0] = x.x, m.m[0][1] = x.y, m.m[0][2] = x.z;
    m.m[1][0] = y.x, m.m[1][1] = y.y, m.m[1][2] = y.z;
    m.m[2][0] = z.x, m.m[2][1] = z.y, m.m[2][2] = z.z;
    m.m[3][0] = x.x * off_x + at.x + y.x * off_y + z.x * off_z;
    m.m[3][1] = x.y * off_x + at.y + y.y * off_y + z.y * off_z;
    m.m[3][2] = x.z * off_x + at.z + y.z * off_y + z.z * off_z;
    return m;
}
