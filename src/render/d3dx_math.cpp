#include "render/d3dx_math.hpp"

#include <cmath>
#include <utility>

Mat4 mat_identity() {
    return Mat4{};
}

Mat4 mat_multiply(const Mat4& a, const Mat4& b) {
    Mat4 out;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c] +
                          a.m[r][3] * b.m[3][c];
        }
    }
    return out;
}

Mat4 mat_translation(float x, float y, float z) {
    Mat4 out;
    out.m[3][0] = x;
    out.m[3][1] = y;
    out.m[3][2] = z;
    return out;
}

Mat4 mat_scaling(float x, float y, float z) {
    Mat4 out;
    out.m[0][0] = x;
    out.m[1][1] = y;
    out.m[2][2] = z;
    return out;
}

Mat4 mat_rotation_x(float angle) {
    Mat4 out;
    const float c = std::cos(angle), s = std::sin(angle);
    out.m[1][1] = c;
    out.m[1][2] = s;
    out.m[2][1] = -s;
    out.m[2][2] = c;
    return out;
}

Mat4 mat_rotation_y(float angle) {
    Mat4 out;
    const float c = std::cos(angle), s = std::sin(angle);
    out.m[0][0] = c;
    out.m[0][2] = -s;
    out.m[2][0] = s;
    out.m[2][2] = c;
    return out;
}

Mat4 mat_rotation_z(float angle) {
    Mat4 out;
    const float c = std::cos(angle), s = std::sin(angle);
    out.m[0][0] = c;
    out.m[0][1] = s;
    out.m[1][0] = -s;
    out.m[1][1] = c;
    return out;
}

// Gauss-Jordan with partial pivoting; a singular matrix comes back as the identity
Mat4 mat_inverse(const Mat4& a) {
    double w[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            w[r][c] = a.m[r][c];
            w[r][c + 4] = r == c ? 1.0 : 0.0;
        }
    }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r) {
            if (std::fabs(w[r][c]) > std::fabs(w[pivot][c])) {
                pivot = r;
            }
        }
        if (std::fabs(w[pivot][c]) < 1e-20) {
            return Mat4{};
        }
        for (int k = 0; k < 8; ++k) {
            std::swap(w[c][k], w[pivot][k]);
        }
        const double divisor = w[c][c];
        for (int k = 0; k < 8; ++k) {
            w[c][k] /= divisor;
        }
        for (int r = 0; r < 4; ++r) {
            if (r == c) {
                continue;
            }
            const double factor = w[r][c];
            for (int k = 0; k < 8; ++k) {
                w[r][k] -= factor * w[c][k];
            }
        }
    }
    Mat4 out;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out.m[r][c] = static_cast<float>(w[r][c + 4]);
        }
    }
    return out;
}

Vec3f transform_coord(const Vec3f& v, const Mat4& m) {
    const float x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0];
    const float y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1];
    const float z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2];
    const float w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3];
    return w != 0 ? Vec3f{x / w, y / w, z / w} : Vec3f{x, y, z};
}

Vec3f transform_normal(const Vec3f& v, const Mat4& m) {
    return {v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
            v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
            v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2]};
}

Vec3f mat_position(const Mat4& m) {
    return {m.m[3][0], m.m[3][1], m.m[3][2]};
}

float vec_length(const Vec3f& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
Vec3f vec_add(const Vec3f& a, const Vec3f& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3f vec_sub(const Vec3f& a, const Vec3f& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3f vec_scale(const Vec3f& a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
Vec3f vec_cross(const Vec3f& a, const Vec3f& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float vec_dot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3f vec_normalize(const Vec3f& a) {
    const float length = vec_length(a);
    return length > 0 ? vec_scale(a, 1.0f / length) : a;
}

void stack_push(MatrixStack& stack) {
    if (stack.top < 63) {
        stack.items[stack.top + 1] = stack.items[stack.top];
        ++stack.top;
    }
}
void stack_pop(MatrixStack& stack) {
    if (stack.top > 0) {
        --stack.top;
    }
}
Mat4& stack_top(MatrixStack& stack) {
    return stack.items[stack.top];
}
void stack_identity(MatrixStack& stack) {
    stack_top(stack) = Mat4{};
}
void stack_translate(MatrixStack& stack, float x, float y, float z) {
    stack_top(stack) = mat_multiply(mat_translation(x, y, z), stack_top(stack));
}
void stack_rotate_x(MatrixStack& stack, float angle) {
    stack_top(stack) = mat_multiply(mat_rotation_x(angle), stack_top(stack));
}
void stack_rotate_y(MatrixStack& stack, float angle) {
    stack_top(stack) = mat_multiply(mat_rotation_y(angle), stack_top(stack));
}
void stack_rotate_z(MatrixStack& stack, float angle) {
    stack_top(stack) = mat_multiply(mat_rotation_z(angle), stack_top(stack));
}
void stack_scale(MatrixStack& stack, float x, float y, float z) {
    stack_top(stack) = mat_multiply(mat_scaling(x, y, z), stack_top(stack));
}
void stack_mult(MatrixStack& stack, const Mat4& m) {
    stack_top(stack) = mat_multiply(m, stack_top(stack));
}
void stack_set(MatrixStack& stack, const Mat4& m) {
    stack_top(stack) = m;
}
