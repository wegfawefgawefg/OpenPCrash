#include "input/pad_rumble.hpp"

#include "world/world.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <gubsy/input/profile_settings.hpp>
#include <gubsy/input/types.hpp>
#include <gubsy/lobby/state.hpp>
#include <gubsy/runtime.hpp>
#include <vector>

namespace {

// D_00232560[1]: the hit's rows, updates and the two motors' XInput levels (row [0] is the copy
// actor_settle_damage scales; [2] is never read)
constexpr int base_rows[3][3] = {{20, 65000, 45000}, {10, 50000, 35000}, {5, 30000, 25000}};
// the damage's scale: clamped 300..1000, over 1000; under 500 the durations halve
constexpr int damage_least = 300;
constexpr int damage_most = 1000;
constexpr int damage_half = 500;
// a row holds its levels d updates and then stops its motors for one before the next row loads
// (the player 0xDF770 with 0xDEB80); R4: 2 d steps and a gap of 2
constexpr int steps_per_update = 2;
// each send keeps the motors running this long, so a game that stops stepping goes quiet
constexpr Uint32 send_ms = 100;
// the triggers' share of the motors' levels (a port addition; impulse triggers are strong)
constexpr float trigger_share = 0.5f;

std::vector<SDL_Gamepad*> seat_pads(GubsyRuntime& runtime, int seat) {
    std::vector<SDL_Gamepad*> pads;
    const GubsyLobbyPlayer* player = gubsy_lobby_player(gubsy_runtime_engine(runtime), seat);
    if (!player) {
        return pads;
    }
    for (const GubsyLobbyDeviceAssignment& d : player->devices) {
        if (d.type == InputSourceType::Gamepad) {
            if (SDL_Gamepad* pad = SDL_GetGamepadFromID(static_cast<SDL_JoystickID>(d.device_id))) {
                pads.push_back(pad);
            }
        }
    }
    return pads;
}

void send(GubsyRuntime& runtime, int seat, int left, int right) {
    const InputSettingsProfile* settings =
        gubsy_lobby_input_settings_profile(gubsy_runtime_engine(runtime), seat);
    const float strength = settings ? std::clamp(settings->vibration_strength, 0.0f, 1.0f) : 1.0f;
    const auto level = [&](int v) {
        return static_cast<Uint16>(std::clamp(static_cast<float>(v) * strength, 0.0f, 65535.0f));
    };
    // PHANTOMCRASH_RUMBLE_TRACE=1: every send, for headless runs with virtual pads
    static const bool trace = SDL_getenv("PHANTOMCRASH_RUMBLE_TRACE") != nullptr;
    const std::vector<SDL_Gamepad*> pads = seat_pads(runtime, seat);
    if (trace && pads.empty()) {
        std::printf("rumble seat %d %d %d: no pad\n", seat, left, right);
    }
    const Uint32 ms = left || right ? send_ms : 0;
    for (SDL_Gamepad* pad : pads) {
        const bool sent = SDL_RumbleGamepad(pad, level(left), level(right), ms);
        if (trace) {
            std::printf("rumble seat %d %u %u %s\n", seat, level(left), level(right),
                        sent ? "sent" : SDL_GetError());
        }
        // the trigger motors, where the pad has them
        const SDL_PropertiesID caps = SDL_GetGamepadProperties(pad);
        if (SDL_GetBooleanProperty(caps, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false)) {
            const auto share = [&](int v) {
                return static_cast<int>(static_cast<float>(v) * trigger_share);
            };
            SDL_RumbleGamepadTriggers(pad, level(share(left)), level(share(right)), ms);
        }
    }
}

// rumble_halt: the table dropped and the motors stopped now
void halt(SeatRumble& s, GubsyRuntime& runtime, int seat) {
    if (s.sent) {
        send(runtime, seat, 0, 0);
    }
    const bool parked = s.parked;
    s = SeatRumble{};
    s.parked = parked;
}

// rumble_start: the table from row 0
void start(SeatRumble& s, int damage) {
    const float high = static_cast<float>(std::clamp(damage, damage_least, damage_most)) /
                       static_cast<float>(damage_most);
    const float low = damage < damage_half ? 0.5f : 1.0f;
    for (int r = 0; r < 3; ++r) {
        s.rows[r].steps =
            static_cast<int>(static_cast<float>(base_rows[r][0]) * low) * steps_per_update;
        s.rows[r].left = static_cast<int>(static_cast<float>(base_rows[r][1]) * high);
        s.rows[r].right = static_cast<int>(static_cast<float>(base_rows[r][2]) * high);
    }
    s.row = 0;
    s.left = s.rows[0].steps;
    s.gap = false;
}

} // namespace

void pad_rumble_events(PadRumble& rumble, GubsyRuntime& runtime, const World& world) {
    // a seat whose fighter a scene parked (battle_pause_others) or that retired halts on that
    // step (rumble_halt); an empty slot plays no seat whatever its controller says
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        if (!f.present || f.controller < 0 || f.controller >= max_pad_players) {
            continue;
        }
        SeatRumble& s = rumble.seats[static_cast<std::size_t>(f.controller)];
        const bool parked = world.battle_state.slots[slot].parked;
        if (parked && !s.parked) {
            halt(s, runtime, f.controller);
        }
        s.parked = parked;
    }
    for (int i = 0; i < world.combat.event_count; ++i) {
        const CombatEvent& e = world.combat.events[static_cast<std::size_t>(i)];
        if (e.kind != event_hit_rumble || e.slot < 0 || e.slot >= fighter_slots) {
            continue;
        }
        const int seat = world.fighters[static_cast<std::size_t>(e.slot)].controller;
        if (seat >= 0 && seat < max_pad_players) {
            start(rumble.seats[static_cast<std::size_t>(seat)], e.value);
            if (SDL_getenv("PHANTOMCRASH_RUMBLE_TRACE")) {
                std::printf("rumble start seat %d damage %d\n", seat, e.value);
            }
        }
    }
}

void pad_rumble_step(PadRumble& rumble, GubsyRuntime& runtime, bool on) {
    for (int seat = 0; seat < max_pad_players; ++seat) {
        SeatRumble& s = rumble.seats[static_cast<std::size_t>(seat)];
        int left = 0, right = 0;
        if (s.row >= 0) {
            // a row's levels, then its gap, then the next row (retail plays only the first row:
            // docs/bugs.md)
            if (!s.gap) {
                left = s.rows[s.row].left;
                right = s.rows[s.row].right;
            }
            if (--s.left <= 0) {
                if (!s.gap) {
                    s.gap = true;
                    s.left = steps_per_update;
                } else if (s.row + 1 < 3) {
                    s.row += 1;
                    s.left = s.rows[s.row].steps;
                    s.gap = false;
                } else {
                    s.row = -1;
                }
            }
        }
        if (!on) {
            left = right = 0;
        }
        const bool running = left || right;
        if (running || s.sent) {
            send(runtime, seat, left, right);
        }
        s.sent = running;
    }
}

void pad_rumble_stop(PadRumble& rumble, GubsyRuntime& runtime) {
    for (int seat = 0; seat < max_pad_players; ++seat) {
        halt(rumble.seats[static_cast<std::size_t>(seat)], runtime, seat);
    }
}
