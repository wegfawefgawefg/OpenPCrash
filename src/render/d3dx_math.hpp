#pragma once
// The D3DX matrix functions the original calls, in its row-vector convention (v' = v * M,
// translation in the last row), so code read from the decomp transcribes line for line. Floats:
// presentation and asset loading only, never the world. The rotation matrices are the standard
// ones, which is what lets the port run them unchanged in its z-negated frame (docs/assets.md).
struct Vec3f {
    float x = 0, y = 0, z = 0;
};

struct Mat4 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
};

Mat4 mat_identity();
Mat4 mat_multiply(const Mat4& a, const Mat4& b); // a then b: v * a * b
Mat4 mat_translation(float x, float y, float z);
Mat4 mat_scaling(float x, float y, float z);
Mat4 mat_rotation_x(float angle);
Mat4 mat_rotation_y(float angle);
Mat4 mat_rotation_z(float angle);
Mat4 mat_inverse(const Mat4& a);
Vec3f transform_coord(const Vec3f& v, const Mat4& m);
Vec3f transform_normal(const Vec3f& v, const Mat4& m);
Vec3f mat_position(const Mat4& m); // the translation row

float vec_length(const Vec3f& v);
Vec3f vec_add(const Vec3f& a, const Vec3f& b);
Vec3f vec_sub(const Vec3f& a, const Vec3f& b);
Vec3f vec_scale(const Vec3f& a, float s);
Vec3f vec_cross(const Vec3f& a, const Vec3f& b);
float vec_dot(const Vec3f& a, const Vec3f& b);
Vec3f vec_normalize(const Vec3f& a);

// GenkiLib's matrix stack (genkilib_o2.c): every operation multiplies on the local side, so
// translate, rotate z, rotate y, rotate x read as "translate, then turn inside that"
struct MatrixStack {
    Mat4 items[64];
    int top = 0;
};
void stack_push(MatrixStack& stack);
void stack_pop(MatrixStack& stack);
Mat4& stack_top(MatrixStack& stack);
void stack_identity(MatrixStack& stack);
void stack_translate(MatrixStack& stack, float x, float y, float z);
void stack_rotate_x(MatrixStack& stack, float angle);
void stack_rotate_y(MatrixStack& stack, float angle);
void stack_rotate_z(MatrixStack& stack, float angle);
void stack_scale(MatrixStack& stack, float x, float y, float z);
void stack_mult(MatrixStack& stack, const Mat4& m); // matrix_mult: m inside the top
void stack_set(MatrixStack& stack, const Mat4& m);
