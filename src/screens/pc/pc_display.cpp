// The window on the screen (docs/settings.md, SCREEN page): WINDOW at a size, BORDERLESS over the
// desktop, FULLSCREEN in an exclusive mode at a refresh rate, on the display the window was last
// on. SDL3's display mode API as gubsy's display_modes.cpp and Mans In My House's settings use it,
// in the order SDL documents (SDL_video.h):
// - an exclusive mode is one SDL_GetFullscreenDisplayModes lists for the display, handed to
//   SDL_SetWindowFullscreenMode; the mode's display is where the window goes. A null mode is
//   fullscreen over the desktop (borderless);
// - SDL_SetWindowSize and SDL_SetWindowPosition do nothing to a fullscreen or maximized window,
//   so the window leaves those first;
// - each change may be asynchronous: SDL_SyncWindow waits for it before the next step.
// A mode kept that the display doesn't have falls back to the nearest it has, then to the desktop;
// a display gone falls back to the window's own, then the primary.
#include "screens/pc/pc_settings.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// the window sizes offered, 4:3 first as the original's screen is
constexpr PcResolution window_sizes[] = {
    {640, 480},   {800, 600},   {960, 720},   {1024, 768},  {1280, 960},
    {1440, 1080}, {1600, 1200}, {1920, 1440}, {1280, 720},  {1366, 768},
    {1600, 900},  {1920, 1080}, {2560, 1440}, {3840, 2160}, {1280, 800},
    {1680, 1050}, {1920, 1200}, {2560, 1080}, {3440, 1440},
};

// a mode's size in pixels (a high-density mode's points times its density) and its exact rate
PcResolution mode_size(const SDL_DisplayMode& mode) {
    const float density = mode.pixel_density > 0.0f ? mode.pixel_density : 1.0f;
    float hz = mode.refresh_rate;
    if (mode.refresh_rate_numerator > 0 && mode.refresh_rate_denominator > 0) {
        hz = static_cast<float>(mode.refresh_rate_numerator) /
             static_cast<float>(mode.refresh_rate_denominator);
    }
    return {static_cast<int>(std::lround(static_cast<float>(mode.w) * density)),
            static_cast<int>(std::lround(static_cast<float>(mode.h) * density)), hz};
}

bool same_rate(float a, float b) {
    return std::fabs(a - b) < 0.01f;
}

// where a display sits, for telling two of one name apart
SDL_Point display_origin(SDL_DisplayID display) {
    SDL_Rect bounds{};
    SDL_GetDisplayBounds(display, &bounds);
    return {bounds.x, bounds.y};
}

// the display the settings name (by its name, and where it sits when two share one), else the
// window's, else the primary
SDL_DisplayID wanted_display(const PcHost& host) {
    const PcSettings& s = host.settings;
    SDL_DisplayID found = 0;
    int count = 0;
    SDL_DisplayID* ids = s.display_name.empty() ? nullptr : SDL_GetDisplays(&count);
    for (int i = 0; ids && i < count; ++i) {
        const char* name = SDL_GetDisplayName(ids[i]);
        if (!name || s.display_name != name) {
            continue;
        }
        const SDL_Point at = display_origin(ids[i]);
        if (!found || (at.x == s.display_x && at.y == s.display_y)) {
            found = ids[i];
        }
    }
    SDL_free(ids);
    if (!found && host.window) {
        found = SDL_GetDisplayForWindow(host.window);
    }
    return found ? found : SDL_GetPrimaryDisplay();
}

// the display's exclusive modes, smallest first, the fastest first within a size (SDL lists the
// largest first); two of one pixel size and rate (a high-density twin) are kept once
std::vector<SDL_DisplayMode> display_modes(SDL_DisplayID display) {
    std::vector<SDL_DisplayMode> out;
    int count = 0;
    SDL_DisplayMode** modes = display ? SDL_GetFullscreenDisplayModes(display, &count) : nullptr;
    for (int i = 0; modes && i < count; ++i) {
        const PcResolution size = mode_size(*modes[i]);
        const bool listed = std::any_of(out.begin(), out.end(), [&](const SDL_DisplayMode& m) {
            const PcResolution other = mode_size(m);
            return other.w == size.w && other.h == size.h && same_rate(other.hz, size.hz);
        });
        if (size.w > 0 && size.h > 0 && !listed) {
            out.push_back(*modes[i]);
        }
    }
    SDL_free(modes);
    std::stable_sort(out.begin(), out.end(),
                     [](const SDL_DisplayMode& a, const SDL_DisplayMode& b) {
                         const PcResolution x = mode_size(a), y = mode_size(b);
                         if (x.w != y.w) {
                             return x.w < y.w;
                         }
                         return x.h != y.h ? x.h < y.h : x.hz > y.hz;
                     });
    return out;
}

// fullscreen's mode on the display: the one kept (its size and rate, or the fastest at its size
// with no rate), else the nearest size the display has at its fastest; the desktop's when none is
// kept. False when the display lists no exclusive modes
bool pick_mode(const PcHost& host, SDL_DisplayID display, SDL_DisplayMode& out) {
    const std::vector<SDL_DisplayMode> modes = display_modes(display);
    if (modes.empty()) {
        return false;
    }
    PcResolution want{host.settings.full_w, host.settings.full_h, host.settings.full_hz};
    if (want.w <= 0) {
        const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
        want = desktop ? mode_size(*desktop) : mode_size(modes.back());
    }
    const SDL_DisplayMode* best = nullptr;
    long best_cost = 0;
    for (const SDL_DisplayMode& mode : modes) {
        const PcResolution size = mode_size(mode);
        // the size first, then the rate (the fastest within a size comes first, so it wins ties)
        long cost = (std::labs(size.w - want.w) + std::labs(size.h - want.h)) * 100000L;
        if (want.hz > 0.0f) {
            cost += std::lround(std::fabs(size.hz - want.hz) * 100.0f);
        }
        if (!best || cost < best_cost) {
            best = &mode;
            best_cost = cost;
        }
    }
    out = *best;
    return true;
}

// the window to a display's middle, out of fullscreen first when it must move (an exclusive
// window only moves through its mode)
void move_to(SDL_Window* window, SDL_DisplayID display) {
    if (SDL_GetDisplayForWindow(window) == display) {
        return;
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) {
        SDL_SetWindowFullscreen(window, false);
        SDL_SyncWindow(window);
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                          SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SyncWindow(window);
}

// fullscreen over the desktop of the display
bool go_borderless(SDL_Window* window, SDL_DisplayID display) {
    SDL_SetWindowFullscreenMode(window, nullptr);
    move_to(window, display);
    return SDL_SetWindowFullscreen(window, true);
}

} // namespace

void pc_display_apply(PcHost& host) {
    SDL_Window* window = host.window;
    if (!window) {
        return;
    }
    const PcSettings& s = host.settings;
    const SDL_DisplayID display = wanted_display(host);
    bool ok = true;
    switch (s.window_mode) {
    case WindowMode::windowed: {
        // out of fullscreen and maximized first: a size or a place asked of either does nothing
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) {
            ok = SDL_SetWindowFullscreen(window, false);
            SDL_SyncWindow(window);
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) {
            SDL_RestoreWindow(window);
            SDL_SyncWindow(window);
        }
        // the size kept, no larger than the display's room for windows
        int w = s.window_w > 0 ? s.window_w : host.base_w;
        int h = s.window_h > 0 ? s.window_h : host.base_h;
        SDL_Rect usable{};
        if (SDL_GetDisplayUsableBounds(display, &usable) && usable.w > 0 && usable.h > 0) {
            w = std::min(w, usable.w);
            h = std::min(h, usable.h);
        }
        SDL_SetWindowSize(window, w, h);
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                              SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        break;
    }
    case WindowMode::borderless:
        ok = go_borderless(window, display);
        break;
    case WindowMode::fullscreen: {
        SDL_DisplayMode mode{};
        if (pick_mode(host, display, mode) && SDL_SetWindowFullscreenMode(window, &mode)) {
            ok = SDL_SetWindowFullscreen(window, true);
        } else {
            // no exclusive modes there (or SDL refused the one picked): over the desktop
            SDL_Log("settings: no exclusive mode on display %u (%s); fullscreen over the desktop",
                    display, SDL_GetError());
            ok = go_borderless(window, display);
        }
        break;
    }
    }
    if (!ok) {
        SDL_Log("settings: the window mode wasn't changed: %s", SDL_GetError());
    }
    SDL_SyncWindow(window);
    // what came of it, for the run log
    int w = 0, h = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window, &w, &h);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    const SDL_DisplayMode* mode = SDL_GetWindowFullscreenMode(window);
    const SDL_DisplayID on = SDL_GetDisplayForWindow(window);
    const char* name = on ? SDL_GetDisplayName(on) : nullptr;
    SDL_Log("settings: window %s on \"%s\" %dx%d (%dx%d pixels)%s, mode %s",
            s.window_mode == WindowMode::windowed     ? "windowed"
            : s.window_mode == WindowMode::borderless ? "borderless"
                                                      : "fullscreen",
            name ? name : "?", w, h, pw, ph,
            (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? " fullscreen" : "",
            mode ? (std::to_string(mode_size(*mode).w) + "x" + std::to_string(mode_size(*mode).h) +
                    "@" + std::to_string(mode_size(*mode).hz))
                       .c_str()
                 : "desktop");
}

bool pc_display_remember(PcHost& host) {
    if (!host.window) {
        return false;
    }
    const SDL_DisplayID display = SDL_GetDisplayForWindow(host.window);
    const char* name = display ? SDL_GetDisplayName(display) : nullptr;
    if (!name) {
        return false;
    }
    const SDL_Point at = display_origin(display);
    PcSettings& s = host.settings;
    if (s.display_name == name && s.display_x == at.x && s.display_y == at.y) {
        return false;
    }
    s.display_name = name;
    s.display_x = at.x;
    s.display_y = at.y;
    return true;
}

std::vector<PcResolution> pc_window_sizes(const PcHost& host) {
    SDL_Rect usable{0, 0, 1 << 14, 1 << 14};
    if (host.window) {
        SDL_GetDisplayUsableBounds(wanted_display(host), &usable);
    }
    std::vector<PcResolution> out;
    for (const PcResolution& size : window_sizes) {
        if (size.w <= usable.w && size.h <= usable.h) {
            out.push_back(size);
        }
    }
    // the command line's own size, and the one kept, always offered
    for (const PcResolution& own : {PcResolution{host.base_w, host.base_h},
                                    PcResolution{host.settings.window_w, host.settings.window_h}}) {
        const bool listed = std::any_of(out.begin(), out.end(), [&](const PcResolution& r) {
            return r.w == own.w && r.h == own.h;
        });
        if (own.w > 0 && !listed) {
            out.push_back(own);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const PcResolution& a, const PcResolution& b) {
        return a.w != b.w ? a.w < b.w : a.h < b.h;
    });
    return out;
}

std::vector<PcResolution> pc_fullscreen_modes(const PcHost& host) {
    std::vector<PcResolution> out;
    if (host.window) {
        for (const SDL_DisplayMode& mode : display_modes(wanted_display(host))) {
            out.push_back(mode_size(mode));
        }
    }
    return out;
}

std::vector<float> pc_refresh_rates(const PcHost& host, int w, int h) {
    std::vector<float> out;
    for (const PcResolution& mode : pc_fullscreen_modes(host)) {
        if (mode.w == w && mode.h == h && mode.hz > 0.0f) {
            out.push_back(mode.hz);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

PcResolution pc_window_now(const PcHost& host) {
    PcResolution now;
    if (host.window) {
        SDL_GetWindowSize(host.window, &now.w, &now.h);
    }
    return now;
}

PcResolution pc_resolution_now(const PcHost& host) {
    const PcSettings& s = host.settings;
    if (s.window_mode == WindowMode::windowed) {
        return {s.window_w > 0 ? s.window_w : host.base_w,
                s.window_h > 0 ? s.window_h : host.base_h};
    }
    // fullscreen: the mode it is in (or would go to); borderless, or no exclusive modes: the
    // desktop's
    const SDL_DisplayID display = host.window ? wanted_display(host) : SDL_GetPrimaryDisplay();
    SDL_DisplayMode mode{};
    if (s.window_mode == WindowMode::fullscreen && pick_mode(host, display, mode)) {
        return mode_size(mode);
    }
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
    return desktop ? mode_size(*desktop) : pc_window_now(host);
}

bool pc_refresh_step(PcHost& host, int step) {
    PcSettings& s = host.settings;
    if (s.window_mode != WindowMode::fullscreen) {
        return false;
    }
    const PcResolution now = pc_resolution_now(host);
    const std::vector<float> rates = pc_refresh_rates(host, now.w, now.h);
    if (rates.empty()) {
        return false;
    }
    int at = static_cast<int>(rates.size()) - 1;
    for (std::size_t i = 0; i < rates.size(); ++i) {
        if (same_rate(rates[i], now.hz)) {
            at = static_cast<int>(i);
        }
    }
    const int next = std::clamp(at + step, 0, static_cast<int>(rates.size()) - 1);
    if (next == at) {
        return false;
    }
    s.full_w = now.w;
    s.full_h = now.h;
    s.full_hz = rates[static_cast<std::size_t>(next)];
    return true;
}

bool pc_resolution_step(PcHost& host, int step) {
    PcSettings& s = host.settings;
    if (s.window_mode == WindowMode::borderless) {
        return false;
    }
    // a window's sizes, or fullscreen's: a size once, at its fastest refresh
    std::vector<PcResolution> list;
    if (s.window_mode == WindowMode::fullscreen) {
        for (const PcResolution& mode : pc_fullscreen_modes(host)) {
            if (list.empty() || list.back().w != mode.w || list.back().h != mode.h) {
                list.push_back(mode);
            }
        }
    } else {
        list = pc_window_sizes(host);
    }
    if (list.empty()) {
        return false;
    }
    const PcResolution now = pc_resolution_now(host);
    int at = -1;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].w == now.w && list[i].h == now.h) {
            at = static_cast<int>(i);
        }
    }
    const int next = std::clamp(at < 0 ? 0 : at + step, 0, static_cast<int>(list.size()) - 1);
    if (next == at) {
        return false;
    }
    const PcResolution& picked = list[static_cast<std::size_t>(next)];
    if (s.window_mode == WindowMode::windowed) {
        s.window_w = picked.w;
        s.window_h = picked.h;
    } else {
        s.full_w = picked.w;
        s.full_h = picked.h;
        s.full_hz = picked.hz;
    }
    return true;
}
