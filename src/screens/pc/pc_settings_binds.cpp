// PC CONFIG's BINDINGS page: the picked player's own binding profile (ids 1..4,
// input/bindings.hpp), a row per pad button of the virtual pad every device drives, then the axes
// (pc_settings_axes.cpp). Accept listens: the next key or mouse button pressed replaces the row's
// keys and mouse buttons, the next pad button its pad buttons; Escape stops listening. Delete
// clears a row's keys, Shift+Delete or a pad's Y its pad buttons. The fight's meaning of the eight
// buttons follows the original's BUTTON CONFIG, so a row says what its button does now.
#include "audio/sound_cues.hpp"
#include "input/bindings.hpp"
#include "input/device_codes.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/pc_binds.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <ginput/ginput.hpp>
#include <gubsy/input/types.hpp>
#include <iterator>

namespace {

using A = PadAction;

// the rows, in the original's button order, then the PC's own actions
constexpr PadAction bind_rows[] = {
    A::A,
    A::B,
    A::X,
    A::Y,
    A::RightTrigger,
    A::LeftTrigger,
    A::Black,
    A::White,
    A::LeftThumb,
    A::RightThumb,
    A::Start,
    A::Back,
    A::DpadUp,
    A::DpadDown,
    A::DpadLeft,
    A::DpadRight,
    A::LeftStickUp,
    A::LeftStickDown,
    A::LeftStickLeft,
    A::LeftStickRight,
    A::Walk,
    A::RightStickUp,
    A::RightStickDown,
    A::RightStickLeft,
    A::RightStickRight,
    A::TextSkip,
    A::StandTurnLeft,
    A::StandTurnRight,
};
constexpr int bind_row_count = static_cast<int>(std::size(bind_rows));
// the page's rows: PLAYER, the buttons, the axes, DEFAULT
constexpr int first_button_row = bind_player_row + 1;
constexpr int first_axis_row = first_button_row + bind_row_count;
constexpr int default_row = first_axis_row + bind_axis_rows;

// a page row's button (index into bind_rows), -1 for the other rows
int button_of(int row) {
    const int b = row - first_button_row;
    return b >= 0 && b < bind_row_count ? b : -1;
}

// the button config's eight (key indices 0..7: A B X Y, left and right trigger, white, black) and
// their icons (scr_common_tex)
constexpr PadAction key_index_action[8] = {
    A::A, A::B, A::X, A::Y, A::LeftTrigger, A::RightTrigger, A::White, A::Black};
constexpr int key_index_icon[8] = {3121, 3122, 3129, 3130, 3125, 3126, 3128, 3124};
// what each of the eight does (KeyAction order, world/control_route.hpp)
constexpr const char* key_action_name[8] = {"RIGHT ARM",     "LEFT ARM",   "RIGHT BACKPACK",
                                            "LEFT BACKPACK", "RIGHT STEP", "LEFT STEP",
                                            "JUMP",          "CAMOUFLAGE"};

const char* pad_names[] = {"A",     "B",    "X",  "Y",  "BACK", "GUIDE", "START",
                           "LS",    "RS",   "LB", "RB", "UP",   "DOWN",  "LEFT",
                           "RIGHT", "MISC", "P1", "P2", "P3",   "P4",    "PAD"};

int key_index_of(PadAction action) {
    for (int k = 0; k < 8; ++k) {
        if (key_index_action[k] == action) {
            return k;
        }
    }
    return -1;
}

// the row's words: the button config's meaning for the eight, the rest fixed
std::string action_label(const FrontEnd& fe, PadAction action) {
    const ButtonConfig& buttons = fe.world->profile.options.buttons;
    const int k = key_index_of(action);
    if (k >= 0) {
        for (int a = 0; a < 8; ++a) {
            if (buttons.keys[a] == k) {
                return key_action_name[a];
            }
        }
        return "NOTHING";
    }
    const bool preferred = buttons.control == 1;
    switch (action) {
    case A::LeftThumb:
        return "DASH";
    case A::RightThumb:
        return "RECENTRE VIEW";
    case A::Start:
        return "PAUSE";
    case A::Back:
        return "MENU BACK";
    case A::DpadUp:
        return "VIEW NEARER";
    case A::DpadDown:
        return "VIEW FARTHER";
    case A::DpadLeft:
        return "LAST VIEW";
    case A::DpadRight:
        return "NEXT VIEW";
    case A::LeftStickUp:
        return "FORWARD";
    case A::LeftStickDown:
        return "BACK";
    case A::LeftStickLeft:
        return preferred ? "TURN LEFT" : "STRAFE LEFT";
    case A::LeftStickRight:
        return preferred ? "TURN RIGHT" : "STRAFE RIGHT";
    case A::Walk:
        return "WALK";
    case A::RightStickUp:
        return "AIM UP";
    case A::RightStickDown:
        return "AIM DOWN";
    case A::RightStickLeft:
        return preferred ? "AIM LEFT" : "TURN LEFT";
    case A::RightStickRight:
        return preferred ? "AIM RIGHT" : "TURN RIGHT";
    case A::TextSkip:
        return "SKIP TEXT";
    case A::StandTurnLeft:
        return "TURN STAND LEFT";
    case A::StandTurnRight:
        return "TURN STAND RIGHT";
    default:
        return "";
    }
}

std::string upper(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}

// a key name's signs the label font lacks, spelled out: "[" is L BRACKET, "KEYPAD *" KEYPAD STAR
std::string spelled(const PcArt& art, const std::string& name) {
    static const struct {
        char c;
        const char* word;
    } words[] = {
        {'[', "L BRACKET"}, {']', "R BRACKET"},  {';', "SEMICOLON"}, {'\'', "APOSTROPHE"},
        {'`', "GRAVE"},     {'\\', "BACKSLASH"}, {'=', "EQUALS"},    {'*', "STAR"},
        {'#', "HASH"},      {'&', "AND"},        {'(', "L PAREN"},   {')', "R PAREN"},
        {'%', "PERCENT"},   {'!', "EXCLAIM"},    {'@', "AT"},        {'$', "DOLLAR"},
        {'^', "CARET"},     {'_', "UNDERSCORE"}, {'<', "LESS"},      {'>', "GREATER"},
        {'?', "QUESTION"},  {'|', "BAR"},        {'{', "L BRACE"},   {'}', "R BRACE"},
        {'~', "TILDE"},     {'"', "QUOTE"},
    };
    std::string out;
    for (const char c : name) {
        if (pc_text_drawable(art, c)) {
            out += c;
            continue;
        }
        const char* word = "KEY";
        for (const auto& w : words) {
            if (w.c == c) {
                word = w.word;
            }
        }
        // a word between others keeps a space either side
        if (!out.empty() && out.back() != ' ') {
            out += ' ';
        }
        out += word;
        out += ' ';
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

// a bound control in the key config's letters
std::string control_name(const PcArt& art, int encoded) {
    DeviceCode code;
    if (!device_code_of(encoded, code)) {
        return "?";
    }
    switch (code.kind) {
    case ginput::DeviceKind::Keyboard: {
        std::string name = upper(SDL_GetScancodeName(static_cast<SDL_Scancode>(code.code)));
        if (name.rfind("LEFT ", 0) == 0) {
            name = "L " + name.substr(5);
        } else if (name.rfind("RIGHT ", 0) == 0) {
            name = "R " + name.substr(6);
        } else if (name == "RETURN") {
            name = "ENTER";
        } else if (name == "ESCAPE") {
            name = "ESC";
        } else if (name == "PAGEUP") {
            name = "PG UP";
        } else if (name == "PAGEDOWN") {
            name = "PG DOWN";
        } else if (name == "BACKSPACE") {
            name = "BKSP";
        }
        return spelled(art, name);
    }
    case ginput::DeviceKind::Mouse: {
        const char* mouse[] = {"?", "L CLICK", "M CLICK", "R CLICK", "MOUSE 4", "MOUSE 5"};
        return code.code >= 1 && code.code <= 5 ? mouse[code.code] : "MOUSE";
    }
    case ginput::DeviceKind::Gamepad:
        return code.code >= 0 && code.code < static_cast<int>(std::size(pad_names))
                   ? pad_names[code.code]
                   : "PAD";
    default:
        return "?";
    }
}

bool is_pad(int encoded) {
    DeviceCode code;
    return device_code_of(encoded, code) && code.kind == ginput::DeviceKind::Gamepad;
}

// gubsy's own code for an SDL control (the inverse of device_code_of), -1 none
int encode_control(ginput::DeviceKind kind, int sdl_code) {
    for (int b = 0; b < static_cast<int>(GubsyButton::COUNT); ++b) {
        DeviceCode code;
        if (device_code_of(b, code) && code.kind == kind && code.code == sdl_code) {
            return b;
        }
    }
    return -1;
}

// the row's binds of one kind replaced by `encoded` (-1: only cleared)
void rebind(FrontEnd& fe, PadAction action, bool pad, int encoded) {
    BindsProfile profile;
    if (!pc_binds_editable(fe, profile)) {
        return;
    }
    const int id = static_cast<int>(action);
    const std::vector<ginput::ButtonBind> binds = ginput::button_binds_for_action(profile, id);
    for (const ginput::ButtonBind& bind : binds) {
        if (is_pad(bind.device_button) == pad) {
            (void)ginput::remove_button_bind(profile, bind);
        }
    }
    if (encoded >= 0) {
        (void)ginput::add_button_bind(profile, {encoded, id});
    }
    pc_binds_store(fe, profile);
}

// the other rows the control also presses, for the help line
std::string also_bound(FrontEnd& fe, int encoded, PadAction action) {
    const BindsProfile* profile = pc_binds_profile(fe);
    std::string out;
    for (const PadAction other : bind_rows) {
        if (other == action || !profile) {
            continue;
        }
        for (const ginput::ButtonBind& bind :
             ginput::button_binds_for_action(*profile, static_cast<int>(other))) {
            if (bind.device_button == encoded) {
                out += (out.empty() ? "" : ", ") + action_label(fe, other);
            }
        }
    }
    return out;
}

void bind_pressed(FrontEnd& fe, Ui& ui, ginput::DeviceKind kind, int sdl_code) {
    PcSettingsScreen& s = fe.screen.pc;
    s.listening = false;
    const int encoded = encode_control(kind, sdl_code);
    const int button = button_of(s.row);
    if (encoded < 0 || button < 0) {
        s.note = "That can't be bound.";
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        return;
    }
    const PadAction action = bind_rows[button];
    rebind(fe, action, kind == ginput::DeviceKind::Gamepad, encoded);
    const std::string others = also_bound(fe, encoded, action);
    s.note = others.empty() ? "Bound." : "Bound. It also does: " + others + ".";
    s.quiet = 20;
    ui_sound(ui, bank_sys, se_menu_accept);
}

} // namespace

int pc_binds_count() {
    return default_row + 1;
}

PcRowView pc_bind_view(FrontEnd& fe, int row) {
    if (row == bind_player_row) {
        PcRowView view = pc_player_view(fe);
        view.arrows = true;
        return view;
    }
    if (row >= first_axis_row && row < default_row) {
        PcRowView view = pc_axis_view(fe, row - first_axis_row);
        view.arrows = true;
        return view;
    }
    PcRowView view;
    view.arrows = false;
    const int button = button_of(row);
    if (button < 0) {
        view.label = "DEFAULT";
        view.help = "Put this player's bindings back as they were.";
        return view;
    }
    const PadAction action = bind_rows[button];
    view.label = action_label(fe, action);
    const int k = key_index_of(action);
    view.icon = k >= 0 ? key_index_icon[k] : -1;
    if (const BindsProfile* profile = pc_binds_profile(fe)) {
        for (const ginput::ButtonBind& bind :
             ginput::button_binds_for_action(*profile, static_cast<int>(action))) {
            std::string& column = is_pad(bind.device_button) ? view.value_2 : view.value;
            column += (column.empty() ? "" : "/") + control_name(fe.pc_art, bind.device_button);
        }
    }
    view.help = "Accept, then press the key, mouse button or pad button for it. Delete clears "
                "the keys, Shift+Delete or Y the pad buttons.";
    return view;
}

bool pc_bind_step(FrontEnd& fe, int row, int step) {
    if (row == bind_player_row) {
        return pc_player_step(fe, step);
    }
    if (row >= first_axis_row && row < default_row) {
        return pc_axis_step(fe, row - first_axis_row, step);
    }
    return false;
}

void pc_bind_accept(FrontEnd& fe, int row) {
    PcSettingsScreen& s = fe.screen.pc;
    if (button_of(row) >= 0) {
        s.listening = true;
        s.listen_ticks = 6 * 60;
        s.note = "Press a key, mouse button or pad button. Escape stops.";
        return;
    }
    if (row != default_row || !fe.pc || !fe.pc->gubsy) {
        return;
    }
    // DEFAULT: the keyboard, mouse and pad premade, as the picked player's own
    BindsProfile profile;
    const BindsProfile* premade = gubsy_find_binds_profile(*fe.pc->gubsy, premade_everything);
    if (premade && pc_binds_editable(fe, profile)) {
        const int id = profile.id;
        const std::string name = profile.name;
        profile = *premade;
        profile.id = id;
        profile.name = name;
        pc_binds_store(fe, profile);
        s.note = "Every binding is back as it was.";
    }
}

bool pc_bind_event(FrontEnd& fe, Ui& ui, const SDL_Event& event) {
    PcSettingsScreen& s = fe.screen.pc;
    if (!s.listening) {
        // on a row: Delete clears its keys and mouse buttons (an axis: nothing drives it), Shift+
        // Delete or a pad's Y its pad buttons
        if (!s.in_rows) {
            return false;
        }
        const bool del = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                         event.key.scancode == SDL_SCANCODE_DELETE;
        const bool pad_y = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                           event.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH;
        const bool shift = del && (event.key.mod & SDL_KMOD_SHIFT) != 0;
        const int button = button_of(s.row);
        if (button >= 0 && (del || pad_y)) {
            rebind(fe, bind_rows[button], shift || pad_y, -1);
            s.note = shift || pad_y ? "The pad buttons are cleared." : "The keys are cleared.";
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            return true;
        }
        if (s.row >= first_axis_row && s.row < default_row && del) {
            pc_axis_clear(fe, s.row - first_axis_row);
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            return true;
        }
        return false;
    }
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        if (event.key.repeat) {
            return true;
        }
        if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            s.listening = false;
            s.note.clear();
            s.quiet = 20;
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            return true;
        }
        bind_pressed(fe, ui, ginput::DeviceKind::Keyboard, static_cast<int>(event.key.scancode));
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        bind_pressed(fe, ui, ginput::DeviceKind::Mouse, static_cast<int>(event.button.button));
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        bind_pressed(fe, ui, ginput::DeviceKind::Gamepad, static_cast<int>(event.gbutton.button));
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        return true;
    default:
        // releases go on, so nothing is left held that was pressed before listening began
        return false;
    }
}
