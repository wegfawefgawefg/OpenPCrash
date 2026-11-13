#pragma once
// The PC's controls as gubsy bindings (docs/input.md, "Bindings"). Every device drives the same
// virtual Xbox pad: one action per bit of GenkiLib's pad word, so a binding says which pad button
// a key, mouse button or pad button presses, and the game's own button config (the original's
// key config screen) then says what that pad button does in a fight. A few PC actions sit beside
// them: the left and right sticks on keys, a walk key, the conversations' skip chord. The mouse
// is not a stick: it aims the upper body (devices.cpp, "mouse aim").
#include <gubsy/runtime.hpp>

// action ids 0..15 are the pad word's bits (world/pad_record.hpp: 0 d-pad up .. 15 right trigger)
enum class PadAction {
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Start,
    Back,
    LeftThumb,
    RightThumb,
    A,
    B,
    X,
    Y,
    Black,
    White,
    LeftTrigger,
    RightTrigger,
    // a stick on four keys: full lean, as a pad stick past its dead zone
    LeftStickUp,
    LeftStickDown,
    LeftStickLeft,
    LeftStickRight,
    RightStickUp,
    RightStickDown,
    RightStickLeft,
    RightStickRight,
    // the left stick on keys at walk_lean instead (walking needs under 0.7: actor_ground_input)
    Walk,
    // both thumbs and both triggers at once: the conversations' fast-forward (0xC0C0)
    TextSkip,
    // PC: turning the machine on the garage screens' stands (retail has no control for it)
    StandTurnLeft,
    StandTurnRight,
    Count
};

// the PC actions the menus read beside the pad record (InputDevices::pc_held, Ui::pc_held)
inline constexpr unsigned pc_stand_left = 0x1;
inline constexpr unsigned pc_stand_right = 0x2;

// axes: the two sticks (2D), the two triggers (1D), and the mouse wheel stepping the d-pad's up
// and down (1D; the original's view near and far)
inline constexpr int axis_2d_left_stick = 0;
inline constexpr int axis_2d_right_stick = 1;
// the mouse's upper-body aim, bound as a 2D axis so a profile can leave it out
inline constexpr int axis_2d_mouse_aim = 2;
inline constexpr int axis_1d_left_trigger = 0;
inline constexpr int axis_1d_right_trigger = 1;
inline constexpr int axis_1d_wheel_dpad = 2;

// the keys' walk lean: 0.6 of full, under the 0.7 that starts a run
inline constexpr int walk_lean = 0x7FFF * 6 / 10;

// premade profiles, rebuilt every start; the player's own start as a copy of the first
inline constexpr int premade_everything = 100; // keyboard, mouse and pad at once (the default)
inline constexpr int premade_pad = 101;        // an Xbox-layout pad
inline constexpr int premade_keyboard = 102;   // keyboard and mouse
inline constexpr int player_profile_first = 1;

// the per-player stick dead zones (gubsy "Controls", profile scope), percent of each axis's
// travel. The original's inner zone is 50 (0x4000), on both sticks: a square zone per axis that
// keeps forward and turn apart, made for the launch pad's loose sticks, and the default here;
// smaller suits a modern pad. The outer zone is the end of the travel that already reads as full
// (a worn stick that no longer reaches it), 0 as retail. Applied before the pad record is built,
// so they change what a player sends, never how the world steps (docs/input.md).
// the install's: the players' own binding profiles from before TURN STAND were given V and B
// (register_input, once)
inline constexpr const char* stand_keys_key = "phantomcrash.input.stand_keys_given";
// the install's: the players' own profiles from before Q and E (and the shoulders) were moved to
// white and black, the left and right steps (register_input, once)
inline constexpr const char* steps_sided_key = "phantomcrash.input.steps_sided";
inline constexpr const char* arms_sided_key = "phantomcrash.input.arms_sided";
inline constexpr const char* left_dead_zone_key = "phantomcrash.input.left_dead_zone";
inline constexpr const char* right_dead_zone_key = "phantomcrash.input.right_dead_zone";
inline constexpr const char* outer_dead_zone_key = "phantomcrash.input.outer_dead_zone";
// PC: player 1's "AIR TURN": 1 (the default) the mouse alone turns the machine in the air while it
// aims, so A and D (the keys' left stick across) don't fight it; 0 the keys turn there too
inline constexpr const char* air_turn_mouse_key = "phantomcrash.input.air_turn_mouse";
// PC: a player's "DOUBLE TAP STEP": 1 (the default, retail) the double flick of the left stick
// steps under Standard (A or D tapped twice on the keys); 0 it doesn't. It reaches the fight as
// setup (World::player_controls, seat_pc_controls), not through the pad
inline constexpr const char* double_tap_step_key = "phantomcrash.input.double_tap_step";
inline constexpr int dead_zone_default = 50;
inline constexpr int dead_zone_min = 5, dead_zone_max = 50;
inline constexpr int outer_dead_zone_max = 30;
// a dead zone percent in raw axis units: 50 is exactly the original's 0x4000
inline int dead_zone_raw(int percent) {
    return percent == 50 ? 0x4000 : 0x7FFF * percent / 100;
}
// PC: player 1's "Story conversations" and "Battle chatter" (gubsy "Gameplay", profile scope),
// "show" (the default) or "skip" (docs/story.md, "Skipping")
inline constexpr const char* story_talk_key = "phantomcrash.story.conversations";
inline constexpr const char* battle_chatter_key = "phantomcrash.story.battle_chatter";
// the trigger conversations run unseen (src/screens/flow/front_end.cpp)
bool story_talk_skipped(GubsyRuntime& runtime);
// battle barks never start; for battle_barks_update's start, when the battle has one
bool battle_chatter_skipped(GubsyRuntime& runtime);
// --no-dialogue / --dialogue: both set to skip (or show) in player 1's settings profile, saved
void story_talk_settings_write(GubsyRuntime& runtime, bool skip);
// the mouse's turn per count at sensitivity 1, radians (about 0.05 degrees)
inline constexpr float mouse_radians_per_count = 0.000873f;

// the schema, the premade profiles, player 1 on its profile with the keyboard and mouse, and the
// rate cap setting. Once, after init_gubsy_runtime.
void register_input(GubsyRuntime& runtime);
