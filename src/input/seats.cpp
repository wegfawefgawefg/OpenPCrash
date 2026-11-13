#include "input/seats.hpp"

#include "input/bindings.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <gubsy/input/types.hpp>
#include <gubsy/lobby/state.hpp>
#include <gubsy/runtime.hpp>
// gubsy's engine, for its lobby (as devices.cpp reads it)
#include "src/engine_state.hpp"

namespace {

bool here(const Seats& seats, SeatDevice device) {
    return device.keyboard || std::find(seats.pads_here.begin(), seats.pads_here.end(),
                                        device.pad) != seats.pads_here.end();
}

bool same(const GubsyLobbyDeviceAssignment& a, const GubsyLobbyDeviceAssignment& b) {
    return a.type == b.type && a.device_id == b.device_id;
}

// what a lobby player should hold: alone, player 0 everything; split, a seat its device
std::vector<GubsyLobbyDeviceAssignment> wanted(const Seats& seats, int index) {
    std::vector<GubsyLobbyDeviceAssignment> devices;
    if (!seats.split) {
        if (index == 0) {
            devices.push_back({InputSourceType::Keyboard, 0});
            devices.push_back({InputSourceType::Mouse, 0});
            for (int pad : seats.pads_here) {
                devices.push_back({InputSourceType::Gamepad, pad});
            }
        }
        return devices;
    }
    const Seat& seat = seats.seats[static_cast<std::size_t>(index)];
    if (!seat.used || !here(seats, seat.device)) {
        return devices;
    }
    if (seat.device.keyboard) {
        devices.push_back({InputSourceType::Keyboard, 0});
        devices.push_back({InputSourceType::Mouse, 0});
    } else {
        devices.push_back({InputSourceType::Gamepad, seat.device.pad});
    }
    return devices;
}

} // namespace

void seats_alone(Seats& seats) {
    seats.split = false;
    for (Seat& seat : seats.seats) {
        seat = Seat{};
    }
    seats.seats[0].used = true;
    seats.seats[0].device = {true, -1};
}

void seats_split(Seats& seats) {
    seats.split = true;
    for (Seat& seat : seats.seats) {
        seat = Seat{};
    }
}

int seat_of(const Seats& seats, SeatDevice device) {
    if (!seats.split) {
        return 0;
    }
    for (int i = 0; i < max_pad_players; ++i) {
        const Seat& seat = seats.seats[static_cast<std::size_t>(i)];
        if (seat.used && seat.device == device) {
            return i;
        }
    }
    return -1;
}

int seat_join(Seats& seats, SeatDevice device) {
    const int already = seat_of(seats, device);
    if (already >= 0) {
        return already;
    }
    for (int i = 0; i < max_pad_players; ++i) {
        Seat& seat = seats.seats[static_cast<std::size_t>(i)];
        if (!seat.used) {
            seat.used = true;
            seat.device = device;
            return i;
        }
    }
    return -1;
}

void seat_leave(Seats& seats, int seat) {
    if (seat >= 0 && seat < max_pad_players) {
        seats.seats[static_cast<std::size_t>(seat)] = Seat{};
    }
}

bool seat_connected(const Seats& seats, int seat) {
    if (seat < 0 || seat >= max_pad_players) {
        return false;
    }
    const Seat& s = seats.seats[static_cast<std::size_t>(seat)];
    return s.used && here(seats, s.device);
}

int seats_free_devices(const Seats& seats) {
    int count = seat_of(seats, {true, -1}) < 0 ? 1 : 0;
    for (int pad : seats.pads_here) {
        count += seat_of(seats, {false, pad}) < 0 ? 1 : 0;
    }
    return count;
}

int mouse_seat(const Seats& seats) {
    return seat_of(seats, {true, -1});
}

std::string seat_device_label(const Seats& seats, SeatDevice device) {
    if (device.keyboard) {
        return "Keyboard and mouse";
    }
    for (std::size_t i = 0; i < seats.pads_here.size(); ++i) {
        if (seats.pads_here[i] == device.pad) {
            return "Pad " + std::to_string(i + 1);
        }
    }
    return "A pad";
}

bool seat_press_of(const SDL_Event& event, SeatPress& press) {
    press = SeatPress{};
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        const SDL_Scancode key = event.key.scancode;
        press.device = {true, -1};
        press.accept = key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER ||
                       key == SDL_SCANCODE_SPACE;
        press.cancel = key == SDL_SCANCODE_ESCAPE || key == SDL_SCANCODE_BACKSPACE;
    } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        const int button = event.gbutton.button;
        press.device = {false, static_cast<int>(event.gbutton.which)};
        press.accept = button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START;
        press.cancel = button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK;
    }
    return press.accept || press.cancel;
}

void seats_sync(Seats& seats, GubsyRuntime& runtime) {
    EngineState& engine = gubsy_runtime_engine(runtime);
    gubsy_lobby_ensure_ready(engine);
    seats.pads_here.clear();
    for (const GubsyGamepad& pad : gubsy_get_gamepads(runtime)) {
        seats.pads_here.push_back(pad.device_id);
    }
    // four lobby players while split, so player i is seat i; one alone. Each starts on player 1's
    // binds and settings profiles (premade "Keyboard, mouse and pad" covers both kinds)
    const int count = seats.split ? max_pad_players : 1;
    const GubsyLobbyState& lobby = gubsy_get_lobby_state(runtime);
    const int binds = lobby.local_players.front().binds_profile_id;
    const int settings = lobby.local_players.front().input_settings_profile_id;
    while (static_cast<int>(gubsy_get_lobby_state(runtime).local_players.size()) < count) {
        const int before = static_cast<int>(gubsy_get_lobby_state(runtime).local_players.size());
        const int added = gubsy_add_lobby_local_player(runtime);
        if (added < before) {
            break;
        }
        (void)gubsy_set_lobby_player_binds_profile(runtime, added, binds);
        (void)gubsy_set_lobby_player_input_settings_profile(runtime, added, settings);
    }
    while (static_cast<int>(gubsy_get_lobby_state(runtime).local_players.size()) > count) {
        const int last = static_cast<int>(gubsy_get_lobby_state(runtime).local_players.size()) - 1;
        if (!gubsy_remove_lobby_local_player(runtime, last)) {
            break;
        }
    }
    // each player's binds: their own profile once PC CONFIG's BINDINGS has made one (players 2..4
    // in VS), else player 1's; and their devices: what it has and shouldn't toggled off, what it
    // lacks toggled on
    for (int index = 0; index < count; ++index) {
        const auto& players = gubsy_get_lobby_state(runtime).local_players;
        if (index >= static_cast<int>(players.size())) {
            break;
        }
        const int own = player_profile_first + index;
        const int binds_wanted =
            gubsy_find_binds_profile(runtime, own) ? own : player_profile_first;
        if (players[static_cast<std::size_t>(index)].binds_profile_id != binds_wanted) {
            (void)gubsy_set_lobby_player_binds_profile(runtime, index, binds_wanted);
        }
        const std::vector<GubsyLobbyDeviceAssignment> want = wanted(seats, index);
        const std::vector<GubsyLobbyDeviceAssignment> have =
            players[static_cast<std::size_t>(index)].devices;
        for (const GubsyLobbyDeviceAssignment& device : have) {
            if (std::none_of(want.begin(), want.end(),
                             [&](const auto& other) { return same(device, other); })) {
                gubsy_toggle_lobby_player_device(runtime, index, device);
            }
        }
        for (const GubsyLobbyDeviceAssignment& device : want) {
            if (std::none_of(have.begin(), have.end(),
                             [&](const auto& other) { return same(device, other); })) {
                gubsy_toggle_lobby_player_device(runtime, index, device);
            }
        }
    }
}
