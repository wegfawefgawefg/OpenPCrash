#pragma once
// PC CONFIG's BINDINGS page from the inside (pc_settings_binds.cpp, pc_settings_axes.cpp): whose
// binding profile it edits, and its rows past the pad buttons. The rows: PLAYER, the pad buttons
// and PC actions, the axes, DEFAULT.
#include "screens/pc/pc_settings_screen.hpp"

#include <gubsy/runtime.hpp>

// the rows before and after the buttons
inline constexpr int bind_player_row = 0;
inline constexpr int bind_axis_rows = 6; // move stick, aim stick, mouse aim, two triggers, wheel

// the picked player's own profile (player_profile_first + PcSettingsScreen::bind_player); players
// 2..4 read player 1's until they change something
const BindsProfile* pc_binds_profile(FrontEnd& fe);
// the picked player's profile to change: theirs, or a copy of player 1's named for them; false
// without gubsy
bool pc_binds_editable(FrontEnd& fe, BindsProfile& out);
// a changed profile kept (gubsy writes it)
void pc_binds_store(FrontEnd& fe, const BindsProfile& profile);

// PLAYER: whose bindings the page shows, stepped 1..4
PcRowView pc_player_view(FrontEnd& fe);
bool pc_player_step(FrontEnd& fe, int step);

// the axes (axis 0..5): which stick, trigger or mouse drives each, stepped through what the input
// layer reads for it (devices.cpp), NONE or OFF last; true when it changed
PcRowView pc_axis_view(FrontEnd& fe, int axis);
bool pc_axis_step(FrontEnd& fe, int axis, int step);
// Delete on an axis row: nothing drives it
void pc_axis_clear(FrontEnd& fe, int axis);
