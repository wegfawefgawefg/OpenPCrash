#pragma once
// One step's input, the world's only window on the player. Built by the view from latched events
// (src/view/input.cpp) and the input layer (src/input/, docs/input.md): each player's pad record,
// which a fighter's step turns into its 20-byte control record (world/control_route.hpp).
#include "world/pad_record.hpp"

#include <array>
#include <cstdint>

struct Inputs {
    // free camera: -1, 0 or 1 along its right, up and forward
    std::int8_t move_right = 0;
    std::int8_t move_up = 0;
    std::int8_t move_forward = 0;
    bool fast = false;
    // turns this step, in binary angle units (2^32 a turn)
    std::int32_t turn_yaw = 0;
    std::int32_t turn_pitch = 0;
    // each match seat's pad as read before this step (src/input/devices.hpp, read_pads), by
    // seat: a human fighter reads pads[controller]. A seat is a local seat now (src/input/seats);
    // online it will be any machine's (docs/multiplayer.md)
    std::array<PadRecord, max_pad_players> pads;
    // and each seat's mouse aim for this step, in control axis units (only the seat that holds
    // the mouse has any)
    std::array<MouseAim, max_pad_players> aims;
    // the battle's scene text that presentation has seen end (its BattleState serial, -1 none):
    // the lessons wait on it (battle_tutorial.cpp). Online this comes from presentation on one
    // machine, so it would have to be a seat's input too (docs/backlog.md)
    int scene_text_done = -1;
    // port-only dev actions of headless campaign runs (battle/battle_dev.hpp); 0 in play
    unsigned dev = 0;
};
