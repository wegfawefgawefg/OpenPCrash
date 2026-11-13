#include "setup/setup_screen.hpp"

#include "build_info.hpp"
#include "game_paths.hpp"
#include "setup/setup_draw.hpp"
#include "setup/setup_job.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace {

enum class Action { browse, folder, quit, cancel };

struct Button {
    Action action;
    const char* label;
    SDL_FRect rect;
};

// a path picked in SDL's file dialog, which may answer on another thread
struct DialogSlot {
    std::mutex mutex;
    bool answered = false;
    std::string path;
    std::string error;
};

void SDLCALL dialog_answer(void* userdata, const char* const* files, int) {
    DialogSlot& slot = *static_cast<DialogSlot*>(userdata);
    std::lock_guard<std::mutex> lock(slot.mutex);
    slot.answered = true;
    if (!files) {
        slot.error = SDL_GetError();
    } else if (files[0]) {
        slot.path = files[0];
    }
}

struct Screen {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SetupArt art;
    SetupJob job;
    DialogSlot* dialog = nullptr;
    std::vector<Button> buttons;
    int focus = 0;
    bool hovering = false; // a file dragged over the window
    std::string note;      // a line under the buttons (a dialog that couldn't open, a stop)
    int done_frames = 0;
};

bool working(const Screen& screen) {
    const SetupStage stage = screen.job.stage.load();
    return stage == SetupStage::opening || stage == SetupStage::extracting ||
           stage == SetupStage::cooking;
}

void lay_out_buttons(Screen& screen) {
    const bool busy = working(screen) || screen.job.stage.load() == SetupStage::done;
    std::vector<Button> buttons;
    if (busy) {
        if (screen.job.stage.load() != SetupStage::cooking &&
            screen.job.stage.load() != SetupStage::done) {
            buttons.push_back({Action::cancel, "CANCEL", {510, 664, 260, 66}});
        }
    } else {
        buttons.push_back({Action::browse, "BROWSE...", {216, 664, 260, 66}});
        buttons.push_back({Action::folder, "FOLDER...", {510, 664, 260, 66}});
        buttons.push_back({Action::quit, "QUIT", {804, 664, 260, 66}});
    }
    if (buttons.size() != screen.buttons.size()) {
        screen.focus = 0;
    }
    screen.buttons = buttons;
}

void start(Screen& screen, const Options& options, const std::string& path) {
    if (working(screen) || path.empty()) {
        return;
    }
    SDL_Log("setup: reading %s", path.c_str());
    screen.note.clear();
    screen.job.data_dir = options.data_dir;
    screen.job.cooked_dir = options.cooked_dir;
    setup_start(screen.job, path);
}

// returns true to quit
bool press(Screen& screen, Action action) {
    switch (action) {
    case Action::browse: {
        static const SDL_DialogFileFilter filters[] = {
            {"Xbox disc image (.iso)", "iso;xiso"},
            {"All files", "*"},
        };
        SDL_ShowOpenFileDialog(dialog_answer, screen.dialog, screen.window, filters, 2, nullptr,
                               false);
        return false;
    }
    case Action::folder:
        SDL_ShowOpenFolderDialog(dialog_answer, screen.dialog, screen.window, nullptr, false);
        return false;
    case Action::cancel:
        setup_stop(screen.job);
        screen.note = "Stopped. Drag the disc in again whenever you like.";
        return false;
    case Action::quit:
        return true;
    }
    return false;
}

// a dashed frame for the drop area
void dashed_frame(SetupArt& art, SDL_FRect r, SetupColor color) {
    for (float x = r.x; x < r.x + r.w; x += 28) {
        const float w = std::min(16.0f, r.x + r.w - x);
        fill_rect(art, {x, r.y, w, 3}, color);
        fill_rect(art, {x, r.y + r.h - 3, w, 3}, color);
    }
    for (float y = r.y; y < r.y + r.h; y += 28) {
        const float h = std::min(16.0f, r.y + r.h - y);
        fill_rect(art, {r.x, y, 3, h}, color);
        fill_rect(art, {r.x + r.w - 3, y, 3, h}, color);
    }
}

void draw(Screen& screen) {
    SetupArt& art = screen.art;
    draw_setup_frame(art, "SETUP",
                     "Nothing is uploaded: your copy stays on this computer.",
                     "v" + build_label().substr(build_label().find(' ') + 1)); // fits the rise
    draw_text(art, SetupFont::small, "First run: OpenPCrash needs your own copy of Phantom Crash",
              1190, 150, setup_white, 1);
    const SetupStage stage = screen.job.stage.load();
    const SDL_FRect panel{184, 236, 912, 390};
    if (working(screen) || stage == SetupStage::done) {
        // the work: what it's doing, the bar, the share done
        fill_round_rect(art, panel, 18, {2, 8, 30, 230});
        draw_text(art, SetupFont::heading,
                  stage == SetupStage::done ? "Ready. Starting the game..." : "Setting up",
                  640, 290, setup_white);
        draw_text(art, SetupFont::body, setup_activity(screen.job), 640, 370, setup_grey);
        const int permille = setup_permille(screen.job);
        draw_bar(art, {232, 430, 816, 50}, permille);
        char share[16];
        std::snprintf(share, sizeof share, "%d%%", permille / 10);
        draw_text(art, SetupFont::heading, share, 640, 500, setup_yellow);
        draw_text(art, SetupFont::small, "This takes about a minute. It happens only once.", 640,
                  570, setup_grey);
    } else if (stage == SetupStage::failed) {
        // what went wrong, plainly, and what to do
        const SetupFailure failure = setup_failure(screen.job);
        fill_round_rect(art, panel, 18, {2, 8, 30, 230});
        draw_text(art, SetupFont::heading, failure.title, 640, 280, setup_yellow);
        draw_text(art, SetupFont::body, failure.detail, 640, 350, setup_white, 0, 800);
        draw_text(art, SetupFont::small,
                  "Drag another image or folder onto this window, or browse for one.", 640, 572,
                  setup_grey);
    } else {
        // the ask: drop it here
        fill_round_rect(art, panel, 18,
                        screen.hovering ? SetupColor{18, 44, 120, 235} : SetupColor{2, 8, 30, 220});
        dashed_frame(art, {panel.x + 14, panel.y + 14, panel.w - 28, panel.h - 28},
                     screen.hovering ? setup_yellow : setup_white);
        draw_text(art, SetupFont::heading,
                  "Drag your Phantom Crash disc image (ISO) or game folder onto this window", 640,
                  300, setup_white, 0, 760);
        draw_text(art, SetupFont::body,
                  "An .iso made from your own Xbox disc, or the folder that holds default.xbe and "
                  "Media. It's read once, here on this computer, and then the game starts.",
                  640, 440, setup_grey, 0, 760);
    }
    for (std::size_t i = 0; i < screen.buttons.size(); ++i) {
        draw_button(art, screen.buttons[i].rect, screen.buttons[i].label,
                    static_cast<int>(i) == screen.focus);
    }
    draw_text(art, SetupFont::small, screen.note, 640, 742, setup_grey, 0, 900);
}

// the frames PHANTOMCRASH_DEV_SETUP_SHOTS asks for
std::set<int> shot_frames() {
    std::set<int> frames;
    const char* text = std::getenv("PHANTOMCRASH_DEV_SETUP_SHOTS");
    for (const char* at = text; at && *at;) {
        frames.insert(std::atoi(at));
        const char* comma = std::strchr(at, ',');
        at = comma ? comma + 1 : nullptr;
    }
    return frames;
}

void write_shot(Screen& screen, const Options& options, int frame) {
    const std::string dir =
        options.capture.empty() ? "" : options.capture.substr(0, options.capture.rfind('/') + 1);
    const std::string path = dir + "setup_" + std::to_string(frame) + ".png";
    SDL_Surface* shot = SDL_RenderReadPixels(screen.renderer, nullptr);
    if (shot && IMG_SavePNG(shot, path.c_str())) {
        std::printf("setup: wrote %s\n", path.c_str());
    }
    SDL_DestroySurface(shot);
}

} // namespace

int run_setup_screen(const Options& options) {
    // a dialog still open when the screen closes answers later: its slot outlives the screen
    static DialogSlot dialog;
    Screen screen;
    screen.dialog = &dialog;
    if (!SDL_CreateWindowAndRenderer("Phantom Crash", options.width, options.height,
                                     SDL_WINDOW_RESIZABLE, &screen.window, &screen.renderer)) {
        std::fprintf(stderr, "setup: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderLogicalPresentation(screen.renderer, static_cast<int>(setup_w),
                                     static_cast<int>(setup_h), SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(screen.renderer, 1);
    std::string error;
    if (!setup_art_open(screen.art, screen.renderer, asset_dir() + "/fonts", error)) {
        std::fprintf(stderr, "setup: %s\n", error.c_str());
        const std::string text = "OpenPCrash couldn't start its setup: " + error +
                                 "\n\nIts files may be incomplete; unpack the download again.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenPCrash", text.c_str(), screen.window);
        SDL_DestroyRenderer(screen.renderer);
        SDL_DestroyWindow(screen.window);
        return 1;
    }
    SDL_Log("setup: no game data in %s yet; the first-run screen asks for the disc",
            options.data_dir.c_str());

    const std::set<int> shots = shot_frames();
    const char* quit_env = std::getenv("PHANTOMCRASH_DEV_SETUP_QUIT");
    const int quit_frame = quit_env ? std::atoi(quit_env) : -1;
    const char* cancel_env = std::getenv("PHANTOMCRASH_DEV_SETUP_CANCEL");
    const int cancel_frame = cancel_env ? std::atoi(cancel_env) : -1;
    static std::string dev_drop; // the pushed event points at it
    if (const char* drop = std::getenv("PHANTOMCRASH_DEV_SETUP_DROP")) {
        dev_drop = drop;
    }
    std::vector<SDL_Gamepad*> pads;
    bool quit = false;
    for (int frame = 0; !quit; ++frame) {
        if (frame == 10 && !dev_drop.empty()) {
            SDL_Event drop{};
            drop.type = SDL_EVENT_DROP_FILE;
            drop.drop.windowID = SDL_GetWindowID(screen.window);
            drop.drop.data = dev_drop.c_str();
            SDL_PushEvent(&drop);
        }
        if (frame == cancel_frame && working(screen)) {
            press(screen, Action::cancel);
        }
        lay_out_buttons(screen);

        // pump input: drops, the dialog's answer, the pointer, keys and pads
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            SDL_ConvertEventToRenderCoordinates(screen.renderer, &event);
            int move = 0;
            bool pressed = false, back = false;
            switch (event.type) {
            case SDL_EVENT_QUIT:
                quit = true;
                break;
            case SDL_EVENT_DROP_BEGIN:
            case SDL_EVENT_DROP_POSITION:
                screen.hovering = true;
                break;
            case SDL_EVENT_DROP_COMPLETE:
                screen.hovering = false;
                break;
            case SDL_EVENT_DROP_FILE:
                screen.hovering = false;
                if (event.drop.data) {
                    start(screen, options, event.drop.data);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                for (std::size_t i = 0; i < screen.buttons.size(); ++i) {
                    const SDL_FPoint at{event.motion.x, event.motion.y};
                    if (SDL_PointInRectFloat(&at, &screen.buttons[i].rect)) {
                        screen.focus = static_cast<int>(i);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                for (std::size_t i = 0; i < screen.buttons.size(); ++i) {
                    const SDL_FPoint at{event.button.x, event.button.y};
                    if (event.button.button == SDL_BUTTON_LEFT &&
                        SDL_PointInRectFloat(&at, &screen.buttons[i].rect)) {
                        screen.focus = static_cast<int>(i);
                        pressed = true;
                    }
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                move = event.key.key == SDLK_LEFT || (event.key.key == SDLK_TAB &&
                                                      (event.key.mod & SDL_KMOD_SHIFT))
                           ? -1
                       : event.key.key == SDLK_RIGHT || event.key.key == SDLK_TAB ? 1
                                                                                  : 0;
                pressed = event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER ||
                          event.key.key == SDLK_SPACE;
                back = event.key.key == SDLK_ESCAPE;
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which)) {
                    pads.push_back(pad);
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                move = event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT    ? -1
                       : event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? 1
                                                                                : 0;
                pressed = event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH;
                back = event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST;
                break;
            default:
                break;
            }
            const int count = static_cast<int>(screen.buttons.size());
            if (count > 0 && move != 0) {
                screen.focus = (screen.focus + move + count) % count;
            }
            if (back) {
                // Escape stops the work, else leaves
                quit = working(screen) ? press(screen, Action::cancel) : true;
            } else if (pressed && screen.focus < count) {
                quit = press(screen, screen.buttons[static_cast<std::size_t>(screen.focus)].action);
            }
        }
        {
            std::lock_guard<std::mutex> lock(dialog.mutex);
            if (dialog.answered) {
                dialog.answered = false;
                if (!dialog.error.empty()) {
                    screen.note = "No file browser could open here (" + dialog.error +
                                  "). Drag the image or folder onto the window instead.";
                    dialog.error.clear();
                }
                if (!dialog.path.empty()) {
                    start(screen, options, dialog.path);
                    dialog.path.clear();
                }
            }
        }

        // the work done: a moment on "Ready", then the game
        if (screen.job.stage.load() == SetupStage::done && ++screen.done_frames > 45) {
            break;
        }

        // draw frame
        SDL_SetRenderDrawColor(screen.renderer, 0, 0, 0, 255);
        SDL_RenderClear(screen.renderer);
        draw(screen);
        if (shots.count(frame) != 0) {
            write_shot(screen, options, frame);
        }
        SDL_RenderPresent(screen.renderer);
        if (frame == quit_frame) {
            quit = true;
        }
        SDL_Delay(16);
    }

    // shutdown: the work stopped (a quit while it runs), the window closed
    setup_stop(screen.job);
    for (SDL_Gamepad* pad : pads) {
        SDL_CloseGamepad(pad);
    }
    setup_art_close(screen.art);
    SDL_DestroyRenderer(screen.renderer);
    SDL_DestroyWindow(screen.window);
    const bool ready = !quit && setup_data_ready(options.data_dir);
    SDL_Log("setup: %s", ready ? "data ready, starting the game" : "quit");
    return ready ? 0 : 1;
}
