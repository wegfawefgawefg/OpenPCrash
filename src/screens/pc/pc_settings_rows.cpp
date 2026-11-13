// PC CONFIG's rows (docs/settings.md): every page but BINDINGS, a row per setting, what it shows
// and what left, right and accept do to it.
#include "input/bindings.hpp"
#include "report/reports.hpp"
#include "screens/pc/pc_settings_screen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace {

enum RowId {
    row_mode,
    row_resolution,
    row_refresh,
    row_vsync,
    row_frame_cap,
    row_show_fps,
    row_hud_width,
    row_hud_size,
    row_scale,
    row_antialias,
    row_aspect,
    row_safe,
    row_brightness,
    row_flashes,
    row_shake,
    row_fov,
    row_filter,
    row_soft,
    row_master,
    row_music,
    row_effects,
    row_mute,
    row_mouse_speed,
    row_mouse_y,
    row_air_turn,
    row_double_tap,
    row_left_zone,
    row_right_zone,
    row_outer_zone,
    row_calibrate,
    row_vibration,
    row_story,
    row_chatter,
    row_pause,
    row_reports,
    row_report_bug,
    row_music_panel,
    row_cheats,
    row_default,
};

struct RowText {
    const char* label;
    const char* help;
};

// labels in the key config's letters (no Q or Z there: they're made, so kept rare)
constexpr RowText row_text[] = {
    {"DISPLAY MODE", "Window, borderless over the desktop, or fullscreen at a mode of its own."},
    {"RESOLUTION",
     "The window's size, or the screen's in fullscreen. Borderless is the desktop's."},
    {"REFRESH RATE", "How many times a second the screen redraws. FULLSCREEN picks it."},
    {"VSYNC", "Wait for the screen: no tearing, a little more lag."},
    {"FRAME LIMIT",
     "At most this many frames a second. MAX: up to 500 in a fight, the screen's rate in menus."},
    {"SHOW FRAME RATE", "The frames drawn each second, in the top right corner."},
    {"HUD WIDTH", "How far the HUD and the menus' side pieces reach into a wide window. 0 keeps "
                  "the 4:3 middle."},
    {"HUD SIZE", "How big the fight's gauges and readouts are, each grown from its own corner."},
    {"RENDER SCALE", "Pixels drawn for every hundred the window has. Over 100 smooths edges."},
    {"ANTI-ALIASING", "FXAA smooths the 3D's jagged edges. The menus and the HUD stay sharp."},
    {"ASPECT", "WIDE fills the window with the 3D. 4:3 keeps the original's shape with bars."},
    {"SAFE FRAME", "The 3D in the original's TV-safe 576x448 with a black border, as on the Xbox."},
    {"BRIGHTNESS",
     "Lifts or lowers the 3D. The menus and the HUD stay as they are. 100 is the original."},
    {"FLASHES",
     "How bright blasts and shots light the scene, the sun's flare and a hit's flicker."},
    {"CAMERA SHAKE", "How hard the cockpit view shakes when hit. 100 is the original, 0 none."},
    {"FIELD OF VIEW",
     "How wide the fight camera sees, in degrees across a 16:9 screen. 82 is the original."},
    {"TEXTURE FILTER", "How sharp the ground and walls stay in the distance."},
    {"SOFT FOCUS", "The original's soft, hazy battle picture. OFF draws it sharp."},
    {"MASTER VOLUME", "Everything together. BGM and SE are the original's, in SOUND CONFIG."},
    {"MUSIC VOLUME", "The music and the movies, over the original's BGM volume."},
    {"EFFECTS VOLUME", "Everything else, over the original's SE volume."},
    {"MUTE IN BACKGROUND", "Silent while another window has the keyboard."},
    {"MOUSE SPEED", "How far the upper body turns for a move of the mouse."},
    {"MOUSE UP DOWN", "REVERSE looks down when the mouse goes up."},
    {"AIR TURN",
     "MOUSE: in the air the mouse alone turns you, not A and D. KEYS: they turn you too."},
    {"DOUBLE TAP STEP", "Under STANDARD, a double flick of the stick (A or D twice) steps. OFF: "
                        "only the step buttons (Q and E) do."},
    {"LEFT DEAD ZONE",
     "How far the left stick moves before it counts. 50 is the original's; less suits newer pads."},
    {"RIGHT DEAD ZONE", "How far the right stick moves before it counts. 50 is the original's; "
                        "less suits newer pads."},
    {"OUTER DEAD ZONE", "How much of a stick's far end already counts as full, for a worn stick. "
                        "The original has none."},
    {"STICK CALIBRATION",
     "Teach the game where a pad's sticks rest and how far they reach. Pick the pad, press A."},
    {"VIBRATION STRENGTH",
     "How hard the pad shakes when hit. OPTION's VIBRATION turns it on and off."},
    {"STORY TALK", "SKIP: the story's conversations never show. The story goes on the same."},
    {"BATTLE CHATTER", "SKIP: the pilots' lines during fights never show."},
    {"PAUSE IN BACKGROUND", "A fight stops while another window has the keyboard."},
    {"CRASH REPORTS", "SEND: a crash, a hang or a failed start sends the log's end, the build, the "
                      "system, the GPU, the settings and the fight's inputs. No names."},
    {"REPORT A BUG", "Tell the developer what went wrong, with the log's end and the build."},
    {"MUSIC", "The fights' music: the song playing, the mode, and the playlists."},
    {"CHEATS", "Money, songs, paints, garages, days and cheaper prices. A game that uses them "
               "says so in its save."},
    {"DEFAULT", "Put this page back as it was."},
};

constexpr RowId screen_rows[] = {row_mode,      row_resolution, row_refresh,
                                 row_vsync,     row_frame_cap,  row_show_fps,
                                 row_hud_width, row_hud_size,   row_default};
constexpr RowId picture_rows[] = {row_scale,      row_antialias, row_aspect, row_safe,
                                  row_brightness, row_flashes,   row_shake,  row_fov,
                                  row_filter,     row_soft,      row_default};
constexpr RowId sound_rows[] = {row_master, row_music,       row_effects,
                                row_mute,   row_music_panel, row_default};
constexpr RowId input_rows[] = {row_mouse_speed, row_mouse_y,    row_air_turn,   row_double_tap,
                                row_left_zone,   row_right_zone, row_outer_zone, row_calibrate,
                                row_vibration,   row_default};
constexpr RowId game_rows[] = {row_story,      row_chatter, row_pause,  row_reports,
                               row_report_bug, row_cheats,  row_default};
// without a report address built in, no report rows (src/report/reports.hpp)
constexpr RowId game_rows_plain[] = {row_story, row_chatter, row_pause, row_cheats, row_default};

constexpr int frame_caps[] = {0, 30, 60, 75, 90, 120, 144, 165, 240};
constexpr int render_scales[] = {50, 75, 100, 125, 150, 175, 200};
constexpr int anisotropies[] = {0, 2, 4, 8, 16};

bool row_of(int page, int row, RowId& out) {
    const RowId* rows = nullptr;
    int count = 0;
    switch (page) {
    case page_screen:
        rows = screen_rows, count = static_cast<int>(std::size(screen_rows));
        break;
    case page_picture:
        rows = picture_rows, count = static_cast<int>(std::size(picture_rows));
        break;
    case page_sound:
        rows = sound_rows, count = static_cast<int>(std::size(sound_rows));
        break;
    case page_input:
        rows = input_rows, count = static_cast<int>(std::size(input_rows));
        break;
    case page_game:
        if (reports_available()) {
            rows = game_rows, count = static_cast<int>(std::size(game_rows));
        } else {
            rows = game_rows_plain, count = static_cast<int>(std::size(game_rows_plain));
        }
        break;
    default:
        return false;
    }
    if (row < 0 || row >= count) {
        return false;
    }
    out = rows[row];
    return true;
}

std::string number(int value) {
    return std::to_string(value);
}

// a refresh rate: whole ("60"), or to two places where it isn't ("59.94")
std::string rate_text(float hz) {
    char text[16];
    const bool whole = std::fabs(hz - std::round(hz)) < 0.01f;
    std::snprintf(text, sizeof text, whole ? "%.0f" : "%.2f", static_cast<double>(hz));
    return text;
}

std::string size_text(int w, int h) {
    return std::to_string(w) + "x" + std::to_string(h); // 'x' is the number font's times sign
}

const char* on_off(bool on) {
    return on ? "ON" : "OFF";
}

// the next value in a list, from the one nearest the current
template <std::size_t N> int step_in(const int (&values)[N], int now, int step) {
    int at = 0;
    for (std::size_t i = 0; i < N; ++i) {
        if (std::abs(values[i] - now) < std::abs(values[static_cast<std::size_t>(at)] - now)) {
            at = static_cast<int>(i);
        }
    }
    return values[static_cast<std::size_t>(std::clamp(at + step, 0, static_cast<int>(N) - 1))];
}

int step_range(int now, int step, int by, int lo, int hi) {
    return std::clamp(now + step * by, lo, hi);
}

void page_defaults(PcSettings& s, int page) {
    const PcSettings d;
    switch (page) {
    case page_screen:
        s.window_mode = d.window_mode;
        s.window_w = s.window_h = 0;
        s.full_w = s.full_h = 0;
        s.full_hz = 0.0f;
        s.render.vsync = d.render.vsync;
        s.frame_cap = d.frame_cap;
        s.show_fps = d.show_fps;
        s.render.hud_width = d.render.hud_width;
        s.render.hud_size = d.render.hud_size;
        break;
    case page_picture: {
        const bool vsync = s.render.vsync;
        const int hud_width = s.render.hud_width, hud_size = s.render.hud_size; // SCREEN's
        s.render = d.render;
        s.render.vsync = vsync;
        s.render.hud_width = hud_width;
        s.render.hud_size = hud_size;
        s.safe_area = d.safe_area;
        break;
    }
    case page_sound:
        s.master = d.master, s.music = d.music, s.effects = d.effects;
        s.mute_unfocused = d.mute_unfocused;
        break;
    case page_input:
        s.mouse_sensitivity = d.mouse_sensitivity, s.invert_y = d.invert_y;
        s.air_turn_mouse = d.air_turn_mouse, s.double_tap_step = d.double_tap_step;
        s.left_dead_zone = d.left_dead_zone, s.right_dead_zone = d.right_dead_zone;
        s.outer_dead_zone = d.outer_dead_zone;
        s.vibration_strength = d.vibration_strength;
        break;
    case page_game:
        s.story_skip = d.story_skip, s.chatter_skip = d.chatter_skip;
        s.focus_pause = d.focus_pause;
        break;
    default:
        break;
    }
}

} // namespace

int pc_rows_count(int page) {
    int count = 0;
    RowId id;
    while (row_of(page, count, id)) {
        ++count;
    }
    return count;
}

PcRowView pc_row_view(const PcHost& host, int page, int row) {
    PcRowView view;
    RowId id;
    if (!row_of(page, row, id)) {
        return view;
    }
    const PcSettings& s = host.settings;
    view.label = row_text[id].label;
    view.help = row_text[id].help;
    switch (id) {
    case row_mode:
        view.value = s.window_mode == WindowMode::windowed     ? "WINDOW"
                     : s.window_mode == WindowMode::borderless ? "BORDERLESS"
                                                               : "FULLSCREEN";
        break;
    case row_resolution: {
        const PcResolution now = pc_resolution_now(host);
        view.value = size_text(now.w, now.h);
        view.dim = s.window_mode == WindowMode::borderless;
        view.arrows = !view.dim;
        break;
    }
    case row_refresh: {
        // the fullscreen mode's, or the desktop's shown dim
        const PcResolution now = pc_resolution_now(host);
        view.dim = s.window_mode != WindowMode::fullscreen ||
                   pc_refresh_rates(host, now.w, now.h).size() < 2;
        view.arrows = !view.dim;
        view.value = now.hz > 0.0f ? rate_text(now.hz) + " HZ" : "DESKTOP";
        break;
    }
    case row_vsync:
        view.value = on_off(s.render.vsync);
        break;
    case row_frame_cap:
        view.value = s.frame_cap > 0 ? number(s.frame_cap) : "MAX";
        break;
    case row_show_fps:
        view.value = on_off(s.show_fps);
        break;
    case row_hud_width:
        view.value = number(s.render.hud_width);
        view.bar = s.render.hud_width;
        break;
    case row_hud_size:
        view.value = number(s.render.hud_size);
        break;
    case row_scale:
        view.value = number(s.render.render_scale);
        break;
    case row_antialias:
        view.value = s.render.antialias ? "FXAA" : "OFF";
        break;
    case row_aspect:
        view.value = s.render.fit_43 ? "4:3" : "WIDE";
        break;
    case row_safe:
        view.value = on_off(s.safe_area);
        break;
    case row_brightness:
        view.value = number(s.render.brightness);
        view.bar = s.render.brightness - 50;
        break;
    case row_flashes:
        view.value = number(s.render.flashes);
        view.bar = s.render.flashes;
        break;
    case row_shake:
        view.value = number(s.render.camera_shake);
        view.bar = s.render.camera_shake;
        break;
    case row_fov:
        view.value = number(s.render.fov);
        view.bar = (s.render.fov - 82) * 100 / 48;
        break;
    case row_filter:
        view.value =
            s.render.anisotropy > 1 ? "ANISOTROPIC " + number(s.render.anisotropy) : "TRILINEAR";
        break;
    case row_soft:
        view.value = on_off(s.render.soft_focus);
        break;
    case row_master:
        view.value = number(s.master);
        view.bar = s.master;
        break;
    case row_music:
        view.value = number(s.music);
        view.bar = s.music;
        break;
    case row_effects:
        view.value = number(s.effects);
        view.bar = s.effects;
        break;
    case row_mute:
        view.value = on_off(s.mute_unfocused);
        break;
    case row_mouse_speed:
        view.value = number(s.mouse_sensitivity);
        break;
    case row_mouse_y:
        view.value = s.invert_y ? "REVERSE" : "NORMAL";
        break;
    case row_air_turn:
        view.value = s.air_turn_mouse ? "MOUSE" : "KEYS";
        break;
    case row_double_tap:
        view.value = on_off(s.double_tap_step);
        break;
    case row_left_zone:
        view.value = number(s.left_dead_zone);
        break;
    case row_right_zone:
        view.value = number(s.right_dead_zone);
        break;
    case row_outer_zone:
        view.value = number(s.outer_dead_zone);
        break;
    case row_calibrate:
        view.value = "PAD"; // the screen names the pad (pc_settings_calibrate.cpp)
        break;
    case row_vibration:
        view.value = number(s.vibration_strength);
        view.bar = s.vibration_strength;
        break;
    case row_story:
        view.value = s.story_skip ? "SKIP" : "SHOW";
        break;
    case row_chatter:
        view.value = s.chatter_skip ? "SKIP" : "SHOW";
        break;
    case row_pause:
        view.value = on_off(s.focus_pause);
        break;
    case row_reports:
        view.value = report_consent() == ReportConsent::yes ? "SEND" : "OFF";
        break;
    case row_report_bug:
    case row_default:
        view.arrows = false;
        break;
    case row_music_panel:
    case row_cheats:
        view.value = "OPEN";
        view.arrows = false;
        break;
    }
    return view;
}

bool pc_row_step(PcHost& host, int page, int row, int step) {
    RowId id;
    if (step == 0 || !row_of(page, row, id)) {
        return false;
    }
    PcSettings& s = host.settings;
    const std::string before = pc_row_view(host, page, row).value;
    bool window = false;
    switch (id) {
    case row_mode: {
        const int mode = (static_cast<int>(s.window_mode) + step + 3) % 3;
        s.window_mode = static_cast<WindowMode>(mode);
        window = true;
        break;
    }
    case row_resolution:
        window = pc_resolution_step(host, step);
        if (!window) {
            return false;
        }
        break;
    case row_refresh:
        window = pc_refresh_step(host, step);
        if (!window) {
            return false;
        }
        break;
    case row_vsync:
        s.render.vsync = !s.render.vsync;
        break;
    case row_frame_cap:
        s.frame_cap = step_in(frame_caps, s.frame_cap, step);
        break;
    case row_show_fps:
        s.show_fps = !s.show_fps;
        break;
    case row_hud_width:
        s.render.hud_width = step_range(s.render.hud_width, step, 10, 0, 100);
        break;
    case row_hud_size:
        s.render.hud_size = step_range(s.render.hud_size, step, 10, 70, 130);
        break;
    case row_scale:
        s.render.render_scale = step_in(render_scales, s.render.render_scale, step);
        break;
    case row_antialias:
        s.render.antialias = !s.render.antialias;
        break;
    case row_aspect:
        s.render.fit_43 = !s.render.fit_43;
        break;
    case row_safe:
        s.safe_area = !s.safe_area;
        break;
    case row_brightness:
        s.render.brightness = step_range(s.render.brightness, step, 5, 50, 150);
        break;
    case row_flashes:
        s.render.flashes = step_range(s.render.flashes, step, 10, 0, 100);
        break;
    case row_shake:
        s.render.camera_shake = step_range(s.render.camera_shake, step, 10, 0, 100);
        break;
    case row_fov:
        s.render.fov = step_range(s.render.fov, step, 2, 82, 130);
        break;
    case row_filter:
        s.render.anisotropy = step_in(anisotropies, s.render.anisotropy, step);
        break;
    case row_soft:
        s.render.soft_focus = !s.render.soft_focus;
        break;
    case row_master:
        s.master = step_range(s.master, step, 5, 0, 100);
        break;
    case row_music:
        s.music = step_range(s.music, step, 5, 0, 100);
        break;
    case row_effects:
        s.effects = step_range(s.effects, step, 5, 0, 100);
        break;
    case row_mute:
        s.mute_unfocused = !s.mute_unfocused;
        break;
    case row_mouse_speed:
        s.mouse_sensitivity = step_range(s.mouse_sensitivity, step, 10, 10, 500);
        break;
    case row_mouse_y:
        s.invert_y = !s.invert_y;
        break;
    case row_air_turn:
        s.air_turn_mouse = !s.air_turn_mouse;
        break;
    case row_double_tap:
        s.double_tap_step = !s.double_tap_step;
        break;
    case row_left_zone:
        s.left_dead_zone = step_range(s.left_dead_zone, step, 5, dead_zone_min, dead_zone_max);
        break;
    case row_right_zone:
        s.right_dead_zone = step_range(s.right_dead_zone, step, 5, dead_zone_min, dead_zone_max);
        break;
    case row_outer_zone:
        s.outer_dead_zone = step_range(s.outer_dead_zone, step, 5, 0, outer_dead_zone_max);
        break;
    case row_calibrate:
        return false; // the screen steps the pad
    case row_vibration:
        s.vibration_strength = step_range(s.vibration_strength, step, 10, 0, 100);
        break;
    case row_story:
        s.story_skip = !s.story_skip;
        break;
    case row_chatter:
        s.chatter_skip = !s.chatter_skip;
        break;
    case row_pause:
        s.focus_pause = !s.focus_pause;
        break;
    case row_reports:
        // kept beside the reports, not in gubsy's settings (src/report/reports.hpp)
        report_consent_set(report_consent() != ReportConsent::yes);
        return true;
    case row_report_bug:
    case row_music_panel:
    case row_cheats:
        return false;
    case row_default:
        return false;
    }
    // at the end of its range: nothing changed
    if (pc_row_view(host, page, row).value == before) {
        return false;
    }
    pc_settings_save(host);
    pc_settings_apply(host, window);
    return true;
}

bool pc_row_is_calibrate(int page, int row) {
    RowId id;
    return row_of(page, row, id) && id == row_calibrate;
}

bool pc_row_next_fight(int page, int row) {
    RowId id;
    // the fight's views are laid out at its start, and its fighters' controls set at their spawn
    return row_of(page, row, id) && (id == row_safe || id == row_double_tap);
}

bool pc_row_is_report(int page, int row) {
    RowId id;
    return row_of(page, row, id) && id == row_report_bug;
}

bool pc_row_is_music(int page, int row) {
    RowId id;
    return row_of(page, row, id) && id == row_music_panel;
}

bool pc_row_is_cheats(int page, int row) {
    RowId id;
    return row_of(page, row, id) && id == row_cheats;
}

int pc_cheats_row() {
    for (int row = 0; row < pc_rows_count(page_game); ++row) {
        if (pc_row_is_cheats(page_game, row)) {
            return row;
        }
    }
    return 0;
}

int pc_music_row() {
    for (int row = 0; row < static_cast<int>(std::size(sound_rows)); ++row) {
        if (sound_rows[row] == row_music_panel) {
            return row;
        }
    }
    return 0;
}

bool pc_row_accept(PcHost& host, int page, int row) {
    RowId id;
    if (!row_of(page, row, id) || id != row_default) {
        return false;
    }
    page_defaults(host.settings, page);
    pc_settings_save(host);
    pc_settings_apply(host, page == page_screen);
    return true;
}
