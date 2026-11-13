#pragma once
// The PC's devices to each player's pad record and mouse aim, once a world step, the port's
// `pads_read` (docs/input.md). Events are seen as they come (input_event) so a press that goes
// down and up between two steps is still read once as held and pressed; the held state, the
// sticks and the triggers are read at the step (read_pads). Players are gubsy's lobby players,
// each with its own devices and binds profile.
#include "input/device_codes.hpp"
#include "input/pad_calibration.hpp"
#include "world/pad_record.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <gubsy/runtime.hpp>
#include <vector>

// How fast the fighter turns for an axis of 1, radians a 60 Hz step, so the mouse's angle can be
// turned into axis values: the twist (or the leg turn under Standard) is turn_deg * pi/180 a step
// (2 * turn_deg a 30 Hz update, actor_camera_input / actor_turn_by_stick), the
// pitch 0.05235988 / 2 (3 degrees an update). 0 when the player has no fighter: the mouse aims
// nothing.
struct AimRates {
    float twist = 0.0f;
    float pitch = 0.0f;
    // how far the twist target may go, radians either way of now: under Preferred the aim stops
    // at 45 degrees either side of the legs (actor_camera_input's clamp), so the target does too
    float twist_low = -1e9f, twist_high = 1e9f;
    // the seat's fighter is in the air under Preferred, where the keys' left stick across turns
    // it (AIR TURN, input/bindings.hpp air_turn_mouse_key)
    bool air_turning = false;
};

struct ReadOptions {
    int cadence = 60;            // the original update rate of what is on screen (60, or 30)
    bool blank = false;          // a fade or a lost-pad pause: no pad is read (pads_read_mask 0)
    unsigned player_mask = 0xF;  // players read (the OR of GameState input masks); others neutral
    bool mouse_captured = false; // relative mouse mode: the mouse aims
    // in a fight (battle, free play, test drive): Escape is Start, so it opens and closes the
    // pause as Start does; everywhere else Escape is Back and never Start, which menus give
    // meanings of its own
    bool fight = false;
    std::array<AimRates, max_pad_players> rates;
};

struct InputDevices {
    std::vector<DeviceCode> taps; // went down since the last read
    float mouse_x = 0.0f;         // counts moved since the last read, right and down
    float mouse_y = 0.0f;
    float wheel = 0.0f; // notches since the last read, away from the player positive
    // each seat's mouse target: what the mouse asked for and the fighter has not turned yet,
    // radians (right and up positive); kept until it is reached
    std::array<float, max_pad_players> owed_twist = {};
    std::array<float, max_pad_players> owed_pitch = {};
    // the last read, for the world (Inputs::pads, Inputs::aims) and the menus
    std::array<PadRecord, max_pad_players> pads;
    std::array<MouseAim, max_pad_players> aims;
    // the PC actions held beside the pad (input/bindings.hpp pc_stand_left ...), for the menus
    std::array<std::uint32_t, max_pad_players> pc_held = {};
    // the pads' stored calibrations by kind (STICK CALIBRATION), read at start
    PadCalibrations calibrations;
};

struct World;
// each seat's rates from the fighter that seat plays (Fighter::controller), zero for a seat with
// none, so a seat's mouse aims its own fighter at its own turn rate
void seat_aim_rates(const World& world, ReadOptions& options);
// each seat's PC control settings (DOUBLE TAP STEP, from the seat's settings profile) into the
// world before a battle starts, where its fighters' spawns read them (World::player_controls)
void pc_controls_commit(World& world, GubsyRuntime& runtime);

// every SDL event: passed on to gubsy (devices coming and going, its menus), presses latched,
// mouse motion and the wheel added up
void input_event(InputDevices& devices, GubsyRuntime& runtime, const SDL_Event& event);
// once before each world step
void read_pads(InputDevices& devices, GubsyRuntime& runtime, const ReadOptions& options);
