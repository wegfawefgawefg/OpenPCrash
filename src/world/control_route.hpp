#pragma once
// sv_control_route: a human fighter's pad turned into its control record at the start
// of its step. It lives in the world because it reads the fighter (airborne, control type, the
// stick-flick state, whether it may move at all) and keeps the twist axis from the step before in
// the air; the pad record it reads is the world's input (Inputs::pads). docs/input.md. It reads
// only the pad's `b.cur`, `b.rising` and both sticks' x and y, and the MouseAim: what an online
// seat sends (world/pad_record.hpp). Keep it to those.
#include "world/control_record.hpp"
#include "world/pad_record.hpp"

#include <array>
#include <cstdint>

// The fighter's button masks over the pad word, built from the player's button config at spawn
// (actor_init, Actor +0x2F78..+0x2FA0). A mask of 0 never fires.
struct ControlBindings {
    std::uint16_t fire[4] = {}; // binding_fire_a..d: arm A, arm B, back A, back B
    std::uint16_t jump = 0;
    std::uint16_t button1 = pad_left_thumb; // dash, fixed 0x40
    std::uint16_t dodge_a = 0;              // right step
    std::uint16_t dodge_b = 0;              // left step
    std::uint16_t camo = 0;
    std::uint16_t dpad_up = pad_dpad_up;     // fixed 1
    std::uint16_t dpad_down = pad_dpad_down; // fixed 2
};

// The button config's key indices (the KeyBlock at GameState +0x10FDC per slot) and the masks
// they stand for: `sv_trigger_bit_of` reads D_001DD470, A B X Y, left and right trigger, white,
// black. The defaults are D_001DD490, also what the AI and the free play lessons (but lesson 3)
// get: fire arm A on the right trigger, arm B left trigger, back A on B, back B on Y, right step
// black, left step white, jump A, camouflage X (the key config's rows: key_config_sprites_2).
inline constexpr std::array<std::uint16_t, 8> key_index_bits = {
    pad_a, pad_b, pad_x, pad_y, pad_left_trigger, pad_right_trigger, pad_white, pad_black};
inline constexpr std::array<int, 8> default_key_block = {5, 4, 1, 3, 7, 6, 0, 2};
// the order of the eight keys in a KeyBlock
enum class KeyAction { FireArmA, FireArmB, FireBackA, FireBackB, StepRight, StepLeft, Jump, Camo };

ControlBindings control_bindings_from_keys(const std::array<int, 8>& keys);

// control_type (Actor +0x2740, GameState +0x10FD8 per player): bit 0 Standard (the left stick
// strafes, the right stick turns), bit 1 View Change Normal (pitch negated). 3 Standard and
// Normal, 0 Preferred and Reverse; the AI is 0.
inline constexpr int control_type_standard = 1;
inline constexpr int control_type_view_normal = 2;

// The double flick of the left stick that steps under Standard (Actor +0x2538, +0x253C),
// cleared with the Actor at spawn
struct StickFlick {
    std::int32_t state = 0;  // stick_dodge_state 0..4
    std::int32_t window = 0; // stick_dodge_window, steps left
};

// What the route reads of the fighter, read by the caller before the fighter's step
struct RouteFighter {
    int control_type = 0;
    bool airborne = false;
    // door_progress == 2, a scene below 0x18, VS once vs_end_state >= 1, or not alive: the record
    // goes to zero and nothing else changes
    bool blocked = false;
};

// Builds this step's record into `record`; the twist axis carries over from the step before
// while airborne under Standard, as retail's does. `aim` (the mouse) adds to the axes the right
// stick drives: the twist under Preferred, the leg turn under Standard (where retail has no
// twist), and the pitch with up as up.
void control_route(ControlRecord& record, const PadRecord& pad, const MouseAim& aim,
                   const ControlBindings& bindings, const RouteFighter& fighter, StickFlick& flick);
