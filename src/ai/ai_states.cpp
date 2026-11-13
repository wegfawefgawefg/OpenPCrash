// The AI's states 0..6, the ram and the crate chase (docs/port/ai.md "State machine", "Special
// moves"): the handlers of ai_state_handlers and the ones special moves install, one
// switch. A change made inside a handler runs its entry next update; the rest of the update goes
// on.
#include "ai/ai_internal.hpp"
#include "ai/ai_links.hpp"
#include "fx/mat.hpp"
#include "mech/fighter_rates.hpp"

namespace {

// (int)(r / 32767.0f x 3) % 3: a mode with no reader, one draw
int mode_draw(Ai& ai) {
    return static_cast<int>(static_cast<std::int64_t>(ai_draw(ai)) * 3 / 32767) % 3;
}

void decide(Ai& ai) {
    const int next = ai_choose_next_state(ai);
    if (next >= 0) {
        ai_change_state(ai, next);
    }
}

void zero_axes(AiController& c) {
    c.axis_forward = c.axis_leg_turn = c.axis_pitch = c.axis_twist = Fx{};
}

// buttons 2 and 4 while it wants to jump or is jumping, else 4 let go
void jump_with_wants(Ai& ai) {
    if (ai.c.wants_jump || motion_jumping(ai)) {
        press(ai, control_jump_held | control_jump_pressed);
    } else {
        release(ai, control_jump_pressed);
    }
}

// state 0, ai_state_stand_fire and its update ai_state_stand_fire_update
// decomp: ai_state_stand_fire 0x588B8
void stand_fire(Ai& ai) {
    AiController& c = ai.c;
    c.axis_forward = Fx{};
    c.mode = mode_draw(ai);
    c.sub_timer = c.state_timer = c.reeval_timer = static_cast<std::int16_t>(ai_cadence * 2);
    ai_fire_arm(ai);
    press(ai, control_recentre);
    c.handler = AiHandler::stand_fire_update;
}
void stand_fire_update(Ai& ai) {
    AiController& c = ai.c;
    ai_evade_buttons(ai);
    if (c.target >= 0) {
        ai_aim_at_target(ai, aim_twist | aim_legs);
        if (c.target >= 0) {
            ai_want_fire(ai);
        }
    } else {
        --c.sub_timer;
        if (c.sub_timer < 0) {
            c.mode = mode_draw(ai);
        }
    }
    --c.state_timer;
    decide(ai);
}

// state 1, ai_state_route_follow, ai_route_follow_step
// decomp: ai_state_route_follow 0x58A63, ai_route_follow_step 0x58B4A
void route_follow(Ai& ai) {
    ai_route_choose_ahead(ai, false);
    // the controller's address + 0x7F where the level was meant: negative (kept)
    ai.c.state_timer = ai_address_bug_timer(ai.slot);
    press(ai, control_recentre);
    ai.c.handler = AiHandler::route_follow_update;
}
void route_follow_update(Ai& ai) {
    AiController& c = ai.c;
    ai_route_step(ai, 0, 3);
    ai_route_steer(ai);
    const int next = ai_choose_next_state(ai);
    if (next >= 0) {
        ai_change_state(ai, next);
        return;
    }
    if (c.target >= 0) {
        // twist and pitch only: the legs belong to the route
        ai_aim_at_target(ai, aim_twist);
        if (c.target >= 0) {
            ai_want_fire(ai);
        }
    }
    --c.state_timer;
}

// states 2 and 4: nothing asks for them, ported for completeness
void facing_pause(Ai& ai) {
    ai.c.spare_1c = ai_draw(ai);
    zero_axes(ai.c);
    ai_fire_arm(ai);
    ai.c.handler = AiHandler::facing_pause_update;
    ai_run_handler(ai);
}
void neutral_pause(Ai& ai) {
    ai.c.mode = 0;
    zero_axes(ai.c);
    ai_fire_arm(ai);
    ai.c.handler = AiHandler::neutral_pause_update;
    ai_run_handler(ai);
}
void pause_update(Ai& ai) {
    ai_want_fire(ai);
    --ai.c.state_timer;
    decide(ai);
}

// state 3, ai_state_combat_maneuver and its update ai_state_combat_maneuver_update
// decomp: ai_state_combat_maneuver 0x58CE9
void combat(Ai& ai) {
    AiController& c = ai.c;
    // the mode cycles with the update it was entered on; a blade rolls 220 of 255 for a charge
    c.mode = c.age_frames % 5;
    if (ai_weapon_class6_ready(ai) && ai_roll(ai, 0xDC, 0xFF)) {
        c.mode = 4;
    } else if (c.mode == 4) {
        c.mode = 3;
    }
    c.sub_timer = static_cast<std::int16_t>(ai_cadence * 3);
    zero_axes(c);
    ai_fire_arm(ai);
    c.handler = AiHandler::combat_update;
    ai_route_choose_ahead(ai, false);
    ai_run_handler(ai);
}
void combat_update(Ai& ai) {
    AiController& c = ai.c;
    if (c.target < 0) {
        c.handler = AiHandler::combat_lost_target;
        return;
    }
    ai_aim_at_target(ai, aim_twist | aim_legs);
    jump_with_wants(ai);
    ai_want_fire(ai);
    if (c.sub_timer <= 0) {
        ai_evade_buttons(ai);
        c.sub_timer = static_cast<std::int16_t>(ai_cadence * 3);
    }
    if (c.mode == 4) {
        // the charge: in to 12
        c.axis_forward = c.target_distance_flat > fx(12) ? fx(1) : Fx{};
    } else if (c.mode == 3) {
        ai_route_step(ai, 0, 3);
        ai_route_steer(ai);
    }
    if (c.sub_timer > 0) {
        --c.sub_timer;
    }
    --c.state_timer;
    decide(ai);
}

// a search move from the weights, a spin one way
// decomp: ai_combat_lost_target 0x58FE5
void combat_lost_target(Ai& ai) {
    AiController& c = ai.c;
    c.search_move = ai_weighted_pick(ai);
    if (c.search_move < 4) {
        c.reeval_timer = c.state_timer = static_cast<std::int16_t>(ai_cadence * 2);
    }
    ai_fire_stop_all(ai);
    c.axis_leg_turn = (ai_draw(ai) & 1) ? fx(-1) : fx(1);
    switch (c.search_move) {
    case 0:
        ai_route_choose_ahead(ai, false);
        c.axis_forward = fx(1);
        break;
    case 1:
        c.axis_forward = (ai_draw(ai) & 1) ? fx(1) : fx(-1);
        press(ai, control_jump_held | control_jump_pressed);
        break;
    case 2:
        if (c.age_frames & 1) {
            release(ai, control_step_right);
            press(ai, control_step_left);
        } else {
            press(ai, control_step_right);
            release(ai, control_step_left);
        }
        break;
    case 3:
        c.state_timer = static_cast<std::int16_t>(ai_cadence * 2);
        break;
    default:
        c.search_move = 4;
        ai_route_choose_ahead(ai, false);
        break;
    }
    c.axis_pitch = fx(1);
    c.handler = AiHandler::combat_search_update;
    ai_run_handler(ai);
}
void combat_search_update(Ai& ai) {
    AiController& c = ai.c;
    const HudSense* sense = ai_target_sense(ai);
    if (c.target >= 0 && sense && !sense->occluded) {
        c.handler = AiHandler::combat_update;
        return;
    }
    if (c.search_move >= 4) {
        ai_route_step(ai, 0, 3);
        ai_route_steer(ai);
    }
    // the pitch sweeps between +-pi/4
    const Fx pitch = camera_pitch_original(ai);
    if (c.axis_pitch < Fx{}) {
        if (pitch >= fx_f(0.7853982)) {
            c.axis_pitch = fx(1);
        }
    } else if (pitch <= fx_f(-0.7853982)) {
        c.axis_pitch = fx(-1);
    }
    --c.state_timer;
    decide(ai);
}

// state 5, ai_state_back_off and its update ai_state_back_off_update
// decomp: ai_state_back_off 0x59CE2
void back_off(Ai& ai) {
    ai_fire_arm(ai);
    ai.c.handler = AiHandler::back_off_update;
    ai_run_handler(ai);
}
void back_off_update(Ai& ai) {
    AiController& c = ai.c;
    release(ai, control_dash | control_jump_pressed | control_jump_held);
    c.axis_leg_turn = c.axis_pitch = c.axis_twist = Fx{};
    if (ai_level(ai) > 0) {
        c.axis_forward = fx(-1);
        ai_aim_at_target(ai, aim_twist | aim_legs);
        if (c.target >= 0) {
            ai_want_fire(ai);
        }
    } else {
        c.axis_forward = fx(1);
        ai_aim_at_offset(ai);
    }
    ai_evade_buttons(ai);
    --c.state_timer;
    decide(ai);
}

// state 6, ai_state_wander: out of ammo, toward the door paths, never deciding again
// decomp: ai_state_wander 0x59E29
void wander(Ai& ai) {
    ai_route_choose_ahead(ai, false);
    ai.c.handler = AiHandler::wander_update;
}
void wander_update(Ai& ai) {
    AiController& c = ai.c;
    ai_route_step(ai, 2, 0);
    if (c.target >= 0) {
        ai_aim_at_target(ai, aim_twist);
        ai_want_fire(ai);
    }
    ai_route_steer(ai);
}

// the ram: ai_ram_start, ai_ram_jump, ai_ram_coast
void ram_start(Ai& ai) {
    AiController& c = ai.c;
    c.state_timer = 0;
    c.mode = 0;
    zero_axes(c);
    press(ai, control_jump_held | control_jump_pressed);
    ai_fire_arm(ai);
    c.handler = AiHandler::ram_jump;
}
void ram_end(Ai& ai) {
    ai.c.state_timer = 0;
    ai.c.move_flags &= ~move_ram;
    ai_change_state(ai, 1);
}
void ram_jump(Ai& ai) {
    AiController& c = ai.c;
    if (c.target < 0) {
        ram_end(ai);
        return;
    }
    ai_want_fire(ai);
    c.axis_forward = fx(1);
    ai_aim_at_target(ai, aim_twist | aim_legs);
    if (motion_jumping(ai)) {
        press(ai, control_jump_held | control_jump_pressed);
        return;
    }
    if (motion_falling(ai)) {
        press(ai, control_jump_held);
        return;
    }
    release(ai, control_jump_pressed);
    c.state_timer = static_cast<std::int16_t>(ai_cadence * 3);
    c.handler = AiHandler::ram_coast;
}
void ram_coast(Ai& ai) {
    AiController& c = ai.c;
    if (c.target >= 0) {
        ai_aim_at_target(ai, aim_twist | aim_legs);
    }
    --c.state_timer;
    if (c.state_timer < 0) {
        ram_end(ai);
    }
}

// the crate chase: ai_fetch_pickup_start, ai_fetch_pickup
void fetch_pickup_start(Ai& ai) {
    AiController& c = ai.c;
    c.state_timer = 0;
    c.mode = 0;
    zero_axes(c);
    press(ai, control_recentre);
    ai_fire_arm(ai);
    c.handler = AiHandler::fetch_pickup;
    ai_run_handler(ai);
}
void fetch_pickup(Ai& ai) {
    AiController& c = ai.c;
    release(ai, control_dash);
    const int state = c.pickup >= 0 ? pickup_state(ai.world, c.pickup) : 0;
    if (c.pickup < 0 || state == 0 || state == 3 || state == 4 ||
        c.pickup_frames > ai_cadence * 5) {
        c.pickup = -1;
        c.move_flags &= ~move_crate_chase;
        ai_change_state(ai, 1);
        return;
    }
    const FxVec3 crate = pickup_position(ai.world, c.pickup);
    ai_steer_to_point(ai, crate);
    jump_with_wants(ai);
    ++c.pickup_frames;
    const FxVec3 look = ai_look_direction(ai);
    const FxVec3 to = crate - ai.f.position;
    const Fx dot_look = dot(normalized(to), look);
    // close under it and it's well above: jump for it (D_0017AAE0 1 + radius - 0.5)
    if (shorter_than(to, ai.f.radius + fx_f(0.5)) && crate.y > ai.f.position.y + fx(3)) {
        c.axis_forward = Fx{};
        press(ai, control_jump_held | control_jump_pressed);
        return;
    }
    if (fx_cos(angle_f_radians(0.2617994)) < dot_look) {
        if (fighter_role(ai.world, ai.slot) >= 2) {
            press(ai, control_dash);
        }
        c.axis_forward = fx(1);
        return;
    }
    if (fx_cos(angle_f_radians(0.7853982)) < dot_look) {
        c.axis_forward = shorter_than(to, fx(5)) ? Fx{} : fx_bits(0x3F30A3D7);
        return;
    }
    c.axis_forward = Fx{};
}

// Shibuya's zone map, which the ram check reads on every stage (kept);
// 1 is street level. The original's z is the port's negated
// decomp: shibuya_zone_at 0x6E2B1
int shibuya_zone_at(FxVec3 p) {
    const FxVec2 across{p.x - fx_f(30.522), -p.z - fx_f(59.5941)};
    if (shorter_than(across, fx_f(39.295))) {
        if (p.y < fx(-25)) {
            return 2;
        }
        return p.y < fx_f(-0.5) ? 0 : 1;
    }
    if (p.y > fx_f(-2.5)) {
        return 1;
    }
    return p.y > fx(-40) ? 3 : 4;
}

} // namespace

void ai_run_handler(Ai& ai) {
    if (ai_run_ranker_handler(ai)) {
        return;
    }
    switch (ai.c.handler) {
    case AiHandler::stand_fire:
        stand_fire(ai);
        break;
    case AiHandler::stand_fire_update:
        stand_fire_update(ai);
        break;
    case AiHandler::route_follow:
        route_follow(ai);
        break;
    case AiHandler::route_follow_update:
        route_follow_update(ai);
        break;
    case AiHandler::facing_pause:
        facing_pause(ai);
        break;
    case AiHandler::facing_pause_update:
        pause_update(ai);
        break;
    case AiHandler::combat:
        combat(ai);
        break;
    case AiHandler::combat_update:
        combat_update(ai);
        break;
    case AiHandler::combat_lost_target:
        combat_lost_target(ai);
        break;
    case AiHandler::combat_search_update:
        combat_search_update(ai);
        break;
    case AiHandler::neutral_pause:
        neutral_pause(ai);
        break;
    case AiHandler::neutral_pause_update:
        pause_update(ai);
        break;
    case AiHandler::back_off:
        back_off(ai);
        break;
    case AiHandler::back_off_update:
        back_off_update(ai);
        break;
    case AiHandler::wander:
        wander(ai);
        break;
    case AiHandler::wander_update:
        wander_update(ai);
        break;
    case AiHandler::ram_start:
        ram_start(ai);
        break;
    case AiHandler::ram_jump:
        ram_jump(ai);
        break;
    case AiHandler::ram_coast:
        ram_coast(ai);
        break;
    case AiHandler::fetch_pickup_start:
        fetch_pickup_start(ai);
        break;
    case AiHandler::fetch_pickup:
        fetch_pickup(ai);
        break;
    default:
        break;
    }
}

// decomp: ai_special_moves 0x5941B
void ai_special_moves(Ai& ai) {
    AiController& c = ai.c;
    ai_ranker_gate(ai);
    if (c.move_flags & move_ram) {
        return;
    }
    bool begun = false;
    // the ram: not on Shibuya, and on every stage only where Shibuya's map isn't street level;
    // no blade, on the ground, a target 5..20 away within 15 degrees of the look, 15/255 a try
    const int selector = ai.world.story.rumble_selected;
    if (selector != 2 && selector != 3 && shibuya_zone_at(ai.f.position) != 1 &&
        !ai_weapon_class6_ready(ai) && !motion_jumping(ai) && !motion_falling(ai) &&
        c.target >= 0 && ai_roll(ai, 0xF, 0xFF)) {
        const FxVec3 look = ai_look_direction(ai);
        const FxVec3 to =
            ai.world.fighters[static_cast<std::size_t>(c.target)].position - ai.f.position;
        const Fx distance = length(to);
        if (distance < fx(20) && distance > fx(5) &&
            fx_cos(angle_f_radians(0.2617994)) < dot(look, normalized(to))) {
            begun = true;
            c.move_flags = move_ram;
            c.handler = AiHandler::ram_start;
        }
    }
    if (c.move_flags & move_any) {
        return;
    }
    // no target: the nearest crate within 25 (score crates left alone above level 30, but
    // chased in a demo)
    if (!begun && c.target < 0) {
        c.pickup = ai_pickup_nearest(ai.world, ai.f.position, ai.slot, fx(25));
        if (c.pickup >= 0) {
            if (ai.world.battle_phase != 1 && ai_level(ai) > 0x1E &&
                pickup_kind(ai.world, c.pickup) <= 3) {
                return;
            }
            c.pickup_frames = 0;
            c.move_flags |= move_crate_chase;
            c.handler = AiHandler::fetch_pickup_start;
        }
    }
}
