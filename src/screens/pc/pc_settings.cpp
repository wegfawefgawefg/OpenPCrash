#include "screens/pc/pc_settings.hpp"

#include "audio/audio.hpp"
#include "input/bindings.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <gubsy/input/profile_settings.hpp>
#include <gubsy/lobby/state.hpp>
#include <gubsy/runtime.hpp>
#include <gubsy/settings/catalog.hpp>
#include <gubsy/settings/game_settings.hpp>
#include <gubsy/settings/top_level_settings.hpp>
// gubsy's engine, for its settings (as devices.cpp reads them)
#include "src/engine_state.hpp"

namespace {

// a number kept as an int, a float or text (gubsy keeps a whole float as an int)
int read_int(const TopLevelGameSettings& settings, const char* key, int fallback) {
    const auto at = settings.settings.find(key);
    if (at == settings.settings.end()) {
        return fallback;
    }
    if (const int* value = std::get_if<int>(&at->second)) {
        return *value;
    }
    if (const float* value = std::get_if<float>(&at->second)) {
        return static_cast<int>(std::lround(*value));
    }
    if (const std::string* value = std::get_if<std::string>(&at->second)) {
        return std::atoi(value->c_str());
    }
    return fallback;
}

std::string read_text(const TopLevelGameSettings& settings, const char* key,
                      const std::string& fallback) {
    return get_top_level_setting_string(settings, key, fallback);
}

// "1280x960" or "1920x1080@59.94"
bool parse_size(const std::string& text, int& w, int& h, float& hz) {
    hz = 0.0f;
    return std::sscanf(text.c_str(), "%dx%d@%f", &w, &h, &hz) >= 2 && w > 0 && h > 0;
}

std::string size_text(int w, int h, float hz = 0.0f) {
    char text[48];
    if (hz > 0.0f) {
        std::snprintf(text, sizeof text, "%dx%d@%.2f", w, h, static_cast<double>(hz));
        // 60.00 kept as 60
        std::string out = text;
        while (out.back() == '0') {
            out.pop_back();
        }
        if (out.back() == '.') {
            out.pop_back();
        }
        return out;
    }
    std::snprintf(text, sizeof text, "%dx%d", w, h);
    return text;
}

// the display kept: "NAME@X,Y", where it sat telling two of one name apart
void parse_display(const std::string& text, PcSettings& s) {
    s.display_name.clear();
    s.display_x = s.display_y = 0;
    const std::size_t at = text.rfind('@');
    if (at == std::string::npos || at == 0 ||
        std::sscanf(text.c_str() + at + 1, "%d,%d", &s.display_x, &s.display_y) != 2) {
        return;
    }
    s.display_name = text.substr(0, at);
}

const char* mode_text(WindowMode mode) {
    switch (mode) {
    case WindowMode::borderless:
        return "borderless";
    case WindowMode::fullscreen:
        return "fullscreen";
    default:
        return "windowed";
    }
}

// player 1's settings profile, made when missing (the catalog does it)
GameSettings* player_settings(GubsyRuntime& runtime, bool make) {
    EngineState& engine = gubsy_runtime_engine(runtime);
    if (const UserProfile* user = gubsy_lobby_user_profile(engine, 0)) {
        for (GameSettings& settings : engine.game_settings_pool) {
            if (settings.id == user->last_game_settings_profile_id) {
                return &settings;
            }
        }
    }
    if (!make) {
        return nullptr;
    }
    return build_settings_catalog(engine, 0).profile_settings;
}

} // namespace

void pc_settings_load(PcHost& host) {
    PcSettings& s = host.settings;
    if (!host.gubsy) {
        return;
    }
    EngineState& engine = gubsy_runtime_engine(*host.gubsy);
    const TopLevelGameSettings& top = engine.top_level_game_settings;

    const std::string mode = read_text(top, key_window_mode, "windowed");
    s.window_mode = mode == "borderless"   ? WindowMode::borderless
                    : mode == "fullscreen" ? WindowMode::fullscreen
                                           : WindowMode::windowed;
    float hz = 0.0f;
    if (!parse_size(read_text(top, key_window_size, ""), s.window_w, s.window_h, hz)) {
        s.window_w = s.window_h = 0;
    }
    if (!parse_size(read_text(top, key_fullscreen_mode, ""), s.full_w, s.full_h, s.full_hz)) {
        s.full_w = s.full_h = 0;
        s.full_hz = 0.0f;
    }
    parse_display(read_text(top, key_display, ""), s);
    s.frame_cap = std::clamp(read_int(top, key_frame_cap, 0), 0, 1000);
    s.show_fps = read_int(top, key_show_fps, 0) != 0;
    s.safe_area = read_int(top, key_safe_area, 0) != 0;
    s.render.vsync = read_int(top, key_vsync, 1) != 0;
    s.render.render_scale = std::clamp(read_int(top, key_render_scale, 100), 50, 200);
    s.render.fit_43 = read_int(top, key_fit_43, 0) != 0;
    s.render.anisotropy = std::clamp(read_int(top, key_anisotropy, 0), 0, 16);
    s.render.soft_focus = read_int(top, key_soft_focus, 1) != 0;
    s.render.brightness = std::clamp(read_int(top, key_brightness, 100), 50, 150);
    s.render.flashes = std::clamp(read_int(top, key_flashes, 100), 0, 100);
    s.render.camera_shake = std::clamp(read_int(top, key_camera_shake, 100), 0, 100);
    s.render.hud_width = std::clamp(read_int(top, key_hud_width, 100), 0, 100);
    s.render.hud_size = std::clamp(read_int(top, key_hud_size, 100), 70, 130);
    s.render.antialias = read_text(top, key_antialias, "off") == "fxaa";
    s.master = std::clamp(read_int(top, key_master, 100), 0, 100);
    s.music = std::clamp(read_int(top, key_music, 100), 0, 100);
    s.effects = std::clamp(read_int(top, key_effects, 100), 0, 100);
    s.mute_unfocused = read_int(top, key_mute_unfocused, 1) != 0;
    s.focus_pause = read_int(top, key_focus_pause, 1) != 0;

    // player 1's
    if (const GameSettings* player = player_settings(*host.gubsy, false)) {
        s.left_dead_zone =
            std::clamp(get_game_setting_int(*player, left_dead_zone_key, dead_zone_default),
                       dead_zone_min, dead_zone_max);
        s.right_dead_zone =
            std::clamp(get_game_setting_int(*player, right_dead_zone_key, dead_zone_default),
                       dead_zone_min, dead_zone_max);
        s.outer_dead_zone = std::clamp(get_game_setting_int(*player, outer_dead_zone_key, 0), 0,
                                       outer_dead_zone_max);
        s.story_skip = get_game_setting_string(*player, story_talk_key, "show") == "skip";
        s.chatter_skip = get_game_setting_string(*player, battle_chatter_key, "show") == "skip";
    }
    if (const InputSettingsProfile* input = gubsy_lobby_input_settings_profile(engine, 0)) {
        s.mouse_sensitivity =
            std::clamp(static_cast<int>(std::lround(input->mouse_sensitivity * 100.0f)), 10, 500);
        s.invert_y = input->mouse_invert_y;
        s.vibration_strength = std::clamp(
            static_cast<int>(std::lround(input->vibration_strength * 100.0f)), 0, 100);
    }
}

void pc_settings_save(PcHost& host) {
    const PcSettings& s = host.settings;
    if (!host.gubsy) {
        return;
    }
    EngineState& engine = gubsy_runtime_engine(*host.gubsy);
    TopLevelGameSettings& top = engine.top_level_game_settings;
    set_top_level_setting_string(top, key_window_mode, mode_text(s.window_mode));
    if (s.window_w > 0) {
        set_top_level_setting_string(top, key_window_size, size_text(s.window_w, s.window_h));
    }
    set_top_level_setting_string(top, key_fullscreen_mode,
                                 s.full_w > 0 ? size_text(s.full_w, s.full_h, s.full_hz)
                                              : std::string("desktop"));
    if (!s.display_name.empty()) {
        char at[32];
        std::snprintf(at, sizeof at, "@%d,%d", s.display_x, s.display_y);
        set_top_level_setting_string(top, key_display, s.display_name + at);
    }
    set_top_level_setting_int(top, key_frame_cap, s.frame_cap);
    set_top_level_setting_int(top, key_show_fps, s.show_fps ? 1 : 0);
    set_top_level_setting_int(top, key_safe_area, s.safe_area ? 1 : 0);
    set_top_level_setting_int(top, key_vsync, s.render.vsync ? 1 : 0);
    set_top_level_setting_int(top, key_render_scale, s.render.render_scale);
    set_top_level_setting_int(top, key_fit_43, s.render.fit_43 ? 1 : 0);
    set_top_level_setting_int(top, key_anisotropy, s.render.anisotropy);
    set_top_level_setting_int(top, key_soft_focus, s.render.soft_focus ? 1 : 0);
    set_top_level_setting_int(top, key_brightness, s.render.brightness);
    set_top_level_setting_int(top, key_flashes, s.render.flashes);
    set_top_level_setting_int(top, key_camera_shake, s.render.camera_shake);
    set_top_level_setting_int(top, key_hud_width, s.render.hud_width);
    set_top_level_setting_int(top, key_hud_size, s.render.hud_size);
    set_top_level_setting_string(top, key_antialias, s.render.antialias ? "fxaa" : "off");
    set_top_level_setting_int(top, key_master, s.master);
    set_top_level_setting_int(top, key_music, s.music);
    set_top_level_setting_int(top, key_effects, s.effects);
    set_top_level_setting_int(top, key_mute_unfocused, s.mute_unfocused ? 1 : 0);
    set_top_level_setting_int(top, key_focus_pause, s.focus_pause ? 1 : 0);
    if (!save_top_level_game_settings(top)) {
        std::fprintf(stderr, "settings: couldn't write the install's settings\n");
    }

    // player 1's
    if (GameSettings* player = player_settings(*host.gubsy, true)) {
        set_game_setting_int(*player, left_dead_zone_key, s.left_dead_zone);
        set_game_setting_int(*player, right_dead_zone_key, s.right_dead_zone);
        set_game_setting_int(*player, outer_dead_zone_key, s.outer_dead_zone);
        set_game_setting_string(*player, story_talk_key, s.story_skip ? "skip" : "show");
        set_game_setting_string(*player, battle_chatter_key, s.chatter_skip ? "skip" : "show");
        (void)save_game_settings(*player);
    }
    if (InputSettingsProfile* input = gubsy_lobby_input_settings_profile(engine, 0)) {
        input->mouse_sensitivity = static_cast<float>(s.mouse_sensitivity) / 100.0f;
        input->mouse_invert_y = s.invert_y;
        input->vibration_strength = static_cast<float>(s.vibration_strength) / 100.0f;
        (void)save_input_settings_profile(*input);
    }
}

void pc_settings_apply(PcHost& host, bool window) {
    const PcSettings& s = host.settings;
    if (host.renderer && host.renderer->vk) {
        renderer_set_options(*host.renderer, s.render);
    }
    pc_settings_focus(host, host.focused);
    if (!window || !host.window) {
        return;
    }
    pc_display_apply(host);
}

void pc_settings_focus(PcHost& host, bool focused) {
    host.focused = focused;
    if (!host.audio) {
        return;
    }
    const PcSettings& s = host.settings;
    const bool muted = !focused && s.mute_unfocused;
    set_pc_volumes(*host.audio, muted ? 0.0f : static_cast<float>(s.master) / 100.0f,
                   static_cast<float>(s.music) / 100.0f, static_cast<float>(s.effects) / 100.0f);
}

double pc_frame_seconds(const PcHost& host) {
    return host.settings.frame_cap > 0 ? 1.0 / host.settings.frame_cap : 0.0;
}

bool pc_window_event(PcHost& host, const SDL_Event& event) {
    // Alt+Enter and F11
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        (event.key.scancode == SDL_SCANCODE_F11 ||
         (event.key.scancode == SDL_SCANCODE_RETURN && (event.key.mod & SDL_KMOD_ALT)))) {
        WindowMode& mode = host.settings.window_mode;
        mode = mode == WindowMode::windowed ? WindowMode::borderless : WindowMode::windowed;
        pc_settings_save(host);
        pc_settings_apply(host, true);
        return true;
    }
    // the focus: the mute (and the viewer's hold) while it's away
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        pc_settings_focus(host, false);
    } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        pc_settings_focus(host, true);
    }
    // the display the window moved to is kept (the player dragged it there, or one went); a
    // display unplugged puts a fullscreen window back on what is left in its mode
    PcSettings& s = host.settings;
    if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED && pc_display_remember(host)) {
        pc_settings_save(host);
    }
    if (event.type == SDL_EVENT_DISPLAY_REMOVED && s.window_mode != WindowMode::windowed) {
        pc_display_apply(host);
    }
    // a window dragged to a new size keeps it (RESOLUTION offers it); our own resizes bring the
    // size already kept, and a maximized window's size isn't one to start with
    if (event.type == SDL_EVENT_WINDOW_RESIZED && host.window &&
        s.window_mode == WindowMode::windowed &&
        !(SDL_GetWindowFlags(host.window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN))) {
        const int w = event.window.data1, h = event.window.data2;
        const int kept_w = s.window_w > 0 ? s.window_w : host.base_w;
        const int kept_h = s.window_h > 0 ? s.window_h : host.base_h;
        if (w > 0 && h > 0 && (w != kept_w || h != kept_h)) {
            s.window_w = w;
            s.window_h = h;
            host.size_dragged_at = SDL_GetTicksNS();
        }
    }
    return false;
}

void pc_frame_end(PcHost& host, std::uint64_t frame_start, bool capturing) {
    // the frame rate, twice a second
    const std::uint64_t end = SDL_GetTicksNS();
    ++host.fps_frames;
    if (host.fps_since == 0) {
        host.fps_since = end;
        host.fps_frames = 0;
    } else if (end - host.fps_since >= 500000000ull) {
        host.fps = static_cast<int>(std::lround(static_cast<double>(host.fps_frames) * 1e9 /
                                                static_cast<double>(end - host.fps_since)));
        host.fps_since = end;
        host.fps_frames = 0;
    }
    // a dragged size, written once the drag has stopped
    if (host.size_dragged_at != 0 && end - host.size_dragged_at >= 500000000ull) {
        host.size_dragged_at = 0;
        pc_settings_save(host);
    }
    // the frame limit
    const double seconds = pc_frame_seconds(host);
    if (seconds <= 0.0 || capturing) {
        return;
    }
    const std::uint64_t until = frame_start + static_cast<std::uint64_t>(seconds * 1e9);
    const std::uint64_t now = SDL_GetTicksNS();
    if (now < until) {
        SDL_DelayPrecise(until - now);
    }
}
