#include "battle/vs_battle.hpp"

#include "ai/ai_links.hpp"
#include "input/menu_input.hpp"
#include "input/pad_read.hpp"
#include "world/world.hpp"

namespace {

// the end sequence's times, ticks (one a 60 Hz step; retail adds video_rate_nominal /
// frame_cadence, 2, to the timer each 30 Hz update): two seconds before the menu, 30 for it to
// slide in or out
constexpr int decided_ticks = 120;
constexpr int slide_ticks = 30;
// the battle's pad reads are at its 30 Hz update (the auto-repeat counts them)
constexpr int battle_cadence = 30;

// actor_has_weapon 0x9D63E: a weapon on a mount that is still fitted
bool has_weapon(const Fighter& f) {
    for (const Weapon& w : f.combat.weapons) {
        if (w.id != -1 && w.fitted) {
            return true;
        }
    }
    return false;
}

// every seat's pad read again for the menu each step: the button word and the left stick's
// directions with their edges and auto-repeat, from what the world reads of the pad (b.cur,
// b.rising, the stick), at the battle's cadence
void menu_pads_read(VsBattle& vs, const Inputs& inputs) {
    for (int seat = 0; seat < max_pad_players; ++seat) {
        const auto i = static_cast<std::size_t>(seat);
        const PadRecord& pad = inputs.pads[i];
        PadButtons& b = vs.menu_buttons[i];
        b.prev = b.cur;
        b.cur = pad.b.cur;
        b.rising = pad.b.rising;
        b.falling = 0;
        pad_buttons_repeat(b, 2, battle_cadence);
        PadStick stick = pad.stick[0];
        stick.dirs = vs.menu_stick[i];
        pad_stick_directions(stick, battle_cadence);
        vs.menu_stick[i] = stick.dirs;
    }
}

// the seats in the battle (player_input_mask(0..3) ORed: their pads in order)
bool seat_in_battle(const World& world, int seat) {
    for (const Fighter& f : world.fighters) {
        if (f.present && f.controller == seat) {
            return true;
        }
    }
    return false;
}

// pads_cursor_move(mask, &cursor, 0, count, flags): the first seat whose pad moves it, with the
// d-pad and the left stick, repeating while held (0x16 down the end menu, 0x17 across the quit
// box: a fresh press wraps, a held one stops at the end)
void menu_cursor(const World& world, int& cursor, int count, unsigned flags) {
    const VsBattle& vs = world.vs;
    for (int seat = 0; seat < max_pad_players; ++seat) {
        if (!seat_in_battle(world, seat)) {
            continue;
        }
        PadRecord pad;
        pad.b = vs.menu_buttons[static_cast<std::size_t>(seat)];
        pad.stick[0].dirs = vs.menu_stick[static_cast<std::size_t>(seat)];
        if (menu_cursor_move(pad, cursor, 0, count, flags) != 0) {
            return;
        }
    }
}

// input_accept_pressed / input_cancel_pressed over the seats: A or Start, B or Back
bool menu_pressed(const World& world, const Inputs& inputs, std::uint16_t bits) {
    for (int seat = 0; seat < max_pad_players; ++seat) {
        if (seat_in_battle(world, seat) &&
            (inputs.pads[static_cast<std::size_t>(seat)].b.rising & bits) != 0) {
            return true;
        }
    }
    return false;
}

} // namespace

bool vs_round_decided(const World& world) {
    return world.vs.mode && world.vs.end_state >= 1;
}

// decomp: vs_nobody_in_play 0x48E8C
bool vs_nobody_in_play(const World& world) {
    for (bool in : world.vs.in_play) {
        if (in) {
            return false;
        }
    }
    return true;
}

void vs_battle_update(World& world, const Inputs& inputs) {
    VsBattle& vs = world.vs;
    if (!vs.mode) {
        return;
    }
    menu_pads_read(vs, inputs);
    // who dropped out: durability 0 or less, no weapon left (or through a gate, door_progress 2,
    // when doors come); the round is decided with one or none still in, or under the lobby's
    // team end one pilot team or none (a team of -1 is the fighter alone)
    int count = 0;
    int teams[fighter_slots];
    int team_count = 0;
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        if (!f.present) {
            vs.in_play[static_cast<std::size_t>(slot)] = false;
            continue;
        }
        if (f.combat.durability <= 0 || !has_weapon(f)) {
            vs.in_play[static_cast<std::size_t>(slot)] = false;
        }
        if (!vs.in_play[static_cast<std::size_t>(slot)]) {
            continue;
        }
        count += 1;
        const int pilot_team = battle_roster_entry(world, slot).pilot.team;
        const int team = pilot_team >= 0 ? pilot_team : -1 - slot;
        bool seen = false;
        for (int t = 0; t < team_count; ++t) {
            seen = seen || teams[t] == team;
        }
        if (!seen) {
            teams[team_count++] = team;
        }
    }
    if ((world.rules.team_end ? team_count : count) <= 1 && vs.end_state == 0) {
        vs.end_state = 1;
        vs.end_timer = 0;
    }

    switch (vs.end_state) {
    case 1:
        vs.end_timer += 1;
        if (vs.end_timer >= decided_ticks) {
            vs.end_state = 2;
            vs.end_timer = 0;
            vs.cursor = 0;
            vs.quit_open = false;
        }
        break;
    case 2: {
        if (vs.end_timer < slide_ticks) {
            vs.end_timer += 1;
        }
        const bool accept = menu_pressed(world, inputs, pad_a | pad_start);
        const bool cancel = menu_pressed(world, inputs, pad_b | pad_back);
        if (!vs.quit_open) {
            menu_cursor(world, vs.cursor, 4, cursor_menu_vertical);
            if (accept) {
                if (vs.cursor == vs_quit) {
                    vs.quit_open = true;
                    vs.quit_cursor = 1;
                } else {
                    vs.end_state = 3;
                    vs.end_timer = 0;
                }
            } else if (cancel) {
                vs.cursor = vs_quit;
            }
        } else {
            menu_cursor(world, vs.quit_cursor, 2, cursor_menu_horizontal);
            if (accept) {
                if (vs.quit_cursor == 0) {
                    vs.end_state = 3;
                    vs.end_timer = 0;
                }
                vs.quit_open = false;
            } else if (cancel) {
                vs.quit_cursor = 1;
            }
        }
        break;
    }
    case 3:
        if (vs.end_timer < slide_ticks) {
            vs.end_timer += 1;
        }
        if (vs.end_timer >= slide_ticks) {
            vs.end_state = 4;
            vs.end_timer = 0;
        }
        break;
    case 4:
        // a win for each player in play and a loss for the rest, unless nobody is (a draw)
        if (!vs_nobody_in_play(world)) {
            for (int slot = 0; slot < fighter_slots; ++slot) {
                const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
                if (!f.present || f.controller < 0 || f.controller >= max_pad_players) {
                    continue;
                }
                const auto seat = static_cast<std::size_t>(f.controller);
                if (vs.in_play[static_cast<std::size_t>(slot)]) {
                    vs.wins[seat] += 1;
                } else {
                    vs.losses[seat] += 1;
                }
            }
        }
        vs.result = vs.cursor + 2;
        vs.end_state = 6;
        break;
    case 5:
        // the pause's quit: to the title, nothing counted
        vs.result = 5;
        vs.end_state = 6;
        break;
    default:
        break;
    }
}
