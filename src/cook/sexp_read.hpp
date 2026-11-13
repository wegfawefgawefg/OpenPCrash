#pragma once
// Reading the converted .sexp tables: a whole file parsed, and numbers out of atoms.
#include <cstdlib>
#include <gsexp/sexp.hpp>
#include <string>

bool parse_sexp_file(const std::string& path, gsexp::ParseResult& out, std::string& error);

inline int atom_int(gsexp::Node node) {
    return node.valid()
               ? static_cast<int>(std::strtol(std::string(node.text()).c_str(), nullptr, 0))
               : 0;
}
inline unsigned atom_hex(gsexp::Node node) {
    return node.valid()
               ? static_cast<unsigned>(std::strtoul(std::string(node.text()).c_str(), nullptr, 0))
               : 0;
}
inline float atom_float(gsexp::Node node) {
    return node.valid() ? std::strtof(std::string(node.text()).c_str(), nullptr) : 0.0f;
}
