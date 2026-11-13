// BINDINGS' player and axis rows (pc_binds.hpp): whose profile the page edits, and which pad stick,
// trigger or mouse drives each of the profile's axes (input/bindings.hpp). The input layer reads a
// stick axis from either pad stick, a trigger axis from either trigger, the mouse aim from the
// mouse and the wheel's view steps from the wheel (input/devices.cpp), so those are the choices.
#include "input/bindings.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/pc_binds.hpp"

#include <ginput/ginput.hpp>
#include <gubsy/input/types.hpp>
#include <string>

namespace {

using D1 = Gubsy1DAnalog;
using D2 = Gubsy2DAnalog;

struct AxisRow {
    const char* label;
    bool two_d;
    int axis;
    int devices[2]; // what may drive it, -1 for none
    const char* names[2];
    const char* none; // the value with nothing bound
    int icon;         // the pad button's icon before the label, -1 none
    const char* help;
};

constexpr AxisRow axis_rows[bind_axis_rows] = {
    {"MOVE STICK",
     true,
     axis_2d_left_stick,
     {static_cast<int>(D2::GP_LEFT_STICK), static_cast<int>(D2::GP_RIGHT_STICK)},
     {"LS", "RS"},
     "NONE",
     -1,
     "The pad stick that walks and runs. LS is the original's."},
    {"AIM STICK",
     true,
     axis_2d_right_stick,
     {static_cast<int>(D2::GP_RIGHT_STICK), static_cast<int>(D2::GP_LEFT_STICK)},
     {"RS", "LS"},
     "NONE",
     -1,
     "The pad stick that aims, and turns under STANDARD. RS is the original's."},
    {"MOUSE AIM",
     true,
     axis_2d_mouse_aim,
     {static_cast<int>(D2::MOUSE_XY), -1},
     {"ON", ""},
     "OFF",
     -1,
     "The mouse turns and tilts the upper body in a fight."},
    {"LEFT TRIGGER",
     false,
     axis_1d_left_trigger,
     {static_cast<int>(D1::GP_LEFT_TRIGGER), static_cast<int>(D1::GP_RIGHT_TRIGGER)},
     {"LT", "RT"},
     "NONE",
     3125,
     "The pad trigger that pulls the left trigger, by how far."},
    {"RIGHT TRIGGER",
     false,
     axis_1d_right_trigger,
     {static_cast<int>(D1::GP_RIGHT_TRIGGER), static_cast<int>(D1::GP_LEFT_TRIGGER)},
     {"RT", "LT"},
     "NONE",
     3126,
     "The pad trigger that pulls the right trigger, by how far."},
    {"WHEEL",
     false,
     axis_1d_wheel_dpad,
     {static_cast<int>(D1::MOUSE_WHEEL), -1},
     {"VIEW", ""},
     "OFF",
     -1,
     "The mouse wheel brings the view nearer and takes it farther."},
};

// the row's choice now: 0 or 1 its device, 2 nothing (the first bound of its devices)
int choice_of(const BindsProfile& profile, const AxisRow& row) {
    for (int c = 0; c < 2; ++c) {
        if (row.devices[c] < 0) {
            continue;
        }
        if (row.two_d) {
            for (const ginput::Axis2DBind& bind : ginput::binds_for_axis_2d(profile, row.axis)) {
                if (bind.device_stick == row.devices[c]) {
                    return c;
                }
            }
        } else {
            for (const ginput::Axis1DBind& bind : ginput::binds_for_axis_1d(profile, row.axis)) {
                if (bind.device_axis == row.devices[c]) {
                    return c;
                }
            }
        }
    }
    return 2;
}

// the axis driven by choice c alone (2: by nothing)
void bind_choice(BindsProfile& profile, const AxisRow& row, int c) {
    if (row.two_d) {
        const std::vector<ginput::Axis2DBind> binds = ginput::binds_for_axis_2d(profile, row.axis);
        for (const ginput::Axis2DBind& bind : binds) {
            (void)ginput::remove_axis_2d_bind(profile, bind);
        }
        if (c < 2 && row.devices[c] >= 0) {
            ginput::Axis2DBind bind;
            bind.device_stick = row.devices[c];
            bind.axis_2d = row.axis;
            (void)ginput::add_axis_2d_bind(profile, bind);
        }
        return;
    }
    const std::vector<ginput::Axis1DBind> binds = ginput::binds_for_axis_1d(profile, row.axis);
    for (const ginput::Axis1DBind& bind : binds) {
        (void)ginput::remove_axis_1d_bind(profile, bind);
    }
    if (c < 2 && row.devices[c] >= 0) {
        ginput::Axis1DBind bind;
        bind.device_axis = row.devices[c];
        bind.axis_1d = row.axis;
        (void)ginput::add_axis_1d_bind(profile, bind);
    }
}

int own_id(const FrontEnd& fe) {
    return player_profile_first + fe.screen.pc.bind_player;
}

} // namespace

const BindsProfile* pc_binds_profile(FrontEnd& fe) {
    if (!fe.pc || !fe.pc->gubsy) {
        return nullptr;
    }
    if (const BindsProfile* own = gubsy_find_binds_profile(*fe.pc->gubsy, own_id(fe))) {
        return own;
    }
    return gubsy_find_binds_profile(*fe.pc->gubsy, player_profile_first);
}

bool pc_binds_editable(FrontEnd& fe, BindsProfile& out) {
    if (!fe.pc || !fe.pc->gubsy) {
        return false;
    }
    GubsyRuntime& gubsy = *fe.pc->gubsy;
    if (const BindsProfile* own = gubsy_find_binds_profile(gubsy, own_id(fe))) {
        out = *own;
        return true;
    }
    // a player's first change: player 1's bindings become theirs
    const BindsProfile* base = gubsy_find_binds_profile(gubsy, player_profile_first);
    if (!base) {
        base = gubsy_find_binds_profile(gubsy, premade_everything);
    }
    if (!base) {
        return false;
    }
    out = *base;
    out.id = own_id(fe);
    out.name = "Player " + std::to_string(fe.screen.pc.bind_player + 1);
    return true;
}

void pc_binds_store(FrontEnd& fe, const BindsProfile& profile) {
    if (fe.pc && fe.pc->gubsy) {
        (void)gubsy_replace_binds_profile(*fe.pc->gubsy, profile);
    }
}

PcRowView pc_player_view(FrontEnd& fe) {
    PcRowView view;
    view.label = "PLAYER";
    const int player = fe.screen.pc.bind_player;
    view.value = std::to_string(player + 1);
    const bool own =
        fe.pc && fe.pc->gubsy && gubsy_find_binds_profile(*fe.pc->gubsy, own_id(fe)) != nullptr;
    view.help = player == 0
                    ? "Player 1's bindings: alone, and P1 in VS. Each VS player has their own."
                : own ? "Player " + std::to_string(player + 1) + "'s own bindings, for VS."
                      : "Player " + std::to_string(player + 1) +
                            " plays with player 1's bindings until one is changed here.";
    return view;
}

bool pc_player_step(FrontEnd& fe, int step) {
    int& player = fe.screen.pc.bind_player;
    const int next = (player + step + max_pad_players) % max_pad_players;
    if (next == player) {
        return false;
    }
    player = next;
    return true;
}

PcRowView pc_axis_view(FrontEnd& fe, int axis) {
    PcRowView view;
    if (axis < 0 || axis >= bind_axis_rows) {
        return view;
    }
    const AxisRow& row = axis_rows[axis];
    view.label = row.label;
    view.icon = row.icon;
    view.help = row.help;
    const BindsProfile* profile = pc_binds_profile(fe);
    const int c = profile ? choice_of(*profile, row) : 2;
    view.value = c < 2 ? row.names[c] : row.none;
    return view;
}

bool pc_axis_step(FrontEnd& fe, int axis, int step) {
    if (axis < 0 || axis >= bind_axis_rows || step == 0) {
        return false;
    }
    const AxisRow& row = axis_rows[axis];
    BindsProfile profile;
    if (!pc_binds_editable(fe, profile)) {
        return false;
    }
    // the choices in turn, nothing last, round
    int choices[3];
    int count = 0;
    for (int c = 0; c < 2; ++c) {
        if (row.devices[c] >= 0) {
            choices[count++] = c;
        }
    }
    choices[count++] = 2;
    const int now = choice_of(profile, row);
    int at = 0;
    for (int i = 0; i < count; ++i) {
        if (choices[i] == now) {
            at = i;
        }
    }
    bind_choice(profile, row, choices[(at + (step > 0 ? 1 : count - 1)) % count]);
    pc_binds_store(fe, profile);
    return true;
}

void pc_axis_clear(FrontEnd& fe, int axis) {
    BindsProfile profile;
    if (axis < 0 || axis >= bind_axis_rows || !pc_binds_editable(fe, profile)) {
        return;
    }
    bind_choice(profile, axis_rows[axis], 2);
    pc_binds_store(fe, profile);
}
