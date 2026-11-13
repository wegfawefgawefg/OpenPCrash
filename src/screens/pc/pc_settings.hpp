#pragma once
// The PC's own settings (docs/settings.md): the screen, the picture, the sound, the mouse and pads,
// and how the game treats the player. Kept by gubsy: the install's in its top-level settings
// (gubsy.video.* and gubsy.audio.* where gubsy has a key, phantomcrash.* for ours), player 1's in
// their settings profile, the mouse in their input settings profile. Read at start, written on
// every change, applied to the window, the renderer and the sound at once.
#include "render/renderer.hpp"

#include <SDL3/SDL_events.h>
#include <cstdint>
#include <string>
#include <vector>

class GubsyRuntime;
struct Audio;
struct PadCalibrations;

enum class WindowMode { windowed, borderless, fullscreen };

struct PcSettings {
    // the screen
    WindowMode window_mode = WindowMode::windowed;
    int window_w = 0, window_h = 0;          // the window's size; 0 the command line's
    int full_w = 0, full_h = 0; // exclusive fullscreen's mode in pixels; 0 the desktop's
    float full_hz = 0.0f;       // its refresh rate, 0 the fastest at its size
    // the display the window was last on (its name, and where it sits for two of one name);
    // "" the window's own
    std::string display_name;
    int display_x = 0, display_y = 0;
    int frame_cap = 0;                       // frames a second at most, 0 no cap
    bool show_fps = false;                   // the frame rate in the top right corner
    bool safe_area = false; // the 3D in the original's 576x448 title-safe rectangle
    RenderOptions render;   // v-sync, render scale, 4:3, filtering, soft focus, brightness
    // the sound, percent, over the original's two volumes
    int master = 100, music = 100, effects = 100;
    bool mute_unfocused = true;
    // the game
    bool focus_pause = true;
    // player 1's input (gubsy's profiles)
    int mouse_sensitivity = 100; // percent of gubsy's 1.0
    bool invert_y = false;
    int vibration_strength = 100; // percent of gubsy's vibration_strength 1.0 (pad rumble)
    int left_dead_zone = 50;      // percent, input/bindings.hpp left_dead_zone_key (retail's)
    int right_dead_zone = 50;     // right_dead_zone_key
    int outer_dead_zone = 0;      // outer_dead_zone_key
    bool story_skip = false;     // "Story conversations": skip (input/bindings.hpp story_talk_key)
    bool chatter_skip = false;   // "Battle chatter": skip (battle_chatter_key)
};

// what the settings act on, made in main
struct PcHost {
    GubsyRuntime* gubsy = nullptr;
    SDL_Window* window = nullptr;
    Renderer* renderer = nullptr;
    Audio* audio = nullptr;
    PadCalibrations* calibrations = nullptr; // the pads' (InputDevices), STICK CALIBRATION's
    PcSettings settings;
    int base_w = 1280, base_h = 960; // the command line's window size
    bool focused = true;
    // a window size dragged by hand, written once the drag has been still for half a second
    std::uint64_t size_dragged_at = 0; // SDL ticks, ns; 0 nothing to write
    // the frame rate shown (SHOW FRAME RATE): frames counted since fps_since
    int fps = 0, fps_frames = 0;
    std::uint64_t fps_since = 0;
};

// a resolution offered on the screen page: a window's size, or a mode's in pixels and its rate
struct PcResolution {
    int w = 0, h = 0;
    float hz = 0.0f;
};

// the keys (docs/settings.md), all phantomcrash.*: gubsy's settings catalog writes its own
// gubsy.video.* and gubsy.audio.* defaults (fullscreen, 1280x720, music 0.8) into the file the
// first time anything builds it, so ours stay apart. The story ones are the conversations',
// input/bindings.hpp's story_talk_key and battle_chatter_key (docs/story.md, "Skipping").
inline constexpr const char* key_window_mode = "phantomcrash.video.window_mode";
inline constexpr const char* key_window_size = "phantomcrash.video.window_size";
inline constexpr const char* key_fullscreen_mode = "phantomcrash.video.fullscreen_mode";
inline constexpr const char* key_display = "phantomcrash.video.display";
inline constexpr const char* key_vsync = "phantomcrash.video.vsync";
inline constexpr const char* key_frame_cap = "phantomcrash.video.frame_cap";
inline constexpr const char* key_show_fps = "phantomcrash.video.show_fps";
inline constexpr const char* key_render_scale = "phantomcrash.video.render_scale";
inline constexpr const char* key_safe_area = "phantomcrash.video.safe_area";
inline constexpr const char* key_fit_43 = "phantomcrash.video.fit_43";
inline constexpr const char* key_anisotropy = "phantomcrash.video.anisotropy";
inline constexpr const char* key_soft_focus = "phantomcrash.video.soft_focus";
inline constexpr const char* key_brightness = "phantomcrash.video.brightness";
inline constexpr const char* key_flashes = "phantomcrash.video.flashes";
inline constexpr const char* key_camera_shake = "phantomcrash.video.camera_shake";
inline constexpr const char* key_hud_width = "phantomcrash.video.hud_width";
inline constexpr const char* key_hud_size = "phantomcrash.video.hud_size";
inline constexpr const char* key_antialias = "phantomcrash.video.antialias";
inline constexpr const char* key_master = "phantomcrash.audio.master";
inline constexpr const char* key_music = "phantomcrash.audio.music";
inline constexpr const char* key_effects = "phantomcrash.audio.effects";
inline constexpr const char* key_mute_unfocused = "phantomcrash.audio.mute_unfocused";
inline constexpr const char* key_focus_pause = "phantomcrash.game.focus_pause";

// gubsy's files to settings (once gubsy has started)
void pc_settings_load(PcHost& host);
// settings to gubsy's files
void pc_settings_save(PcHost& host);
// to the renderer and the sound, and the window when asked (a size or mode change)
void pc_settings_apply(PcHost& host, bool window);
// the window gained or lost focus: the mute follows
void pc_settings_focus(PcHost& host, bool focused);
// the window's mode, size and display (pc_display.cpp): windowed at its size on the display
// kept, borderless over its desktop, or fullscreen in the exclusive mode kept or the nearest one
// it has (the desktop when it has none); SDL's asynchronous changes waited for
void pc_display_apply(PcHost& host);
// the display the window is on now, kept in the settings; true when it changed
bool pc_display_remember(PcHost& host);
// the window sizes that fit the display, or the display's exclusive modes (in pixels, smallest
// first, the fastest first within a size)
std::vector<PcResolution> pc_window_sizes(const PcHost& host);
std::vector<PcResolution> pc_fullscreen_modes(const PcHost& host);
// the window's size now, in its own pixels' units (what windowed shows)
PcResolution pc_window_now(const PcHost& host);
// RESOLUTION's value: the window's size, fullscreen's mode, or the desktop's (with its rate)
PcResolution pc_resolution_now(const PcHost& host);
// RESOLUTION stepped: a window size, or a fullscreen size at its fastest refresh; false when
// nothing changed (borderless is the desktop's)
bool pc_resolution_step(PcHost& host, int step);
// the refresh rates fullscreen offers at a size, slowest first
std::vector<float> pc_refresh_rates(const PcHost& host, int w, int h);
// REFRESH RATE stepped among them (fullscreen only); false when nothing changed
bool pc_refresh_step(PcHost& host, int step);
// how long a frame may take under the cap, seconds (0 no cap)
double pc_frame_seconds(const PcHost& host);
// the window's events the settings follow: F11 and Alt+Enter flip DISPLAY MODE between WINDOW and
// BORDERLESS (a fullscreen mode goes back to WINDOW), kept like a change on PC CONFIG; the focus
// mutes; a window resized by hand keeps its size (not maximized); the display it moves to is kept;
// a display unplugged puts a fullscreen window on what is left. True for the fullscreen keys,
// which go no further
bool pc_window_event(PcHost& host, const SDL_Event& event);
// the frame's end: the frame rate counted, a dragged size written once still, the frame limit
// waits out the rest of the frame (never in a capture)
void pc_frame_end(PcHost& host, std::uint64_t frame_start, bool capturing);
