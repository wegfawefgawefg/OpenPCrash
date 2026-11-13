#include "talk/talk_player.hpp"

#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "cook/sexp_read.hpp"
#include "story/story_tables.hpp"
#include "talk/talk_canvas.hpp"
#include "talk/talk_task.hpp"

#include <SDL3/SDL.h>
#include <cstdint>
#include <cstdio>

namespace {

// the default pilot name game_state_new copies in (D_0025BAF0 -> str.25bae0)
constexpr const char* default_pilot_name = "str.25bae0";
constexpr double step_seconds = 1.0 / 60.0; // talks run at the menus' 60 Hz, as in retail
constexpr int window_scale = 2;

struct Loaded {
    StoryTables tables;
    TalkFont font;
    SpriteRegistry registry;
    TalkScript script;
    TalkContext context;
    const TriggerRow* row = nullptr;
    std::uint32_t script_flags = 0;
};

// a string of data/text/strings.sexp by id, as glyphs
GlyphText table_string(const TalkFont& font, const std::string& data_dir, const char* id) {
    gsexp::ParseResult parsed;
    std::string error;
    if (parse_sexp_file(data_dir + "/text/strings.sexp", parsed, error)) {
        for (std::size_t i = 0; i < parsed.root_count(); ++i) {
            const gsexp::Node form = parsed.root(i);
            if (form.is_list() && form.child_count() >= 3 && form.child_at(1).text() == id) {
                return glyphs_from_text(font, std::string(form.child_at(2).text()));
            }
        }
    }
    return {};
}

bool load_all(Loaded& loaded, const Options& options, std::string& error) {
    if (!load_story_tables(loaded.tables, options.data_dir, error) ||
        !load_talk_font(loaded.font, options.data_dir, error) ||
        !load_sprite_registry(loaded.registry, options.data_dir, error) ||
        !load_talk_script(loaded.script, loaded.font, options.data_dir, options.talk, error)) {
        return false;
    }
    // a talk plays under its trigger row's policy; the .data groups have their starters' flags
    if (loaded.script.state >= 0) {
        const int row = trigger_row_of_state(loaded.tables, loaded.script.state);
        loaded.row = row < trigger_row_count
                         ? &loaded.tables.triggers[static_cast<std::size_t>(row)]
                         : nullptr;
    }
    const std::string& name = loaded.script.name;
    if (name.rfind("bark", 0) == 0) {
        loaded.script_flags = talk_quiet;
    } else if (name.rfind("bio", 0) == 0 || name.rfind("maker", 0) == 0) {
        loaded.script_flags = talk_no_skip | talk_flag_2;
    }
    loaded.context.font = &loaded.font;
    loaded.context.registry = &loaded.registry;
    loaded.context.pilot_name = table_string(loaded.font, options.data_dir, default_pilot_name);
    return true;
}

// a bark, bio or dealer talk draws over its starter's screen, which has loaded the sets its
// sprites use; the player stands in for that screen and loads each sprite's own set
void bind_starter_sets(TalkTask& task, const Loaded& loaded) {
    if (loaded.row) {
        return;
    }
    for (const TalkCommand& command : loaded.script.commands) {
        const std::size_t id = static_cast<std::size_t>(command.sprite);
        if (command.type == op_sprite && command.sprite >= 0 &&
            id < loaded.registry.sprites.size()) {
            bind_texture_set(task.slots, loaded.registry, loaded.registry.sprites[id].asset);
        }
    }
}

// headless: run the frames with scripted presses and write the last one
int run_capture(Loaded& loaded, const Options& options) {
    TalkTask task;
    talk_task_begin(task, loaded.row, loaded.registry, loaded.context, loaded.script_flags, 1);
    bind_starter_sets(task, loaded);
    TalkCanvas canvas;
    std::vector<int> sounds;
    int presses = 0;
    for (int frame = 0; frame < options.frames; ++frame) {
        TalkPads pads;
        pads.accept = options.talk_press > 0 && frame > 0 && frame % options.talk_press == 0;
        pads.chord = options.talk_chord >= 0 && frame >= options.talk_chord;
        presses += pads.accept ? 1 : 0;
        Draw2D draw;
        sounds.clear();
        talk_task_frame(task, loaded.script, loaded.context, pads, draw, sounds);
        if (frame + 1 == options.frames) {
            canvas_draw(canvas, task.slots, draw, 0xFF000000);
        }
    }
    if (!canvas_save_png(canvas, options.capture, window_scale)) {
        std::fprintf(stderr, "talk: can't write %s: %s\n", options.capture.c_str(), SDL_GetError());
        return 1;
    }
    std::printf("talk %s: %d frames, %d presses, pc %d of %zu, %s\n", loaded.script.name.c_str(),
                options.frames, presses, task.interp.pc_high, loaded.script.commands.size(),
                task.phase == TalkPhase::Done ? "ended" : "running");
    return 0;
}

// the policy's music: a catalogue track at its volume (202, the shuffled catalogue, isn't played
// yet: it needs the music player's shuffle)
void start_music(Audio* audio, const TalkTask& task) {
    if (audio && task.music >= 0 && task.music < 202) {
        play_music(*audio, task.music, true, static_cast<float>(task.volume_percent) / 100.0f);
    }
}

int run_window(Loaded& loaded, const Options& options) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Phantom Crash talk", 640 * window_scale, 480 * window_scale,
                                     SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        std::fprintf(stderr, "window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderLogicalPresentation(renderer, 640, 480, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_Texture* frame_texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 640, 480);
    SDL_SetTextureScaleMode(frame_texture, SDL_SCALEMODE_NEAREST);
    AudioPtr audio = init_audio(options.data_dir);
    if (audio) {
        load_sound_bank(*audio, bank_sys, "sys");
    }

    TalkTask task;
    talk_task_begin(task, loaded.row, loaded.registry, loaded.context, loaded.script_flags,
                    static_cast<std::uint32_t>(SDL_GetTicks()));
    bind_starter_sets(task, loaded);
    start_music(audio.get(), task);
    TalkCanvas canvas;
    std::vector<int> sounds;
    SDL_Gamepad* gamepad = nullptr;
    bool accept_latched = false;
    bool running = true;
    double accumulator = 0.0;
    double previous = static_cast<double>(SDL_GetTicksNS()) / 1e9;
    while (running && task.phase != TalkPhase::Done) {
        // pump input: presses latch until the next step, the chord is held state
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                const SDL_Scancode key = event.key.scancode;
                running = running && key != SDL_SCANCODE_ESCAPE;
                accept_latched |= key == SDL_SCANCODE_Z || key == SDL_SCANCODE_RETURN ||
                                  key == SDL_SCANCODE_SPACE;
            } else if (event.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad) {
                gamepad = SDL_OpenGamepad(event.gdevice.which);
            } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                accept_latched |= event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
                                  event.gbutton.button == SDL_GAMEPAD_BUTTON_START;
            }
        }

        // run fixed steps, one talk frame each
        const double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
        accumulator += now - previous;
        previous = now;
        Draw2D draw;
        bool stepped = false;
        for (int steps = 0; accumulator >= step_seconds && steps < 8; ++steps) {
            TalkPads pads;
            pads.accept = accept_latched;
            accept_latched = false;
            const bool* keys = SDL_GetKeyboardState(nullptr);
            pads.chord = keys[SDL_SCANCODE_TAB];
            if (gamepad) {
                pads.chord = pads.chord ||
                             (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000 &&
                              SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000 &&
                              SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK) &&
                              SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK));
            }
            draw = Draw2D{};
            sounds.clear();
            talk_task_frame(task, loaded.script, loaded.context, pads, draw, sounds);
            for (const int cue : sounds) {
                if (audio) {
                    play_sound(*audio, bank_sys, cue);
                }
            }
            accumulator -= step_seconds;
            stepped = true;
        }
        accumulator = accumulator > step_seconds ? step_seconds : accumulator;
        if (audio) {
            update_audio(*audio);
        }

        // nothing new to draw: sleep until the next step
        if (!stepped) {
            SDL_DelayPrecise(static_cast<std::uint64_t>((step_seconds - accumulator) * 1e9));
            continue;
        }

        // draw frame
        canvas_draw(canvas, task.slots, draw, 0xFF000000);
        SDL_UpdateTexture(frame_texture, nullptr, canvas.rgba.data(), canvas.width * 4);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, frame_texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    // shutdown
    audio.reset();
    if (gamepad) {
        SDL_CloseGamepad(gamepad);
    }
    SDL_DestroyTexture(frame_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace

int run_talk_player(const Options& options) {
    Loaded loaded;
    std::string error;
    if (!load_all(loaded, options, error)) {
        std::fprintf(stderr, "talk: %s\n", error.c_str());
        return 1;
    }
    return options.capture.empty() ? run_window(loaded, options) : run_capture(loaded, options);
}
