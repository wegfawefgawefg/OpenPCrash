#pragma once
// The world's one random generator: retail's rand_table_next, a walk over the 0x7FF
// values of rand_table (data/tables/rand_table.sexp), and rand_float on top
// (decomp docs/port/architecture.md, "Randomness"). The table is constant data loaded once; the
// world holds only the index, so a copied world draws the same numbers.
#include "fx/fx.hpp"

#include <string>

inline constexpr int rand_table_size = 0x7FF;

struct WorldRandom {
    int index = 0; // retail seeds it from the clock at boot (rand_seed_from_clock)
};

struct Pack;
// from the core pack (tables/rand_table.sexp)
bool load_rand_table(const Pack& core, std::string& error);
// rand_table_next: index = (index + 1) % 0x7FF, then that value (12..32766)
int rand_table_next(WorldRandom& random);
// rand_float(range): the next value / 32768 x range, nearest
Fx rand_fx(WorldRandom& random, Fx range);
// (int)rand_float(range) for a whole range, truncated as the original's cast
int rand_int(WorldRandom& random, int range);
