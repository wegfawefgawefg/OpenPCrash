#pragma once
// The 20-byte control record: everything a fighter sees of its pilot, human or AI, once a step
// (decomp Actor +0x2F5C `control_axis[4]`, +0x2F6C `control_buttons`; docs/port/gameplay.md,
// "Controls"; docs/input.md here). Humans fill it from the pad in `control_route`
// (world/control_route.hpp, sv_control_route), the AI writes it in its own step, replays
// store and play it back (`replay_input_record`). Retail's axes are floats; here they are
// Q16.16, so the record is still 20 bytes: four Fx and one 32-bit word.
#include "fx/fx.hpp"

#include <cstdint>

// control_axis indices, named as the decomp's AiController names them (include/actor.h +0x60..)
// 0: left stick y, positive forward
inline constexpr int axis_forward = 0;
// 1: turns the legs, heading += 2 turn_deg pi/180 axis an update (actor_turn_by_stick)
inline constexpr int axis_leg_turn = 1;
// 2: camera pitch, 3 degrees an update at full (actor_camera_input)
inline constexpr int axis_pitch = 2;
// 3: aim_yaw, the upper body's twist up to 45 degrees; under control type bit 0 (Standard) the
// strafe instead
inline constexpr int axis_twist = 3;

// control_buttons bits (sv_control_route, mount_fire_bits). "Edge" bits are set only on
// the step the button went down; the rest are levels while held.
inline constexpr std::uint32_t control_dash = 0x1;          // button 1 (left thumb) held
inline constexpr std::uint32_t control_jump_held = 0x2;     // jump binding held
inline constexpr std::uint32_t control_jump_pressed = 0x4;  // edge; the AI sends it as a level
inline constexpr std::uint32_t control_camouflage = 0x8;    // edge
// the steps: 0x10 moves the body to -x, the mech's right (forward is body +z here, -z in the
// original's frame; either way -x is its right), with clip 13; retail's key config calls
// binding_dodge_a RIGHT STEP and a double flick to the right sets it. 0x20 is the left step
inline constexpr std::uint32_t control_step_right = 0x10;   // binding_dodge_a held, or a flick
inline constexpr std::uint32_t control_step_left = 0x20;    // binding_dodge_b held, or a flick
inline constexpr std::uint32_t control_fire_arm_a = 0x40;   // mount 0
inline constexpr std::uint32_t control_fire_arm_b = 0x80;   // mount 1
inline constexpr std::uint32_t control_fire_back_a = 0x100; // mount 2
inline constexpr std::uint32_t control_fire_back_b = 0x200; // mount 3
inline constexpr std::uint32_t control_view_near = 0x400;   // edge, d-pad up (binding_dpad_up)
inline constexpr std::uint32_t control_view_far = 0x800;    // edge, d-pad down (binding_dpad_down)
// edges from the d-pad's sides, fixed (not in the button config). The code gives 0x1000 to the
// d-pad's right (pad bit 8) and 0x2000 to its left (pad bit 4); gameplay.md has them the other way
inline constexpr std::uint32_t control_view_right = 0x1000;
inline constexpr std::uint32_t control_view_left = 0x2000;
inline constexpr std::uint32_t control_recentre = 0x4000; // right thumb held (level)

// the edge bits, cleared when a record is held over a step that didn't build it (the AI's odd
// steps, docs/sixty.md "How the port is laid out")
inline constexpr std::uint32_t control_edges = control_jump_pressed | control_camouflage |
                                               control_view_near | control_view_far |
                                               control_view_right | control_view_left;

// mount_fire_bits, indexed by Weapon.mount 0..3
inline constexpr std::uint32_t mount_fire_bits[4] = {control_fire_arm_a, control_fire_arm_b,
                                                     control_fire_back_a, control_fire_back_b};

struct ControlRecord {
    Fx control_axis[4] = {}; // -1..1 each, see axis_* above
    std::uint32_t control_buttons = 0;
};
static_assert(sizeof(ControlRecord) == 20, "the original's record is 20 bytes");
