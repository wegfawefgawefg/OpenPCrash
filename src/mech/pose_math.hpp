#pragma once
// The matrix pieces the pose walk needs beyond src/fx, in Q16.16 (src/mech/pose_ik.cpp): a node's
// turns as one matrix, the IK solvers of sv_ik_chain, and the quaternion slerp of the pose blend
// and the arms' raise. Matrices are D3DX's (fx/mat.hpp): a point is a row, a * b applies a first.
#include "fx/mat.hpp"

// the matrix stack's translate, rotate_z, rotate_y, rotate_x (each multiplied on the local side)
// as one local matrix: Rx * Ry * Rz with the translation, each entry summed whole and rounded once
FxMat3 mat3_turns(Angle x, Angle y, Angle z);
inline FxMat4 mat4_turns(FxVec3 translate, Angle x, Angle y, Angle z) {
    return mat4_from(mat3_turns(x, y, z), translate);
}

// matrix_from_dir_up (out, up, dir) as the port frame has it. The original builds
// x = up x dir, y = dir x x, z = dir; mirrored in z, that is the port's mat3_from_dir_up of -dir
// (a cross product flips under a mirror, and the port's z row is the original's negated)
// decomp: matrix_from_dir_up 0xD3D59
inline FxMat3 mat3_from_dir_up_mirrored(FxVec3 dir, FxVec3 up) {
    return mat3_from_dir_up(-dir, up);
}

// sv_ik_solve_three (ik_bend_two_bones): the chain's joint, end and tip from the
// chain root's frame top, the target and the authored link lengths; positive bends one way
void ik_solve_three(FxVec3 target, const FxMat4& top, Fx length_a, Fx length_b, bool positive,
                    FxMat4& joint, FxMat4& end, FxMat4& tip);
// sv_ik_solve_two (ik_link_aim): one link aimed at the target
void ik_solve_two(FxVec3 target, const FxMat4& top, Fx length, FxMat4& joint, FxMat4& end);

// a rotation as a unit quaternion (D3DXQuaternionRotationMatrix's convention) and back
struct FxQuat {
    Fx x, y, z, w;
};
FxQuat quat_from_mat3(const FxMat3& m);
FxMat3 mat3_from_quat(FxQuat q);
// D3DXQuaternionSlerp: a to b by t, the short way round
FxQuat quat_slerp(FxQuat a, FxQuat b, Fx t);
// sv_collision_spheres_select's and sv_pose_weapon_node's blend: the translations lerped, the
// rotations slerped, a to b by t
FxMat4 mat4_blend(const FxMat4& a, const FxMat4& b, Fx t);
