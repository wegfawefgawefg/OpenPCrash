#pragma once
// Small helpers for writing .sexp text: quoted strings, floats that read back exactly, hex words.
#include <cstdint>
#include <cstdio>
#include <gsexp/sexp.hpp>
#include <string>
#include <string_view>

inline std::string sx_string(std::string_view text) {
    return gsexp::quote_string(text);
}

// nine significant digits: every float reads back to the same bits
inline std::string sx_float(float value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.9g", static_cast<double>(value));
    return text;
}

inline std::string sx_hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%x", value);
    return text;
}

inline std::string sx_int(long long value) {
    return std::to_string(value);
}
