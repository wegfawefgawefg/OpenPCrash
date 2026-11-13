// The conversation states (0x30..0x9D) as the talk task plays them (src/talk/talk_task.hpp), and a
// placeholder for the states the port has no screen for yet, which says so and goes on the way the
// original's state would.
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"

#include <cstdio>

namespace {

// ASCII as the font's full-width forms (ascii_char_to_fullwidth's table does the same)
std::string full_width(const std::string& ascii) {
    std::string out;
    for (const char c : ascii) {
        std::uint32_t code = static_cast<unsigned char>(c);
        code = c == ' ' ? 0x3000 : (code > 0x20 && code < 0x7f) ? code - 0x20 + 0xff00 : code;
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xc0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    return out;
}

// where a placeholder goes on any press, as the state would (the next state on the day's round:
// the game never stops at a state that isn't ported)
int placeholder_next(int state, int) {
    switch (state) {
    case app_adjust:     // screen adjust
    case app_key_config: // key config
        return app_options;
    default:
        return app_title_return;
    }
}

} // namespace

void talk_state_spawn(FrontEnd& fe, Ui& ui) {
    char name[16];
    std::snprintf(name, sizeof name, "talk_%02x", fe.state);
    std::string error;
    fe.talk = TalkTask{};
    if (!load_talk_script(fe.talk_script, ui.data.font, ui.data.data_dir, name, error)) {
        std::fprintf(stderr, "talk: %s\n", error.c_str());
        fe.talk.phase = TalkPhase::Done;
        return;
    }
    const int row = trigger_row_of_state(fe.story_tables, fe.state);
    const TriggerRow* trigger =
        row < trigger_row_count ? &fe.story_tables.triggers[static_cast<std::size_t>(row)] : nullptr;
    talk_task_begin(fe.talk, trigger, ui.registry, fe.talk_context, 0, ui.task_timer + 1);
    for (const auto& [slot, path] : fe.talk.slots.files) {
        ui.slots.files[slot] = path;
    }
    // the policy's music (202, the shuffled catalogue, needs the music player's shuffle)
    if (ui.audio && fe.talk.music >= 0 && fe.talk.music < 202) {
        play_music(*ui.audio, fe.talk.music, true,
                   static_cast<float>(fe.talk.volume_percent) / 100.0f);
    }
    // the talk task fades itself in from black and out again
    ui.fade = UiFade{};
    // PC: the skip binds start afresh; the hint offers "skip all" when more conversations follow
    fe.talk_skip = TalkSkip{};
    fe.talk_skip.more = talk_chain_continues(fe);
}

void talk_state_step(FrontEnd& fe, Ui& ui) {
    if (fe.talk.phase != TalkPhase::Done) {
        // PC: Back or a right click skips the conversation (src/talk/talk_skip.hpp)
        fe.talk_context.frame_cadence = ui.frame_cadence;
        if (talk_skip_update(fe.talk_skip, ui.pad, ui.pointer.right)) {
            talk_task_skip(fe.talk, fe.talk_script, fe.talk_context);
        }
        TalkPads pads;
        pads.accept = menu_accept(ui);
        pads.chord = text_skip_held(ui.pad);
        fe.talk_context.input_age = menu_input_age(ui);
        std::vector<int> sounds;
        talk_task_frame(fe.talk, fe.talk_script, fe.talk_context, pads, ui.draw, sounds);
        for (const int cue : sounds) {
            ui_sound(ui, bank_sys, cue);
        }
        if (!fe.talk_skip.skipped && fe.talk.phase != TalkPhase::Close) {
            talk_skip_hint_draw(fe.talk_context, fe.talk_skip.more, ui.draw);
        }
    }
    if (fe.talk.phase == TalkPhase::Done) {
        // a group skip passes over the rest of the chain (front_end.cpp, talk_chain_skip)
        fe.talk_chain_skip = fe.talk_skip.group;
        task_header_set(fe, app_none); // a conversation ends by asking for 0x9F
    }
}

void talk_state_destroy(FrontEnd&, Ui& ui) {
    // the talk faded out to black: the next screen fades in from there
    ui.fade.state = 2;
    ui.fade.timer = 0;
}

void placeholder_spawn(FrontEnd& fe, Ui& ui) {
    PlaceholderScreen& p = fe.screen.placeholder;
    p.state = fe.state;
    char line[96];
    const std::string& name = fe.states[static_cast<std::size_t>(fe.state)].name;
    std::snprintf(line, sizeof line, "State 0x%X %s", fe.state, name.c_str());
    p.lines[0] = utf8_to_glyphs(ui.data, full_width(line));
    p.lines[1] = utf8_to_glyphs(ui.data, full_width("is not ported yet."));
    p.result = placeholder_next(fe.state, fe.previous);
    std::snprintf(line, sizeof line, "Any button goes on to 0x%X.", p.result);
    p.lines[2] = utf8_to_glyphs(ui.data, full_width(line));
}

void placeholder_step(FrontEnd& fe, Ui& ui) {
    PlaceholderScreen& p = fe.screen.placeholder;
    for (int i = 0; i < 4; ++i) {
        text_draw(ui, p.lines[i], 64, 180 + i * 24, 6, 18, 18, 0xfff0f0f0, 0x40);
    }
    switch (fe.screen.step) {
    case 0:
        texture_set_load(ui, 0x690);
        fade_in_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
        fe.screen.step = 1;
        break;
    case 1:
        if (!fade_is_idle(ui)) {
            break;
        }
        if (ui.pad.b.rising != 0 || ui.pointer.left || ui.pointer.right) {
            ui_sound(ui, bank_sys, se_menu_accept);
            fade_out_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
            fe.screen.step = 2;
        }
        break;
    case 2:
        if (fade_is_black(ui)) {
            task_header_set(fe, p.result);
            fe.screen.step = 3;
        }
        break;
    default:
        break;
    }
}
