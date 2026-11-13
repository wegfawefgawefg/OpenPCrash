// The AI's spine (decomp src/od/u_0577a0.c and ai_target_track.c): ai_enter, ai_update's order,
// ai_sense_update, the state changes and decisions, the rolls. docs/port/ai.md, "Spawning", "One
// update", "State machine".
#include "ai/ai_internal.hpp"
#include "ai/ai_links.hpp"
#include "mech/fighter_rates.hpp"

namespace {

// ai_state_handlers: the entry of each state
constexpr AiHandler state_entries[7] = {
    AiHandler::stand_fire,    AiHandler::route_follow, AiHandler::facing_pause, AiHandler::combat,
    AiHandler::neutral_pause, AiHandler::back_off,     AiHandler::wander};

// the odd step's record: retail's AI sets 4 with 2 as a level and 0x4000 every update it turns
// hard, and its 8 and the d-pad bits are presses; held over a step they'd press twice
constexpr std::uint32_t odd_step_cleared = control_edges | control_recentre;

// decomp: ai_sense_update 0x58406
void ai_sense_update(Ai& ai) {
    AiController& c = ai.c;
    Fighter& f = ai.f;
    // a wall face or a prop, or an edge on a 64 of 255 roll. The port's contact bits are a step's;
    // the pair's two steps together stand for retail's update
    const int contact = c.contact_flags_even | f.contact_flags;
    c.wants_jump = false;
    if ((contact & (contact_wall | surface_prop)) ||
        ((contact & contact_edge) && ai_roll(ai, 0x40, 0xFF))) {
        c.wants_jump = true;
    }
    if (c.frames_since_jump < 0x1000) {
        ++c.frames_since_jump;
    }
    if (c.frames_since_backup < 0x1000) {
        ++c.frames_since_backup;
    }
    const int durability = fighter_durability(ai.world, ai.slot);
    const int durability_max = fighter_durability_max(ai.world, ai.slot);
    const int band = durability_max != 0 ? durability * 5 / durability_max : 0;
    c.durability_band = band < 0 ? 0 : band > 4 ? 4 : band;
    if (c.state != 6) {
        ai_weapons_stop_if_empty(ai);
    }
    // state 1 measures to the route point, which the weapon range test then uses (kept)
    FxVec3 d = f.position;
    if (c.state == 1) {
        d = f.position - c.route.from;
    } else if (c.target >= 0) {
        d = f.position - ai.world.fighters[static_cast<std::size_t>(c.target)].position;
    }
    c.target_distance = length(d);
    d.y = Fx{};
    c.target_distance_flat = length(d);
    c.prev_state = c.state;
    triggers_clear_pressed(ai.world, ai.slot);
    release(ai, control_recentre);
    // past five seconds path_cache is set to -1 every update; it never holds anything else
    if (c.frames_ticking++ > ai_cadence * 5) {
        c.path_cache = -1;
    }
}

// ai_fire_pacing_init's and ai_camo_setup's numbers come from here and ai_weapons.cpp
// decomp: ai_enter 0x577E3
void ai_enter(Ai& ai) {
    AiController& c = ai.c;
    const ControlRecord held = c.record;
    c = AiController{};
    c.record = held;
    c.node_kind = 3;
    c.flags = 0;
    c.prev_state = 1;
    c.handler = state_entries[c.state];
    c.last_durability = fighter_durability(ai.world, ai.slot);
    // the view cone from ai_level: 0 in a quest (spec isn't written yet), 99 in a demo
    const int level = ai_level(ai);
    constexpr Fx sixth_pi = fx_f(0.5235988);
    if (level > 0) {
        const Fx cone =
            fx_div_near(fx_mul_near(fx(level), fx_f(2.6179938)) + fx_f(51.83628), fx(99));
        c.view_yaw = fx_clamp(cone, sixth_pi, fx_f(3.1415927));
        c.view_pitch = fx_clamp(cone, sixth_pi, fx_f(1.5707964));
    } else {
        c.view_yaw = sixth_pi;
        c.view_pitch = sixth_pi;
    }
    // ai_range_roll: no reader, one draw
    constexpr int ranges[5] = {13, 18, 25, 30, 35};
    c.wanted_range = fx(ranges[ai_draw(ai) % 5]);
    c.target_slot = -1;
    c.path_cache = -1;
    c.frames_since_jump = ai_cadence * 3 + 1;
    c.frames_since_backup = ai_cadence + 1;
    ai_fire_pacing_init(ai);
    ai_weapons_bind(ai);
    ai_camo_setup(ai);
    ai_stat_bands_apply(ai);
    ai_ranker_setup(ai);
    ai_change_state(ai, 1);
}

// decomp: ai_update 0x57BF1
void ai_update(Ai& ai) {
    AiController& c = ai.c;
    Fighter& f = ai.f;
    if (fighter_durability(ai.world, ai.slot) <= 0) {
        return;
    }
    f.control.control_buttons = 0;
    ai_pick_target(ai);
    ai_sense_update(ai);
    ai_run_handler(ai);
    ai_camo_update(ai);
    if (c.handler != AiHandler::wander_update) {
        ai_special_moves(ai);
    }
    ai_dodge_shots(ai);
    // the stick scale, (level + 128) / 99 within 0.5..1: 1 from level -29 up
    const Fx scale = fx_clamp(fx_ratio(ai_level(ai) + 128, 99), fx_f(0.5), fx(1));
    const Fx axes[4] = {c.axis_forward, c.axis_leg_turn, c.axis_pitch, c.axis_twist};
    for (int i = 0; i < 4; ++i) {
        f.control.control_axis[i] = fx_clamp(fx_mul_near(axes[i], scale), fx(-1), fx(1));
    }
    ai_weapons_update(ai);
    c.last_durability = fighter_durability(ai.world, ai.slot);
    c.node_kind = route_kind(ai);
    --c.reeval_timer;
    ++c.age_frames;
}

} // namespace

Ai ai_context(World& world, int slot) {
    const auto s = static_cast<std::size_t>(slot);
    return {world, slot, world.fighters[s], world.ai[s], world.huds[s], world.battle->ai};
}

// decomp: ai_level 0x5F3C5
int ai_level(const Ai& ai) {
    return ai.world.battle_phase == 1 ? 99 : ai.c.spec_level;
}

int ai_draw(Ai& ai) {
    return rand_table_next(ai.world.random);
}

// decomp: ai_roll 0x5BD98
bool ai_roll(Ai& ai, int chance, int range) {
    // retail passes both as shorts
    chance = static_cast<std::int16_t>(chance);
    range = static_cast<std::int16_t>(range);
    if (range > 0 && chance < range) {
        return chance > 0 && rand_int(ai.world.random, range) <= chance;
    }
    return true;
}

// decomp: ai_weighted_pick 0x5BDF2
int ai_weighted_pick(Ai& ai) {
    const std::vector<int>& weights = ai.data.tables.search_weights;
    int sum = 0;
    for (int w : weights) {
        sum += w;
    }
    const int roll = rand_int(ai.world.random, sum);
    int total = 0;
    for (std::size_t j = 0; j < weights.size(); ++j) {
        total += weights[j];
        if (roll <= total) {
            return static_cast<int>(j);
        }
    }
    return 0;
}

int ai_jittered(Ai& ai, int base, int jitter_tenths) {
    // (r / 32767 x jitter + 1) x base, truncated toward zero; exact here, retail's float rounding
    // agrees except where base x r x jitter / 32767 falls on a whole number
    const std::int64_t r = ai_draw(ai);
    const std::int64_t scale = 32767 * 10;
    const std::int64_t size = base < 0 ? -base : base;
    const std::int64_t value = (size * (scale + r * jitter_tenths)) / scale;
    return static_cast<int>(base < 0 ? -value : value);
}

// decomp: actor_role 0x5EBFD
int fighter_role(const World& world, int slot) {
    // rumble_context_by_selector always finds the area's context while a game is running
    const RumbleSlot& rumble = world.rumble[day_slot_of_selector(world.story)];
    const RosterEntry& entry = rumble.roster[slot];
    switch (entry.ticket.opponent) {
    case 0:
    case 1:
    case 10:
    case 19:
        return 2;
    default:
        return entry.ticket.opponent < 0 ? -1 : entry.ticket.progress.role;
    }
}

// decomp: ai_change_state 0x587C6
void ai_change_state(Ai& ai, int state) {
    AiController& c = ai.c;
    const int row = static_cast<std::int8_t>(c.spec_level) + 128;
    // ai_level_timing_profiles by spec[0] (not ai_level), ticks to updates; state 6 reads the next
    // row's first column
    const auto& table = ai.data.tables.timing;
    const int ticks = state < 6
                          ? table[static_cast<std::size_t>(row)][static_cast<std::size_t>(state)]
                          : table[static_cast<std::size_t>(row + 1)][0];
    c.state = state;
    c.handler = state_entries[state];
    c.state_timer = static_cast<std::int16_t>(static_cast<std::uint16_t>(ticks) * ai_cadence / 60);
    c.reeval_timer = static_cast<std::int16_t>(ai_cadence << 2);
    c.mode = -1;
    release(ai, control_dash | control_jump_held | control_jump_pressed | control_step_right |
                    control_step_left);
    ai_fire_stop_all(ai);
}

// decomp: ai_choose_next_state 0x5C600
int ai_choose_next_state(Ai& ai) {
    AiController& c = ai.c;
    if (c.reeval_timer >= 0) {
        return -1;
    }
    int row = -1;
    if (c.state_timer < 0) {
        row = 0;
    }
    if (c.target >= 0) {
        row = 1;
    }
    if (fighter_durability(ai.world, ai.slot) != c.last_durability) {
        row = 2;
    }
    if (c.out_of_ammo) {
        row = 4;
    }
    if (row < 0) {
        return -1;
    }
    const AiTables& t = ai.data.tables;
    const int profile = c.profile >= 0 && c.profile < ai_profiles ? c.profile : 0;
    const int cell = t.selection[static_cast<std::size_t>(profile)][static_cast<std::size_t>(row)]
                                [static_cast<std::size_t>(c.durability_band)];
    const auto& probs = ai.world.battle_phase == 1 ? t.probs_alternate : t.probs_normal;
    int next = -1;
    for (int col = 0; col < 5; ++col) {
        if (ai_roll(ai, probs[static_cast<std::size_t>(cell)][static_cast<std::size_t>(col)],
                    0xFF)) {
            next = t.result_map[static_cast<std::size_t>(col)];
            break;
        }
    }
    if (next == 0 && ai_roll(ai, 0x80, 0xFF)) {
        next = 1;
    }
    return next < 0 ? c.state : next;
}

void press(Ai& ai, std::uint32_t bits) {
    ai.f.control.control_buttons |= bits;
}
void release(Ai& ai, std::uint32_t bits) {
    ai.f.control.control_buttons &= ~bits;
}
bool motion_jumping(const Ai& ai) {
    return ai.f.motion_state == state_jump;
}
bool motion_falling(const Ai& ai) {
    return ai.f.motion_state == state_fall;
}
Fx camera_pitch_original(const Ai& ai) {
    return -angle_radians(ai.f.camera.pitch);
}

void ai_spawn(World& world, int slot, int level, int profile) {
    Ai ai = ai_context(world, slot);
    ai_enter(ai);
    // ai_controller_assign: from here ai_level reads the roster's level
    ai.c.spec_level = level;
    ai.c.profile = profile;
}

void ai_fighter_input(World& world, int slot) {
    Ai ai = ai_context(world, slot);
    if (world.step % 2 == 0) {
        ai_update(ai);
        ai.c.record = ai.f.control;
    } else {
        ai.f.control = ai.c.record;
        ai.f.control.control_buttons &= ~odd_step_cleared;
    }
}

void ai_after_step(World& world, int slot) {
    if (world.step % 2 == 0) {
        world.ai[static_cast<std::size_t>(slot)].contact_flags_even =
            world.fighters[static_cast<std::size_t>(slot)].contact_flags;
    }
}
