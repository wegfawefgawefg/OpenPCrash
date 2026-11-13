// The root track's move (decomp src/hand/u_088880.c motion_root_delta): what a clip's
// root curves carry the fighter between two phases, for the motion handlers (fighter_motion.cpp)
// and the hurt states (fighter_hurt.cpp).
#include "mech/fighter.hpp"
#include "mech/fighter_rates.hpp"

namespace {

// the root track's x (but in walk and run states) and z at a phase
// decomp: motion_root_sample 0x90F0A
FxVec3 root_sample(const Fighter& f, const RootCurves& root, Fx phase) {
    const bool keeps_x =
        !state_in(f.motion_state, {state_walk, state_walk_back, state_run, state_run_reverse});
    return {keeps_x ? fx_curve_value(root.x, phase) : Fx{}, Fx{}, fx_curve_value(root.z, phase)};
}

} // namespace

// the root track's move between the last phase and this one, across
// the loop when the phase wrapped
// decomp: motion_root_delta 0x91054
FxVec3 motion_root_delta(const Fighter& f, const BattleData& data) {
    const RootCurves& root = data.legs[f.leg_class].root[f.motion_slot_a];
    if (!root.present) {
        return {};
    }
    const FxVec3 now = root_sample(f, root, f.motion_phase);
    const FxVec3 before = root_sample(f, root, f.motion_phase_prev);
    if (f.motion_phase < f.motion_phase_prev) {
        return (now - root_sample(f, root, f.timing.loop_start)) +
               (root_sample(f, root, f.timing.loop_end) - before);
    }
    return now - before;
}
