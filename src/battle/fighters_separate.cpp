// fighters_separate and fighter_pair_separate (decomp src/od/u_0b7ea0.c): the
// battle's first act each update, pushing overlapping fighters apart through Actor.push, which
// the integrate adds to the move. Retail pushes by the pose's contact sets (contact_sets_push and
// its swept form); a fighter whose set is empty takes the sphere push below. The port's fighters
// have no contact sets until the pose walk runs in the step, so every pair takes the sphere push.
#include "battle/fighters_separate.hpp"

#include "ai/ai_links.hpp"
#include "world/world.hpp"

namespace {

// fighter_pair_separate with an empty contact set: the overlap of the two radii along the line
// between them, split in half (all to one side when the other is dead; scenes aren't ported)
void pair_separate(World& world, int a, int b) {
    Fighter& one = world.fighters[static_cast<std::size_t>(a)];
    Fighter& two = world.fighters[static_cast<std::size_t>(b)];
    FxVec3 v = one.position - two.position;
    Fx distance = length(v);
    const Fx radii = one.radius + two.radius;
    if (distance == Fx{}) {
        distance = fx_div_near(radii, fx(2));
        v = {distance, Fx{}, Fx{}};
    }
    if (!(radii > distance)) {
        return;
    }
    const FxVec3 push = scaled_to(v, radii - distance);
    const bool one_down = fighter_durability(world, a) == 0;
    const bool two_down = fighter_durability(world, b) == 0;
    if (one_down) {
        two.push -= push;
    } else if (two_down) {
        one.push += push;
    } else {
        const FxVec3 half = scaled_to(v, fx_div_near(radii - distance, fx(2)));
        one.push += half;
        two.push -= half;
    }
}

} // namespace

// decomp: fighters_separate 0xB7EA0
void fighters_separate(World& world) {
    for (Fighter& f : world.fighters) {
        if (f.present) {
            f.push = {};
        }
    }
    for (int a = 0; a < fighter_slots; ++a) {
        for (int b = a + 1; b < fighter_slots; ++b) {
            if (world.fighters[static_cast<std::size_t>(a)].present &&
                world.fighters[static_cast<std::size_t>(b)].present) {
                pair_separate(world, a, b);
            }
        }
    }
    // capped at the radius an update; R14: half a radius a step (docs/sixty.md, "Integration,
    // contact, separation")
    for (Fighter& f : world.fighters) {
        if (f.present) {
            f.push = clamp_length(f.push, fx_div_near(f.radius, fx(2)));
        }
    }
}
