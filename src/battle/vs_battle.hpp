#pragma once
// Split-screen VS in the battle (decomp docs/port/battle.md "Split-screen VS"; battle_update's VS
// half, src/hand/battle_frame.c). Who is still in play, the round's end, and the end menu
// (rematch, area, machine, quit, with the quit box), which every seat's pad drives. It runs in the
// world on the seats' pads, so online play gets it for free; the HUD draws it (hud_scenes.cpp).
#include "world/fighter_slots.hpp"
#include "world/pad_record.hpp"

#include <array>
#include <cstdint>

struct World;
struct Inputs;

// the end menu's rows, and the battle's result word for each (battle_result_set: 2..5)
enum VsEndRow : int { vs_rematch = 0, vs_area = 1, vs_machine = 2, vs_quit = 3 };

struct VsBattle {
    bool mode = false;               // GameState.vs_mode
    std::array<bool, fighter_slots> in_play{}; // GameState.vs_in_play by slot (join order)
    int end_state = 0;               // Battle.vs_end_state: 0 fighting .. 6 decided
    int end_timer = 0;               // vs_end_timer, ticks (one a step)
    int cursor = 0;                  // vs_end_cursor: the menu's row
    bool quit_open = false;          // vs_quit_open: the quit box is up
    int quit_cursor = 1;             // its answer: 0 yes, 1 no
    int result = 0;                  // the battle's result word once decided (2..5), else 0
    // GameState.vs_wins / vs_losses by controller (seat), kept across rounds
    std::array<int, max_pad_players> wins{}, losses{};
    // each seat's buttons and left stick directions with their auto-repeat (pads_read's
    // 0xDEE20 and 0xDEF90 at the battle's 30 Hz reads), kept from the pads' b.cur, b.rising and
    // stick each step, so the end menu repeats as retail's pads_cursor_move does while the world
    // reads no more of the pad than it did (docs/multiplayer.md item 5)
    std::array<PadButtons, max_pad_players> menu_buttons{}, menu_stick{};
};

// at the top of the battle's update (battle_update, before the fighters): who dropped out
// (durability 0, no fitted weapon), the round decided with one or none left, then the end
// sequence. Nothing outside VS.
void vs_battle_update(World& world, const Inputs& inputs);
// once decided, the control records go to zero and damage stops settling (sv_control_route,
// actor_settle_damage)
bool vs_round_decided(const World& world);
// vs_nobody_in_play
bool vs_nobody_in_play(const World& world);
