// The rankers and fixed opponents (docs/port/ai.md "Rankers and fixed opponents"):
// ai_ranker_setup, ai_ranker_gate, ai_ranker_update and the handlers it
// installs over the state's own without changing the state.
#include "ai/ai_internal.hpp"
#include "ai/ai_links.hpp"

namespace {

int opponent_id(const Ai& ai) {
    return battle_roster_entry(ai.world, ai.slot).ticket.opponent;
}

bool is_ranker(int id) {
    return id == 0 || id == 1 || id == 10 || id == 19;
}

// updates since slot 0 last lost durability, to anyone
// decomp: ai_track_player_hits 0x5EE93
void ai_track_player_hits(Ai& ai) {
    AiController& c = ai.c;
    if (!ai.world.fighters[0].present) {
        return;
    }
    c.frames_since_player_hit =
        c.frames_since_player_hit + 1 > 0xFFFF ? 0xFFFF : c.frames_since_player_hit + 1;
    const int durability = fighter_durability(ai.world, 0);
    if (durability < c.player_last_durability) {
        c.frames_since_player_hit = 0;
    }
    c.player_last_durability = durability;
}

void ranker_update(Ai& ai) {
    AiController& c = ai.c;
    ai_track_player_hits(ai);
    --c.ranker_timer;
    c.ranker_timer = static_cast<std::int16_t>(c.ranker_timer < -10000  ? -10000
                                               : c.ranker_timer > 20000 ? 20000
                                                                        : c.ranker_timer);
    if ((c.move_flags & move_any) || c.ranker_timer > 0) {
        return;
    }
    // by durability band: enraged under 40%; free weapon classes accumulate as it's hurt
    int next = 0;
    switch (c.durability_band) {
    case 0:
    case 1:
        c.enraged = true;
        next = c.target >= 0 ? 3 : 0;
        break;
    case 2:
        c.free_weapon_classes |= 4 | 8;
        next = c.target >= 0 ? 3 : 0;
        break;
    case 3:
        c.free_weapon_classes |= 2;
        next = c.target >= 0 ? 3 : 0;
        break;
    case 4:
        c.free_weapon_classes |= 1;
        next = c.target >= 0 && ai_roll(ai, 0x28, 0xFF) ? 3 : 0;
        break;
    default:
        break;
    }
    // three minutes alive with a target
    if (static_cast<unsigned>(c.age_frames) > static_cast<unsigned>(ai_cadence * 60 * 3) &&
        c.target >= 0) {
        c.enraged = true;
    }
    // modes 1 and 2 (ai_ranker_mode_aim, _hold) are never chosen
    if (next == 0) {
        c.ranker_timer = static_cast<std::int16_t>(ai_cadence << 3);
        c.handler = (c.age_frames & 1) ? AiHandler::ranker_attack : AiHandler::ranker_route;
    } else {
        c.ranker_timer = static_cast<std::int16_t>(ai_cadence << 2);
        c.handler = AiHandler::ranker_attack;
    }
}

void to_route(AiController& c) {
    c.state_timer = 0;
    c.ranker_timer = static_cast<std::int16_t>(ai_cadence << 3);
    c.handler = AiHandler::ranker_route;
}

// ai_ranker_route and its update ai_ranker_route_update
// decomp: ai_ranker_route 0x5E414
void ranker_route(Ai& ai) {
    AiController& c = ai.c;
    ai_route_choose_ahead(ai, false);
    c.state_timer = ai_address_bug_timer(ai.slot);
    c.ranker_timer = static_cast<std::int16_t>(ai_cadence << 3);
    press(ai, control_recentre);
    ai_fire_arm(ai);
    c.handler = AiHandler::ranker_route_update;
}
void ranker_route_update(Ai& ai) {
    AiController& c = ai.c;
    ai_route_step(ai, 0, 3);
    ai_route_steer(ai);
    if (c.target >= 0) {
        ai_aim_at_target(ai, aim_twist);
        ai_want_fire(ai);
        release(ai, control_recentre);
        if (ai_roll(ai, 0xA, 0xFF)) {
            c.handler = AiHandler::ranker_attack;
        }
        return;
    }
    press(ai, control_recentre);
}

// ai_ranker_attack and its update ai_ranker_attack_update
// decomp: ai_ranker_attack 0x5E5E0
void ranker_attack(Ai& ai) {
    AiController& c = ai.c;
    ai_route_choose_ahead(ai, false);
    c.state_timer = ai_address_bug_timer(ai.slot);
    press(ai, control_recentre);
    ai_fire_arm(ai);
    c.attack_frames = 0;
    c.attack_jumps = 0;
    c.ranker_timer = static_cast<std::int16_t>(ai_cadence << 3);
    c.frames_since_player_hit = 0;
    c.handler = AiHandler::ranker_attack_update;
}
void ranker_attack_update(Ai& ai) {
    AiController& c = ai.c;
    release(ai, control_dash);
    if (c.wants_jump) {
        press(ai, control_jump_pressed);
        ++c.attack_jumps;
    } else {
        release(ai, control_jump_pressed);
    }
    if (!((c.target >= 0 && c.attack_jumps < 3) ||
          c.frames_since_player_hit > static_cast<int>(ai_cadence * 3.5))) {
        c.handler = AiHandler::ranker_route;
        return;
    }
    ai_aim_at_target(ai, aim_twist | aim_legs);
    ai_want_fire(ai);
    const HudSense* sense = ai_target_sense(ai);
    if (!sense) {
        return;
    }
    if (!sense->occluded) {
        c.attack_frames = 0;
        c.axis_forward = Fx{};
        if (c.target_distance_flat > fx(18)) {
            press(ai, control_dash);
            c.axis_forward = fx(1);
        } else if (ai_roll(ai, c.strafe_chance, 0xFF)) {
            c.handler = AiHandler::ranker_strafe;
        }
        return;
    }
    // hidden: step aside at 18 and 48 updates ((int)(60 x 0.3), (int)(60 x 0.8)), back to the
    // route after 60
    c.axis_forward = Fx{};
    if (c.attack_frames == 0) {
        release(ai, control_jump_held | control_jump_pressed);
    } else if (c.attack_frames == 18 || c.attack_frames == 48) {
        if (c.age_frames & 1) {
            release(ai, control_step_right);
            press(ai, control_step_left);
        } else {
            press(ai, control_step_right);
            release(ai, control_step_left);
        }
    }
    if (c.attack_frames < static_cast<std::int16_t>(ai_cadence << 1)) {
        ++c.attack_frames;
    } else {
        c.handler = AiHandler::ranker_route;
    }
}

// ai_ranker_strafe and its update ai_ranker_strafe_update
// decomp: ai_ranker_strafe 0x5E95B
void ranker_strafe(Ai& ai) {
    AiController& c = ai.c;
    c.mode = c.age_frames % 5;
    c.sub_timer = static_cast<std::int16_t>(ai_cadence * 3);
    c.axis_forward = c.axis_leg_turn = c.axis_pitch = c.axis_twist = Fx{};
    ai_fire_disarm(ai);
    c.handler = AiHandler::ranker_strafe_update;
    c.strafe_dir = (c.age_frames & 1) != 0;
}
void ranker_strafe_update(Ai& ai) {
    AiController& c = ai.c;
    if (c.target < 0) {
        to_route(c);
        return;
    }
    ai_aim_at_target(ai, aim_twist | aim_legs);
    if (c.wants_jump || c.target_distance_flat > fx(25)) {
        to_route(c);
    } else if (c.strafe_dir) {
        press(ai, control_step_right);
        release(ai, control_step_left);
    } else {
        release(ai, control_step_right);
        press(ai, control_step_left);
    }
    ai_want_fire(ai);
    c.axis_forward = Fx{};
}

// the unreachable modes 1 and 2, ported for completeness
void ranker_mode_aim(Ai& ai) {
    AiController& c = ai.c;
    c.mode = c.age_frames % 5;
    c.sub_timer = static_cast<std::int16_t>(ai_cadence * 3);
    c.axis_forward = c.axis_leg_turn = c.axis_pitch = c.axis_twist = Fx{};
    ai_fire_disarm(ai);
    c.handler = AiHandler::ranker_mode_aim_update;
    ai_route_choose_ahead(ai, false);
    c.move_flags |= move_ranker_aim;
}
void ranker_mode_aim_update(Ai& ai) {
    AiController& c = ai.c;
    if (c.target >= 0) {
        ai_aim_at_target(ai, aim_twist | aim_legs);
        if (c.wants_jump || motion_jumping(ai)) {
            press(ai, control_jump_held | control_jump_pressed);
        } else {
            release(ai, control_jump_pressed);
        }
        ai_want_fire(ai);
        c.axis_forward = Fx{};
        return;
    }
    c.move_flags &= ~move_ranker_aim;
    to_route(c);
}
void ranker_mode_hold(Ai& ai) {
    AiController& c = ai.c;
    ai_route_choose_ahead(ai, false);
    c.state_timer = ai_address_bug_timer(ai.slot);
    press(ai, control_recentre);
    c.move_flags |= move_ranker_hold;
    c.handler = AiHandler::ranker_mode_hold_update;
}
void ranker_mode_hold_update(Ai& ai) {
    AiController& c = ai.c;
    if (c.target >= 0) {
        ai_aim_at_target(ai, aim_twist | aim_legs);
        if (c.wants_jump || motion_jumping(ai)) {
            press(ai, control_jump_held | control_jump_pressed);
        } else {
            release(ai, control_jump_pressed);
        }
        return;
    }
    c.move_flags &= ~move_ranker_hold;
    c.handler = AiHandler::ranker_route;
}

} // namespace

std::int16_t ai_address_bug_timer(int slot) {
    // (short)(int)(cadence x 5 x (int)(controller + 0x7F) / 256 + cadence x 0.5) at cadence 30,
    // the controller at 0x4716B0 + slot x 0x1DC (docs/port/ai.md, State 1)
    constexpr std::int16_t timers[6] = {-22620, -22341, -22062, -21783, -21504, -21225};
    return timers[slot >= 0 && slot < 6 ? slot : 0];
}

// decomp: ai_ranker_setup 0x5ECA1
void ai_ranker_setup(Ai& ai) {
    AiController& c = ai.c;
    if (ai.world.battle_phase == 1) {
        c.free_weapon_classes |= 4 | 8;
    }
    const int id = opponent_id(ai);
    if (id == 1) {
        // Roy: camouflage only against lock-on shots
        c.camo_when_shot = true;
        c.class5_fire_hold = static_cast<std::int16_t>(ai_cadence * 5);
        c.strafe_chance = 0x32;
        c.free_weapon_classes |= 4 | 8;
    } else if (id == 0 || id == 10 || id == 19) {
        c.class5_fire_hold = static_cast<std::int16_t>(ai_cadence * 5);
        c.strafe_chance = 0xA;
    } else if (fighter_role(ai.world, ai.slot) >= 2) {
        // the tournament's fixed opponent
        c.class5_fire_hold = static_cast<std::int16_t>(ai_cadence / 2);
        c.free_weapon_classes |= 4 | 8;
    }
}

// decomp: ai_ranker_gate 0x5DD35
void ai_ranker_gate(Ai& ai) {
    if (is_ranker(opponent_id(ai))) {
        ranker_update(ai);
    }
}

bool ai_run_ranker_handler(Ai& ai) {
    switch (ai.c.handler) {
    case AiHandler::ranker_mode_aim:
        ranker_mode_aim(ai);
        return true;
    case AiHandler::ranker_mode_aim_update:
        ranker_mode_aim_update(ai);
        return true;
    case AiHandler::ranker_mode_hold:
        ranker_mode_hold(ai);
        return true;
    case AiHandler::ranker_mode_hold_update:
        ranker_mode_hold_update(ai);
        return true;
    case AiHandler::ranker_route:
        ranker_route(ai);
        return true;
    case AiHandler::ranker_route_update:
        ranker_route_update(ai);
        return true;
    case AiHandler::ranker_attack:
        ranker_attack(ai);
        return true;
    case AiHandler::ranker_attack_update:
        ranker_attack_update(ai);
        return true;
    case AiHandler::ranker_strafe:
        ranker_strafe(ai);
        return true;
    case AiHandler::ranker_strafe_update:
        ranker_strafe_update(ai);
        return true;
    default:
        return false;
    }
}
