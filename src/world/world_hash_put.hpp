#pragma once
// The world hash's pieces (world_hash.cpp, world_hash_save.cpp): a 64-bit word mixer and `put`
// for every kind of field, so each struct's hash is a plain list of its fields. Integers and
// enums go in as their value, bools as 0 or 1, Fx and Angle as their raw bits, arrays element by
// element; pointers have no `put`, so one can't slip in. Each struct's size is checked next to
// its put: a new field changes it, and the hash wants that field too.
#include "fx/angle.hpp"
#include "fx/fx.hpp"
#include "fx/hit.hpp"
#include "fx/mat.hpp"
#include "fx/vec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

struct Hasher {
    std::uint64_t h = 0xcbf29ce484222325ull;
    void word(std::uint64_t v) {
        h = (h ^ v) * 0x9e3779b97f4a7c15ull;
        h ^= h >> 32;
    }
};

inline void put(Hasher& h, bool v) {
    h.word(v ? 1 : 0);
}
template <class T>
    requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
void put(Hasher& h, T v) {
    if constexpr (std::is_same_v<T, char>) {
        // plain char is signed on x86-64 and unsigned on ARM64: hash its byte
        h.word(static_cast<unsigned char>(v));
    } else {
        h.word(static_cast<std::uint64_t>(static_cast<std::int64_t>(v)));
    }
}
template <class T>
    requires std::is_enum_v<T>
void put(Hasher& h, T v) {
    put(h, static_cast<std::underlying_type_t<T>>(v));
}
inline void put(Hasher& h, Fx v) {
    h.word(static_cast<std::uint32_t>(v.raw));
}
inline void put(Hasher& h, Angle v) {
    h.word(v.raw);
}
inline void put(Hasher& h, const FxVec3& v) {
    put(h, v.x), put(h, v.y), put(h, v.z);
}
inline void put(Hasher& h, const FxMat3& m) {
    put(h, m.x), put(h, m.y), put(h, m.z);
}
inline void put(Hasher& h, const FxMat4& m) {
    put(h, m.x), put(h, m.y), put(h, m.z), put(h, m.w);
}
inline void put(Hasher& h, const FxTri& t) {
    put(h, t.v0), put(h, t.v1), put(h, t.v2), put(h, t.normal);
}
template <class T, std::size_t N> void put(Hasher& h, const T (&a)[N]) {
    for (const T& x : a) {
        put(h, x);
    }
}
template <class T, std::size_t N> void put(Hasher& h, const std::array<T, N>& a) {
    for (const T& x : a) {
        put(h, x);
    }
}
template <class... T> void put_all(Hasher& h, const T&... fields) {
    (put(h, fields), ...);
}

// the save's part of the world (world_hash_save.cpp)
struct Story;
struct Garages;
struct Profile;
struct RumbleSlot;
struct ButtonConfig;
struct BuildRecord;
struct OpponentTicket;
void put(Hasher& h, const Story& s);
void put(Hasher& h, const Garages& g);
void put(Hasher& h, const Profile& p);
void put(Hasher& h, const RumbleSlot& r);
void put(Hasher& h, const ButtonConfig& b);
void put(Hasher& h, const BuildRecord& b);
void put(Hasher& h, const OpponentTicket& t);
