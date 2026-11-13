// PC CONFIG's STICK CALIBRATION (docs/settings.md, docs/input.md "Calibration"): the INPUT row
// picks a pad, A opens this panel over the rows. Three steps: the sticks let go (their centres and
// the triggers' rest, averaged over the last 16 reads), turned round their edges and the triggers
// pulled (each axis's reach either way, the triggers' full pull), then kept for that pad
// (input/pad_calibration.hpp) and shown through the result. Drawn from the fight's own art: the
// radar's ring for each stick's travel, the tracking dot for where it is and how far it has
// reached, the sound config's level bar for the triggers, the key config's letters.
#include "audio/sound_cues.hpp"
#include "input/bindings.hpp"
#include "input/pad_calibration.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/pc_settings_screen.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

// game_tex (0x671): the radar's 160x160 ring (0x570), the tracking box's dot (0x56b) and its
// corner (0x561, 32x32), and back mount 0's lock mark (0x56e, a 16x16 corner triangle)
constexpr int game_sheet = 0x671;
constexpr int ring_sprite = 0x570, dot_sprite = 0x56b, corner_sprite = 0x561, mark_sprite = 0x56e;
constexpr float pi = 3.14159265f;
// the sound config's level bar and its fill, as the rows' volumes
constexpr int volume_bar = 2509, volume_fill = 0x9db;

// the panel in the rows' place (pc_settings_screen.cpp: x 252, width 356, from y 98)
constexpr float panel_x = 252, panel_w = 356, panel_y = 98;
constexpr float ring_scale = 0.75f;
constexpr float ring_y = panel_y + 104;
constexpr float travel = 80.0f * ring_scale * 0.94f; // a full lean, pixels from the ring's middle
constexpr float trigger_y = panel_y + 196;
// the ring's middle for each stick
float ring_x(int stick) {
    return panel_x + panel_w * (stick == 0 ? 0.25f : 0.75f);
}

// how much of the measured reach a full lean takes, so the edge is reached without straining
constexpr int reach_kept = 97;
// a stick axis must reach this far from its rest either way before it is kept
constexpr int reach_needed = 0x3000;

const SDL_GamepadAxis axes[6] = {SDL_GAMEPAD_AXIS_LEFTX,        SDL_GAMEPAD_AXIS_LEFTY,
                                 SDL_GAMEPAD_AXIS_RIGHTX,       SDL_GAMEPAD_AXIS_RIGHTY,
                                 SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};

std::vector<SDL_JoystickID> plugged_pads() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    std::vector<SDL_JoystickID> pads(ids, ids + std::max(count, 0));
    SDL_free(ids);
    return pads;
}

// the pad's six axes now; false when it's gone
bool read_axes(SDL_JoystickID id, int out[6]) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(id);
    if (!pad || !SDL_GamepadConnected(pad)) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        out[i] = SDL_GetGamepadAxis(pad, axes[i]);
    }
    return true;
}

// the calibration the panel measured, each reach a little short of what was reached
PadCalibration measured(const CalibrationPanel& c) {
    PadCalibration out;
    for (int i = 0; i < 4; ++i) {
        AxisCalibration& axis = out.stick[i / 2][i % 2];
        axis.centre = c.centre[i];
        axis.low = c.centre[i] - (c.centre[i] - c.low[i]) * reach_kept / 100;
        axis.high = c.centre[i] + (c.high[i] - c.centre[i]) * reach_kept / 100;
    }
    for (int t = 0; t < 2; ++t) {
        const int pull = c.trigger_max[t] - c.trigger_rest[t];
        if (pull >= reach_needed) {
            out.trigger_rest[t] = c.trigger_rest[t] + pull * (100 - reach_kept) / 100;
            out.trigger_full[t] = c.trigger_rest[t] + pull * reach_kept / 100;
        }
    }
    return out;
}

void start_rest(CalibrationPanel& c) {
    c.phase = 0;
    c.rest_count = 0;
}

void start_turning(CalibrationPanel& c) {
    const int n = std::clamp(c.rest_count, 1, 16);
    for (int i = 0; i < 4; ++i) {
        long long sum = 0;
        for (int k = 0; k < n; ++k) {
            sum += c.rest[k][i];
        }
        c.centre[i] = c.low[i] = c.high[i] = static_cast<int>(sum / n);
    }
    for (int t = 0; t < 2; ++t) {
        int rest = 0;
        for (int k = 0; k < n; ++k) {
            rest = std::max(rest, c.rest[k][4 + t]);
        }
        c.trigger_rest[t] = c.trigger_max[t] = rest;
    }
    for (auto& stick : c.reach) {
        std::fill(std::begin(stick), std::end(stick), 0.0f);
    }
    c.phase = 1;
}

// the readings of one update into the panel
void sample(CalibrationPanel& c, const int raw[6]) {
    if (c.phase == 0) {
        for (int i = 0; i < 6; ++i) {
            c.rest[c.rest_count % 16][i] = raw[i];
        }
        ++c.rest_count;
        return;
    }
    if (c.phase != 1) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        c.low[i] = std::min(c.low[i], raw[i]);
        c.high[i] = std::max(c.high[i], raw[i]);
    }
    for (int t = 0; t < 2; ++t) {
        c.trigger_max[t] = std::max(c.trigger_max[t], raw[4 + t]);
    }
    // each stick's lean from its rest, kept by direction for the drawn outline
    for (int s = 0; s < 2; ++s) {
        const float x = static_cast<float>(raw[s * 2] - c.centre[s * 2]) / 32768.0f;
        const float y = static_cast<float>(raw[s * 2 + 1] - c.centre[s * 2 + 1]) / 32768.0f;
        const float lean = std::sqrt(x * x + y * y);
        if (lean < 0.2f) {
            continue;
        }
        const float turn = std::atan2(y, x) / 6.2831853f + 1.0f;
        const int sector = static_cast<int>(turn * 32.0f + 0.5f) % 32;
        c.reach[s][sector] = std::max(c.reach[s][sector], std::min(lean, 1.2f));
    }
}

// each stick axis reached far enough both ways
bool turned_enough(const CalibrationPanel& c) {
    for (int i = 0; i < 4; ++i) {
        if (c.high[i] - c.centre[i] < reach_needed || c.centre[i] - c.low[i] < reach_needed) {
            return false;
        }
    }
    return true;
}

// the pad put back as it came: no calibration of its own or for its kind
void reset(FrontEnd& fe, Ui& ui) {
    CalibrationPanel& c = fe.screen.pc.calibration;
    if (fe.pc->calibrations && fe.pc->gubsy) {
        pad_calibration_store(*fe.pc->calibrations, *fe.pc->gubsy, c.pad, nullptr);
    }
    start_rest(c);
    c.note = "This pad reads as it came again. Let go of the sticks, then press A.";
    ui_sound(ui, bank_sys, se_message_box);
    menu_input_mark(ui);
}

// the pad's name in the label font's letters, cut to the panel's width
std::string label_name(const PcArt& art, const char* name) {
    std::string out;
    for (const char* c = name ? name : ""; *c; ++c) {
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
        if (pc_text_drawable(art, up)) {
            out += up;
        }
    }
    while (!out.empty() && static_cast<float>(pc_text_width(art, out)) > panel_w - 70) {
        out.pop_back();
    }
    return out;
}

void sprite_at(Ui& ui, int id, float x, float y, float scale, std::uint32_t colour, int blend) {
    Sprite s = sprite_of(ui, id);
    s.pos[0] = x;
    s.pos[1] = y;
    s.scale[0] = s.scale[1] = scale;
    s.color = colour;
    s.blend = blend;
    s.flags = 0x1c; // centred on its middle, filtered
    sprite_submit(ui, s);
}

// a sprite centred on x, y, scaled (a negative x mirrors it) and turned
void piece_at(Ui& ui, int id, float x, float y, float sx, float sy, float turn,
              std::uint32_t colour, int blend, std::uint32_t flags) {
    Sprite s = sprite_of(ui, id);
    s.pos[0] = x;
    s.pos[1] = y;
    s.scale[0] = sx;
    s.scale[1] = sy;
    s.rot[2] = turn;
    s.color = colour;
    s.blend = blend;
    s.flags = flags;
    sprite_submit(ui, s);
}

// the dead zone, a square `half` either way of x, y, outlined by the tracking box's four corners
// as hud_draw_tracking_box sets them (the right ones mirrored, the bottom ones turned over)
void zone_corners(Ui& ui, float x, float y, float half) {
    const float scale = std::clamp(half / 28.0f, 0.3f, 0.6f);
    const float in = half - 16.0f * scale;
    const struct {
        float sx, sy, flip, turn;
    } corners[4] = {{-1, -1, 1, 0}, {1, -1, -1, 0}, {1, 1, 1, pi}, {-1, 1, -1, pi}};
    for (const auto& c : corners) {
        piece_at(ui, corner_sprite, x + c.sx * in, y + c.sy * in, c.flip * scale, scale, c.turn,
                 0xa0ffffff, 0x102, 0x1c);
    }
}

// the stick's place: the lock marks' four corners 9 px round it (lock_mark_corners, each turned
// a quarter more), the tracking dot in the middle
void stick_marker(Ui& ui, float x, float y, std::uint32_t mark, std::uint32_t dot) {
    const float corners[4][2] = {{-9, -9}, {9, -9}, {9, 9}, {-9, 9}};
    for (int i = 0; i < 4; ++i) {
        piece_at(ui, mark_sprite, x + corners[i][0], y + corners[i][1], 1, 1,
                 static_cast<float>(i) * pi * 0.5f, mark, 0x104, 0x1c);
    }
    sprite_at(ui, dot_sprite, x, y, 0.8f, dot, 0x104);
}

void text_centred(Ui& ui, const PcArt& art, const std::string& text, float x, float y,
                  std::uint32_t colour = 0xffffffff, std::uint32_t flags = 0) {
    const float w = static_cast<float>(pc_text_width(art, text));
    pc_text_draw(ui, art, text, std::floor(x - w * 0.5f), y, 0x103, colour, flags);
}

} // namespace

std::string pc_calibrate_value(FrontEnd& fe) {
    const int count = static_cast<int>(plugged_pads().size());
    if (count == 0) {
        return "NO PAD";
    }
    int& pad = fe.screen.pc.calibrate_pad;
    pad = std::clamp(pad, 0, count - 1);
    return "PAD " + std::to_string(pad + 1);
}

bool pc_calibrate_step(FrontEnd& fe, int step) {
    const int count = static_cast<int>(plugged_pads().size());
    int& pad = fe.screen.pc.calibrate_pad;
    const int next = std::clamp(pad + step, 0, std::max(count - 1, 0));
    if (next == pad) {
        return false;
    }
    pad = next;
    return true;
}

void pc_calibrate_open(FrontEnd& fe, Ui& ui) {
    const std::vector<SDL_JoystickID> pads = plugged_pads();
    PcSettingsScreen& s = fe.screen.pc;
    if (pads.empty()) {
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        return;
    }
    CalibrationPanel& c = s.calibration;
    c = CalibrationPanel{};
    c.pad = pads[static_cast<std::size_t>(
        std::clamp(s.calibrate_pad, 0, static_cast<int>(pads.size()) - 1))];
    texture_set_load(ui, game_sheet);
    c.name = label_name(fe.pc_art, SDL_GetGamepadNameForID(c.pad));
    // the pad's buttons as they are now, so the press that opened it isn't read again
    if (SDL_Gamepad* pad = SDL_GetGamepadFromID(c.pad)) {
        c.held[0] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
        c.held[1] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST);
        c.held[2] = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH);
    }
    c.open = true;
    start_rest(c);
    ui_sound(ui, bank_sys, se_menu_accept);
    menu_input_mark(ui);
}

void pc_calibrate_update(FrontEnd& fe, Ui& ui) {
    PcSettingsScreen& s = fe.screen.pc;
    CalibrationPanel& c = s.calibration;
    int raw[6];
    if (!read_axes(c.pad, raw)) {
        c.open = false;
        s.note.clear();
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        return;
    }
    sample(c, raw);

    // A, B and Y: the menus' (player 1's devices), or the measured pad's own
    bool pressed[3] = {};
    if (SDL_Gamepad* pad = SDL_GetGamepadFromID(c.pad)) {
        const SDL_GamepadButton buttons[3] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
                                              SDL_GAMEPAD_BUTTON_NORTH};
        for (int b = 0; b < 3; ++b) {
            const bool down = SDL_GetGamepadButton(pad, buttons[b]);
            pressed[b] = down && !c.held[b];
            c.held[b] = down;
        }
    }
    const bool accept = menu_accept(ui) || pressed[0];
    const bool back = menu_cancel(ui) || pressed[1];
    const bool clear = (ui.pad.b.rising & pad_y) != 0 || pressed[2];
    if (back) {
        c.open = false;
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
        return;
    }
    if (clear) {
        reset(fe, ui);
        return;
    }
    if (!accept) {
        return;
    }
    menu_input_mark(ui);
    switch (c.phase) {
    case 0:
        start_turning(c);
        c.note.clear();
        ui_sound(ui, bank_sys, se_menu_accept);
        break;
    case 1:
        if (!turned_enough(c)) {
            c.note = "Turn both sticks all the way round their edges first, then press A.";
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            break;
        }
        if (fe.pc->calibrations && fe.pc->gubsy) {
            const PadCalibration kept = measured(c);
            pad_calibration_store(*fe.pc->calibrations, *fe.pc->gubsy, c.pad, &kept);
        }
        c.phase = 2;
        c.note.clear();
        ui_sound(ui, bank_sys, se_menu_accept);
        break;
    default:
        start_rest(c);
        c.note.clear();
        ui_sound(ui, bank_sys, se_menu_accept);
        break;
    }
}

std::string pc_calibrate_help(FrontEnd& fe) {
    const CalibrationPanel& c = fe.screen.pc.calibration;
    if (!c.note.empty()) {
        return c.note;
    }
    switch (c.phase) {
    case 0:
        return "Let go of both sticks and triggers, then press A. B goes back, Y or Delete puts "
               "this pad back as it came.";
    case 1:
        return "Turn both sticks slowly round their edges and pull both triggers all the way, "
               "then press A.";
    default:
        return "Kept for this pad. The dots show what the game reads now. A measures "
               "again, B goes back.";
    }
}

void pc_calibrate_draw(FrontEnd& fe, Ui& ui) {
    const CalibrationPanel& c = fe.screen.pc.calibration;
    const PcArt& art = fe.pc_art;
    const PcSettings& settings = fe.pc->settings;
    int raw[6] = {};
    read_axes(c.pad, raw);
    const PadCalibration* kept =
        fe.pc->calibrations ? pad_calibration_of(*fe.pc->calibrations, c.pad) : nullptr;
    const PadCalibration plain;
    const PadCalibration& through = kept ? *kept : plain;

    // the pad's name, then what to do now
    text_centred(ui, art, c.name, panel_x + panel_w * 0.5f, panel_y + 4);
    const char* step = c.phase == 0 ? "LET GO" : c.phase == 1 ? "TURN THE STICKS" : "CALIBRATED";
    text_centred(ui, art, step, panel_x + panel_w * 0.5f, panel_y + 232);

    const int zones[2] = {settings.left_dead_zone, settings.right_dead_zone};
    for (int s = 0; s < 2; ++s) {
        const float x = ring_x(s), y = ring_y;
        text_centred(ui, art, s == 0 ? "LEFT" : "RIGHT", x, panel_y + 24);
        sprite_at(ui, ring_sprite, x, y, ring_scale, 0xc0ffffff, 0x101);
        // the dead zone, as the game reads it after the calibration (a square per axis)
        zone_corners(ui, x, y, travel * static_cast<float>(dead_zone_raw(zones[s])) / 32767.0f);
        // where the stick has reached while turning, by direction
        if (c.phase == 1) {
            for (int k = 0; k < 32; ++k) {
                if (c.reach[s][k] <= 0) {
                    continue;
                }
                const float turn = static_cast<float>(k) * 6.2831853f / 32.0f;
                const float r = c.reach[s][k] * travel;
                sprite_at(ui, dot_sprite, x + std::cos(turn) * r, y + std::sin(turn) * r, 0.4f,
                          0xff60c0ff, 0x102);
            }
        }
        // the stick now: raw from its rest while measuring; once kept, raw dim and the
        // calibrated reading bright
        float rx = static_cast<float>(raw[s * 2]) / 32768.0f;
        float ry = static_cast<float>(raw[s * 2 + 1]) / 32768.0f;
        if (c.phase == 1) {
            rx = static_cast<float>(raw[s * 2] - c.centre[s * 2]) / 32768.0f;
            ry = static_cast<float>(raw[s * 2 + 1] - c.centre[s * 2 + 1]) / 32768.0f;
        }
        if (c.phase == 2) {
            sprite_at(ui, dot_sprite, x + rx * travel, y + ry * travel, 0.7f, 0xe0909090, 0x103);
            rx = static_cast<float>(calibrated_axis(through.stick[s][0], raw[s * 2])) / 32767.0f;
            ry =
                static_cast<float>(calibrated_axis(through.stick[s][1], raw[s * 2 + 1])) / 32767.0f;
        }
        // the lock marks in their own green measuring (drawn solid, not added, to read on the
        // grey), yellow through the calibration
        stick_marker(ui, x + rx * travel, y + ry * travel, c.phase == 2 ? 0xffffd040 : 0xff00e0b0,
                     0xffffffff);

        // the trigger under it: its pull on the level bar
        const float bx = x - 70, bw = 140;
        pc_text_draw(ui, art, s == 0 ? "LT" : "RT", bx - 22, trigger_y + 2, 0x103, 0xffffffff);
        pc_bar_draw(ui, volume_bar, bx, trigger_y + 4, bw, 0x103, 0xffffffff);
        int pull = std::max(0, raw[4 + s]);
        if (c.phase == 1) {
            pull = std::max(0, pull - c.trigger_rest[s]);
        } else if (c.phase == 2) {
            pull = calibrated_trigger(through, s, pull);
        }
        Sprite fill = sprite_of(ui, volume_fill);
        fill.pos[0] = bx + 4;
        fill.pos[1] = trigger_y + 8;
        fill.scale[0] = (bw - 8) / 8.0f * static_cast<float>(std::min(pull, 32767)) / 32767.0f;
        fill.blend = 0x104;
        sprite_submit(ui, fill);
    }
}

bool pc_calibrate_event(FrontEnd& fe, Ui& ui, const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.scancode == SDL_SCANCODE_DELETE) {
        reset(fe, ui);
        return true;
    }
    return false;
}
