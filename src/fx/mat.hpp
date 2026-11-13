#pragma once
// Fixed-point matrices in the original's convention, D3DX's: a point is a row and goes on the
// left (p' = p * M), a matrix's rows are where it sends the x, y and z axes, and M * N applies M
// first. FxMat4 is the affine 4x4 the simulation uses: rows x, y, z and the translation w, the
// fourth column always (0, 0, 0, 1). Nothing in the world projects, so there is no general 4x4;
// and no general inverse (a 3x3 determinant of stage-sized rows is past 64 bits): rigid
// transforms invert by transposing, and the collision's triangle frames are solved in fx/hit.
// Each element is summed whole in 64 bits and rounded once, to nearest.
#include "fx/angle.hpp"
#include "fx/vec.hpp"

#include <cstdint>

struct FxMat3 {
    FxVec3 x, y, z;
    friend constexpr bool operator==(const FxMat3&, const FxMat3&) = default;
};
struct FxMat4 {
    FxVec3 x, y, z, w;
    friend constexpr bool operator==(const FxMat4&, const FxMat4&) = default;
};

constexpr FxMat3 mat3_identity() {
    return {{fx(1), Fx{}, Fx{}}, {Fx{}, fx(1), Fx{}}, {Fx{}, Fx{}, fx(1)}};
}

// v * m: v.x times row x plus v.y times row y plus v.z times row z
constexpr FxVec3 transform(FxVec3 v, const FxMat3& m) {
    const auto part = [&](Fx a, Fx b, Fx c) {
        return fx_from_q32(static_cast<std::int64_t>(v.x.raw) * a.raw +
                           static_cast<std::int64_t>(v.y.raw) * b.raw +
                           static_cast<std::int64_t>(v.z.raw) * c.raw);
    };
    return {part(m.x.x, m.y.x, m.z.x), part(m.x.y, m.y.y, m.z.y), part(m.x.z, m.y.z, m.z.z)};
}
// a * b (D3DXMatrixMultiply): a first, then b. Each row of a sent through b
constexpr FxMat3 mat3_mul(const FxMat3& a, const FxMat3& b) {
    return {transform(a.x, b), transform(a.y, b), transform(a.z, b)};
}
constexpr FxMat3 mat3_transpose(const FxMat3& m) {
    return {{m.x.x, m.y.x, m.z.x}, {m.x.y, m.y.y, m.z.y}, {m.x.z, m.y.z, m.z.z}};
}

// D3DXMatrixRotationX, Y and Z: a positive angle turns y toward z, z toward x and x toward y
constexpr FxMat3 mat3_rotation_x(Angle a) {
    const Fx s = fx_sin(a), c = fx_cos(a);
    return {{fx(1), Fx{}, Fx{}}, {Fx{}, c, s}, {Fx{}, -s, c}};
}
constexpr FxMat3 mat3_rotation_y(Angle a) {
    const Fx s = fx_sin(a), c = fx_cos(a);
    return {{c, Fx{}, -s}, {Fx{}, fx(1), Fx{}}, {s, Fx{}, c}};
}
constexpr FxMat3 mat3_rotation_z(Angle a) {
    const Fx s = fx_sin(a), c = fx_cos(a);
    return {{c, s, Fx{}}, {-s, c, Fx{}}, {Fx{}, Fx{}, fx(1)}};
}
// D3DXMatrixRotationAxis about a unit axis (normalize it first)
constexpr FxMat3 mat3_rotation_axis(FxVec3 axis, Angle a) {
    const Fx s = fx_sin(a), c = fx_cos(a), t = fx(1) - c;
    const Fx x = axis.x, y = axis.y, z = axis.z;
    const Fx tx = fx_mul_near(t, x), ty = fx_mul_near(t, y), tz = fx_mul_near(t, z);
    return {{fx_mul_near(tx, x) + c, fx_mul_near(tx, y) + fx_mul_near(s, z),
             fx_mul_near(tx, z) - fx_mul_near(s, y)},
            {fx_mul_near(ty, x) - fx_mul_near(s, z), fx_mul_near(ty, y) + c,
             fx_mul_near(ty, z) + fx_mul_near(s, x)},
            {fx_mul_near(tz, x) + fx_mul_near(s, y), fx_mul_near(tz, y) - fx_mul_near(s, x),
             fx_mul_near(tz, z) + c}};
}

// Where a heading sends the fighter's forward, -z (gameplay.md "Walk": forward is -z rotated by
// heading): (0, 0, -1) * mat3_rotation_y(heading)
constexpr FxVec3 forward_of_heading(Angle heading) {
    return {-fx_sin(heading), Fx{}, -fx_cos(heading)};
}

// x and y normalized, z the normalized x cross y (taken before x
// and y are normalized, as the original does). Rotations multiplied step after step drift; this
// squares them up
// decomp: matrix_axes_normalize 0x54A83
constexpr FxMat3 mat3_axes_normalized(const FxMat3& m) {
    return {normalized(m.x), normalized(m.y), unit_cross(m.x, m.y)};
}
// z along dir, x = up cross dir, y = dir cross x, all normalized
// (from the exact crosses, so a dir near up still gives square axes)
// decomp: matrix_from_dir_up 0xD3D59
constexpr FxMat3 mat3_from_dir_up(FxVec3 dir, FxVec3 up) {
    const FxVec3 x = unit_cross(up, dir);
    return {x, unit_cross(dir, x), normalized(dir)};
}

constexpr FxMat4 mat4_identity() {
    const FxMat3 basis = mat3_identity();
    return {basis.x, basis.y, basis.z, {}};
}
constexpr FxMat4 mat4_from(const FxMat3& basis, FxVec3 translation) {
    return {basis.x, basis.y, basis.z, translation};
}
constexpr FxMat4 mat4_translation(FxVec3 translation) {
    return mat4_from(mat3_identity(), translation);
}
constexpr FxMat3 mat4_basis(const FxMat4& m) {
    return {m.x, m.y, m.z};
}

// D3DXVec3TransformCoord for an affine matrix: p * basis + translation
constexpr FxVec3 transform_coord(FxVec3 p, const FxMat4& m) {
    return transform(p, mat4_basis(m)) + m.w;
}
// D3DXVec3TransformNormal: the direction turned, not moved
constexpr FxVec3 transform_normal(FxVec3 v, const FxMat4& m) {
    return transform(v, mat4_basis(m));
}
// a * b: a first, then b
constexpr FxMat4 mat4_mul(const FxMat4& a, const FxMat4& b) {
    const FxMat3 basis = mat3_mul(mat4_basis(a), mat4_basis(b));
    return mat4_from(basis, transform_coord(a.w, b));
}
// the inverse of a rotation and translation (an orthonormal basis): the basis transposed, the
// translation sent back through it
constexpr FxMat4 mat4_rigid_inverse(const FxMat4& m) {
    const FxMat3 back = mat3_transpose(mat4_basis(m));
    return mat4_from(back, -transform(m.w, back));
}

// D3DXMatrixLookAtLH, the view matrix: z toward at, x = up cross z, y = z cross x; the columns
// are the axes and the last row moves eye to the origin. Its rigid inverse is the camera's place
// in the world. x and y come from the exact view offset, not from z: looking near straight up or
// down, z's small sideways parts are too coarse to aim the side axis by (docs/numbers.md)
constexpr FxMat4 mat4_look_at_lh(FxVec3 eye, FxVec3 at, FxVec3 up) {
    const FxVec3 view = at - eye;
    const FxVec3 z = normalized(view);
    const FxVec3 x = unit_cross(up, view);
    const FxVec3 y = unit_cross(view, x);
    const FxMat3 axes = mat3_transpose({x, y, z});
    return mat4_from(axes, {-dot(x, eye), -dot(y, eye), -dot(z, eye)});
}
