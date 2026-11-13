// The IK solvers, transcribed from the decomp (src/hand/beam_quad.c 0xAB9BE..0xAC3B2). The original
// runs them on IkLink records it zeroes first, so their extra roll angles and bend flag are always
// 0; those are left out here.
#include "mech/mech_pose.hpp"

#include <cmath>

namespace {

// m turned so its +x points at v; returns the distance to v
// decomp: matrix_aim_x_at 0xABFCE
float aim_x_at(const Vec3f& v, const Mat4& m, Mat4& out) {
    const Vec3f local = transform_coord(v, mat_inverse(m));
    const float flat = std::sqrt(local.x * local.x + local.y * local.y);
    const float reach = std::sqrt(local.z * local.z + local.x * local.x + local.y * local.y);
    Mat4 turn;
    // a z turn by the angle of (x, y)
    const float sine = flat > 0 ? local.y / flat : 0;
    const float cosine = flat > 0 ? local.x / flat : 1;
    turn.m[0][0] = cosine;
    turn.m[0][1] = sine;
    turn.m[1][0] = -sine;
    turn.m[1][1] = cosine;
    out = mat_multiply(turn, m);
    // then a y turn lifting x onto v
    Mat4 lift;
    const float lift_sine = reach > 0 ? local.z / reach : 0;
    const float lift_cosine = reach > 0 ? flat / reach : 1;
    lift.m[2][2] = lift_cosine;
    lift.m[2][0] = -lift_sine;
    lift.m[0][2] = lift_sine;
    lift.m[0][0] = lift_cosine;
    out = mat_multiply(lift, out);
    return reach;
}

float clamp_unit(float value) {
    return value > 1.0f ? 1.0f : value < -1.0f ? -1.0f : value;
}

} // namespace

// ik_bend_two_bones as sv_ik_solve_three calls it
// decomp: ik_bend_two_bones 0xABBE4
void ik_solve_three(const Vec3f& target, const Mat4& top, float length_a, float length_b,
                    float twist, Mat4& joint, Mat4& end, Mat4& tip) {
    const bool mirror = twist >= 0.0f;
    const float reach = aim_x_at(target, top, joint);
    if (length_a + length_b <= reach) {
        // out of reach: the chain straight along the aim
        end = mat_multiply(mat_translation(length_a, 0, 0), joint);
        tip = mat_multiply(mat_translation(length_b, 0, 0), end);
        return;
    }
    // the first link turned off the aim by the law of cosines
    float cosine = clamp_unit((length_a * length_a + reach * reach - length_b * length_b) /
                              (2.0f * length_a * reach));
    float root = std::sqrt(1.0f - cosine * cosine);
    Mat4 bend;
    bend.m[0][0] = cosine;
    bend.m[1][1] = cosine;
    bend.m[0][1] = mirror ? -root : root;
    bend.m[1][0] = mirror ? root : -root;
    joint = mat_multiply(bend, joint);
    // the second link turned back at the knee, then moved out along the first
    cosine = clamp_unit((length_a * length_a + length_b * length_b - reach * reach) /
                        (2.0f * length_a * length_b));
    root = std::sqrt(1.0f - cosine * cosine);
    Mat4 knee;
    knee.m[0][0] = -cosine;
    knee.m[1][1] = -cosine;
    knee.m[0][1] = mirror ? root : -root;
    knee.m[1][0] = mirror ? -root : root;
    knee = mat_multiply(knee, mat_translation(length_a, 0, 0));
    end = mat_multiply(knee, joint);
    tip = mat_multiply(mat_translation(length_b, 0, 0), end);
}

// ik_link_aim as sv_ik_solve_two calls it
// decomp: ik_link_aim 0xAC3B2
void ik_solve_two(const Vec3f& target, const Mat4& top, float length, Mat4& joint, Mat4& end) {
    aim_x_at(target, top, joint);
    end = mat_multiply(mat_translation(length, 0, 0), joint);
}
