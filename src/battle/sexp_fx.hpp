#pragma once
// A float atom of a table as Q16.16 for the world: the text read to the nearest single (as the
// extractor wrote it from one), then its bits to Fx by integers only (fx_bits), so every platform
// agrees.
#include "cook/sexp_read.hpp"
#include "fx/fx.hpp"

#include <bit>
#include <cstdint>

inline Fx atom_fx(gsexp::Node node) {
    return fx_bits(std::bit_cast<std::uint32_t>(atom_float(node)));
}
