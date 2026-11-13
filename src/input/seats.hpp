#pragma once
// Who plays at this machine and on what (docs/input.md, "Seats"). A seat is a match seat 0..3:
// gubsy's lobby player of the same number, the pad record Inputs::pads[seat] and the mouse aim
// Inputs::aims[seat], a fighter's `controller`, and in VS its view, HUD and win count. Alone, seat 0
// plays on the keyboard, the mouse and every pad at once (the front end, the campaign, the viewer).
// Split (VS), each seat plays on the device it joined with, the keyboard and mouse or one pad, in
// any order: a pad as P1 and the keyboard as P2 is as good as the reverse. The screens change the
// seats as plain data; main hands them to gubsy's lobby every update (seats_sync), the way Mans In
// My House's local_players.cpp does.
#include "world/pad_record.hpp"

#include <array>
#include <string>
#include <vector>

union SDL_Event;
struct GubsyRuntime;

// a thing to play on: the keyboard and the mouse together, or one pad
struct SeatDevice {
    bool keyboard = false;
    int pad = -1; // SDL joystick id
    friend bool operator==(const SeatDevice&, const SeatDevice&) = default;
};

struct Seat {
    bool used = false;
    SeatDevice device; // what it joined with
};

// an accept or cancel pressed on a device (Enter, Space, A, Start / Escape, Backspace, B, Back),
// whoever holds it: the VS join page seats whoever presses
struct SeatPress {
    SeatDevice device;
    bool accept = false;
    bool cancel = false;
};

struct Seats {
    bool split = false; // false: seat 0 on everything
    std::array<Seat, max_pad_players> seats;
    std::vector<int> pads_here;     // the pads plugged in, SDL ids (seats_sync)
    std::vector<SeatPress> presses; // since the last front end update (main, from events)
};

// seat 0 on the keyboard, the mouse and every pad: the front end and every one-player mode
void seats_alone(Seats& seats);
// nobody seated yet: the VS join page
void seats_split(Seats& seats);
// the seat playing on a device, -1 none (alone, seat 0 holds everything)
int seat_of(const Seats& seats, SeatDevice device);
// a device joins: the lowest free seat, or -1 when all four are taken
int seat_join(Seats& seats, SeatDevice device);
void seat_leave(Seats& seats, int seat);
// a seat's device is here (an unplugged pad isn't)
bool seat_connected(const Seats& seats, int seat);
// devices here that no seat holds (the keyboard counts as one)
int seats_free_devices(const Seats& seats);
// the seat that reads the mouse: the one holding the keyboard, seat 0 alone, -1 none
int mouse_seat(const Seats& seats);
// "Keyboard and mouse", "Pad 2"
std::string seat_device_label(const Seats& seats, SeatDevice device);

// an event's device and whether it is an accept or cancel press
bool seat_press_of(const SDL_Event& event, SeatPress& press);
// every update before the pads are read: gubsy's lobby players and their devices follow the
// seats (four players while split, so a player's number is its seat), and the pads here
void seats_sync(Seats& seats, GubsyRuntime& runtime);
