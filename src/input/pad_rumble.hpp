#pragma once
// The pads' rumble (decomp src/hand/genkilib_pad_o2.c rumble_start and the player
// 0xDF770, src/o2/genkilib_0de830.c rumble_halt; the hit's table in
// src/hand/mech_part_assets.c actor_settle_damage, D_00232560): a hit settled on a human
// in a fight (the world's event_hit_rumble) plays a short table on its seat's own pads. Retail
// scales the table by the damage, clamped 300..1000 over 1000, and halves its durations under 500.
// Presentation only: the world gives the hit, this plays it. OPTION's VIBRATION turns it off
// (rumble_start's flags), gubsy's vibration strength (PC CONFIG's INPUT page) scales it.
//
// rumble_halt stops a fighter's table on the spot: at the pause and every paused update
// (pause_enter, pause_menu_update), when a scene parks the others (battle_pause_others)
// and when the fighter retires or the fight ends (fighter_task_destroy). The port
// halts at the same points: pad_rumble_stop at the pause and the fight's end, a seat whose fighter
// is parked or retired in pad_rumble_events.
//
// Sent as Mans In My House sends it (local_players.cpp rumble_seat): SDL_RumbleGamepad on every pad
// the seat's gubsy player holds, levels scaled by the seat's input profile. Each send lasts
// send_ms and is sent again every step while a row plays (SDL only passes a change of level to
// the pad; the same level just moves its end), so a stalled game goes quiet by itself. A pad with
// trigger motors (SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN: Xbox One and Series pads) also
// gets the hit in its triggers, a port addition: the left and right motors' levels at
// trigger_share.
#include "world/pad_record.hpp"

#include <array>

class GubsyRuntime;
struct World;

// one row of the table: how long, the left (low) and right (high) motors' levels 0..65535
struct RumbleRow {
    int steps = 0;
    int left = 0, right = 0;
};

struct SeatRumble {
    RumbleRow rows[3];
    int row = -1; // playing, -1 none
    int left = 0; // steps left in the row (its gap after it at 0)
    bool gap = false;
    bool sent = false;   // the motors were last sent running
    bool parked = false; // its fighter parked or retired at the last step (the halt's edge)
};

struct PadRumble {
    std::array<SeatRumble, max_pad_players> seats;
};

// after a world step: a seat whose fighter was parked or retired this step halts, then each
// event_hit_rumble of this step starts its seat's table
void pad_rumble_events(PadRumble& rumble, GubsyRuntime& runtime, const World& world);
// once a world step, after the events: the rows move on and the seats' pads are sent their
// levels (on: OPTION's VIBRATION is on; rumble_start with flags 1 plays nothing)
void pad_rumble_step(PadRumble& rumble, GubsyRuntime& runtime, bool on);
// every seat's table halted and its pads' motors stopped now (rumble_halt: the pause and each
// paused update, the fight's end)
void pad_rumble_stop(PadRumble& rumble, GubsyRuntime& runtime);
