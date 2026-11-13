#include "world/world_random.hpp"

#include "assets/pack_sexp.hpp"
#include "cook/sexp_read.hpp"

#include <array>

namespace {

// constant after load_rand_table
std::array<int, rand_table_size> rand_table{};

} // namespace

bool load_rand_table(const Pack& core, std::string& error) {
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, "tables/rand_table.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node form = parsed.root(i);
        if (!form.is_list() || !form.head().is_atom("rand-table")) {
            continue;
        }
        if (form.child_count() != rand_table_size + 1) {
            error = "rand_table.sexp: not 0x7FF values";
            return false;
        }
        for (int k = 0; k < rand_table_size; ++k) {
            rand_table[k] = atom_int(form.child_at(1 + k));
        }
        return true;
    }
    error = "rand_table.sexp: no rand-table (pc_extract --only stage-props)";
    return false;
}

// decomp: rand_table_next 0x44685
int rand_table_next(WorldRandom& random) {
    random.index = (random.index + 1) % rand_table_size;
    return rand_table[random.index];
}

Fx rand_fx(WorldRandom& random, Fx range) {
    const std::int64_t value = rand_table_next(random);
    return fx_saturate(shift_round(value * range.raw, 15));
}

int rand_int(WorldRandom& random, int range) {
    // value * range / 32768 truncated: both positive, so a floor
    return static_cast<int>(static_cast<std::int64_t>(rand_table_next(random)) * range >> 15);
}
