// The interpreter's walk and its command handlers (decomp src/od/u_0c5740.c).
#include "talk/talk_interp.hpp"

#include <climits>

namespace {

// frames a glyph-budget of "everything" (0x7FFFFFFF in the original)
constexpr int whole_line = INT_MAX;

// CRT rand (MSVC's LCG) for the blinks, as rand_float_crt draws it: rand() / 32768 * n
int crt_rand(TalkInterp& interp, int n) {
    interp.random = interp.random * 214013u + 2531011u;
    const int value = static_cast<int>(interp.random >> 16 & 0x7FFF);
    return static_cast<int>(static_cast<long long>(value) * n / 32768);
}

// pads_button_test(player 0, held, all, 0xC0C0), unless the starter turned it off
bool skip_chord(const TalkInterp& interp, const TalkPads& pads) {
    return pads.chord && !(interp.script_flags & talk_no_skip);
}

// decomp: event_cmd_reset 0xC5A6B
bool cmd_reset(TalkInterp& interp) {
    for (int& timer : interp.timer) {
        timer = 0;
    }
    interp.text_done = true;
    interp.page_skip = false;
    interp.fade_in = 0;
    interp.fade_out = 0;
    return true;
}

// event_cmd_text, one merged pair: the furigana half (a blank in every US script) isn't
// drawn, and the pair's second half, which only counts text_slot, is folded in
// decomp: event_cmd_text 0xC5AFD
bool cmd_text(TalkInterp& interp, const TalkScript& script, const TalkContext& context,
              const TalkPads& pads, Draw2D& draw, std::vector<int>& sounds) {
    const TalkCommand& command = script.commands[static_cast<std::size_t>(interp.pc)];
    const bool hold = (command.flags & 0x80000000u) != 0;
    const std::uint32_t flags = command.flags & 0x7FFFFFFFu;
    const bool chord = skip_chord(interp, pads);
    int& timer = interp.timer[op_text];

    // how much shows: passed before, drawn whole, A-skipped, or typing
    int budget = whole_line;
    int how = 3;
    if (interp.pc_high > interp.pc) {
        how = 0;
    } else if (command.speed <= 0) {
        how = 1;
    } else if (interp.page_skip) {
        how = 2;
    } else {
        timer += 1;
        if (chord) {
            timer = command.speed;
        }
        if (timer >= command.speed) {
            timer = 0;
            interp.glyphs_shown += 1;
        }
        budget = chord ? whole_line : interp.glyphs_shown;
    }

    // the chord marks it done without drawing it; otherwise draw, blipping on a new glyph
    if (chord) {
        interp.text_done = true;
    } else {
        int opts = 0;
        if (timer <= 0 && !(interp.script_flags & talk_quiet)) {
            opts |= 1;
        }
        if (interp.script_flags & talk_flag_2) {
            opts |= 2;
        }
        const GlyphText& line = script.lines[static_cast<std::size_t>(command.text)];
        interp.text_done =
            wide_text_draw(interp, context, line, flags, command.x, command.y, command.texture,
                           command.color, budget, opts, pads, draw, sounds);
    }

    if (interp.text_done) {
        if (how == 2) {
            sounds.push_back(sound_skip_done);
        }
        if (how == 2 || how == 3) {
            // a typed line, finished or A-skipped, clears the count. Retail clears it only when
            // one finishes typing, so after an A skip the next line starts part revealed
            // (docs/bugs.md)
            timer = 0;
            interp.glyphs_shown = 0;
        }
        return true;
    }
    if (pads.accept && !hold) {
        interp.page_skip = true;
    }
    return false;
}

// the page break
// decomp: event_cmd_wait 0xC5FAB
bool cmd_wait(TalkInterp& interp, const TalkCommand& command, const TalkPads& pads) {
    int& timer = interp.timer[op_wait];
    if (command.frames >= 0) {
        timer += timer < INT_MAX ? 1 : 0;
        if (skip_chord(interp, pads)) {
            timer = INT_MAX;
        }
    }
    if (timer > 0 && timer >= command.frames) {
        // each page starts its portraits afresh
        timer = 0;
        interp.timer[op_countdown_a] = 0;
        interp.timer[op_countdown_b] = 0;
        interp.flags &= ~3;
        interp.script_flags &= ~static_cast<std::uint32_t>(talk_fading_in | talk_fading_out);
        interp.fade_in = 0;
        interp.fade_out = 0;
        for (int i = 0; i < 6; ++i) {
            interp.blink[i] = 0;
            interp.blink_wait[i] = 0;
            interp.mouth_wait[i] = 0;
            interp.mouth[i] = 0;
        }
        return true;
    }
    return false;
}

// z 1, flags 4 whatever the script says; -1 and -2 the speakers'
// decomp: event_cmd_sprite 0xC616C
bool cmd_sprite(const TalkCommand& command, const TalkContext& context, Draw2D& draw) {
    int sprite = command.sprite;
    if (sprite == -1) {
        sprite = context.identity_sprite_a;
    } else if (sprite == -2) {
        sprite = context.identity_sprite_b;
    }
    if (sprite >= 0) {
        submit_sprite(draw, *context.registry, sprite, static_cast<float>(command.x),
                      static_cast<float>(command.y), command.order, command.color, 1.0f);
    }
    return true;
}

// the base picture with its fade, then the blinking eyes and the
// talking mouth once fully in
// decomp: event_cmd_portrait 0xC625F
bool cmd_portrait(TalkInterp& interp, const TalkCommand& command, const TalkContext& context,
                  Draw2D& draw) {
    const SpriteRegistry& registry = *context.registry;
    if (command.actor < 0 || command.actor >= 28 || command.expression < 0 ||
        command.expression >= 9) {
        return true;
    }
    const int row =
        registry.actors[static_cast<std::size_t>(command.actor)].expression[command.expression];
    if (row < 0 || static_cast<std::size_t>(row) >= registry.portraits.size()) {
        return true;
    }
    const PortraitSprite& portrait = registry.portraits[static_cast<std::size_t>(row)];
    const int cast = context.cast_slot[command.actor];
    const int bias = cast != -1 ? registry.bias[cast] : 0;
    const int half = static_cast<int>(static_cast<float>(context.frame_cadence) * 0.5f);
    const float x = static_cast<float>(command.x);
    const float y = static_cast<float>(command.y);

    // one fade per page, shared by every fading portrait: the first starts it, each counts it
    const bool fades_in = (command.flags & 1) != 0;
    const bool fades_out = (command.flags & 2) != 0;
    if (fades_in && !(interp.script_flags & talk_fading_in) && interp.fade_in <= 0) {
        interp.fade_in = half;
        interp.script_flags |= talk_fading_in;
    }
    if (fades_out && !(interp.script_flags & talk_fading_out) && interp.fade_out <= 0) {
        interp.fade_out = half;
        interp.script_flags |= talk_fading_out;
    }
    if (fades_in && (interp.script_flags & talk_fading_in) && interp.fade_in > 0) {
        interp.fade_in -= 1;
    }
    if (fades_out && (interp.script_flags & talk_fading_out) && interp.fade_out > 0) {
        interp.fade_out -= 1;
    }
    float z = 1.0f;
    if (fades_in && (interp.script_flags & talk_fading_in) && interp.fade_in > 0) {
        z = 1.0f - static_cast<float>(interp.fade_in) / static_cast<float>(half);
    } else if (fades_out && (interp.script_flags & talk_fading_out)) {
        z = static_cast<float>(interp.fade_out) / static_cast<float>(half);
    }
    submit_sprite(draw, registry, portrait.base, x, y, command.order, command.color, z, bias);
    if (z < 1.0f) {
        return true;
    }

    // per cast slot state; an actor outside the cast (no retail script has one) indexes -1 in
    // retail, aliasing other fields, and here gets a slot of its own that nothing else uses
    int& blink = cast >= 0 ? interp.blink[cast] : interp.uncast[0];
    int& blink_wait = cast >= 0 ? interp.blink_wait[cast] : interp.uncast[1];
    int& mouth_wait = cast >= 0 ? interp.mouth_wait[cast] : interp.uncast[2];
    int& mouth = cast >= 0 ? interp.mouth[cast] : interp.uncast[3];

    // the eyes: shut for 8..11 frames every one to two seconds
    if (blink > 0) {
        submit_sprite(draw, registry, portrait.overlay,
                      x + static_cast<float>(portrait.overlay_offset[0]),
                      y + static_cast<float>(portrait.overlay_offset[1]), command.order + 1,
                      command.color, z, bias);
        blink -= 1;
    } else if (blink_wait > 0) {
        blink_wait -= 1;
    } else {
        blink_wait = context.frame_cadence + crt_rand(interp, context.frame_cadence);
        blink = 8 + crt_rand(interp, 4);
    }

    // the mouth: phases 0, 1, 2, 1 every 5 frames while a glyph printed since the last step
    if (command.flags & 4) {
        return true;
    }
    if (mouth_wait > 0) {
        mouth_wait -= 1;
    } else if (interp.talking) {
        mouth = (mouth + 1) & 3;
        mouth_wait = 5;
    } else {
        mouth = 0;
    }
    interp.talking = false;
    const int phase = mouth == 0 ? 0 : mouth == 2 ? 2 : 1;
    if (portrait.phase[phase] != -1) {
        submit_sprite(draw, registry, portrait.phase[phase],
                      x + static_cast<float>(portrait.phase_offset[phase][0]),
                      y + static_cast<float>(portrait.phase_offset[phase][1]), command.order + 1,
                      command.color, z, bias);
    }
    return true;
}

// a timer the chord ends, negative holds forever
// decomp: event_cmd_delay 0xC696F
bool cmd_delay(TalkInterp& interp, const TalkCommand& command, const TalkPads& pads) {
    if (interp.pc_high > interp.pc) {
        return true;
    }
    if (command.frames < 0) {
        return false;
    }
    int& timer = interp.timer[op_delay];
    timer += 1;
    if (skip_chord(interp, pads)) {
        timer = command.frames + 1;
    }
    if (timer > command.frames) {
        timer = 0;
        return true;
    }
    return false;
}

// a new press, or the chord once its timer is past 1 (nothing but
// reset clears that timer, so only the script's first page wait holds the chord off a frame)
// decomp: event_cmd_page_wait 0xC6B24
bool cmd_page_wait(TalkInterp& interp, const TalkPads& pads, std::vector<int>& sounds) {
    if (interp.pc_high > interp.pc) {
        return true;
    }
    int& timer = interp.timer[op_page_wait];
    timer += 1;
    if (timer > 1 && (skip_chord(interp, pads) || pads.accept)) {
        sounds.push_back(sound_page);
        interp.page_skip = false;
        return true;
    }
    return false;
}

// event_cmd_timer_a / _b: load and count down; they never hold the script
bool cmd_countdown(TalkInterp& interp, const TalkCommand& command, int bit) {
    if (interp.pc_high > interp.pc) {
        return true;
    }
    int& timer = interp.timer[command.type];
    if (interp.flags & bit) {
        timer -= 1;
        if (timer <= 0) {
            interp.flags &= ~bit;
        }
    } else {
        timer = command.frames;
        interp.flags |= bit;
    }
    return true;
}

bool run_command(TalkInterp& interp, const TalkScript& script, const TalkContext& context,
                 const TalkPads& pads, Draw2D& draw, std::vector<int>& sounds) {
    const TalkCommand& command = script.commands[static_cast<std::size_t>(interp.pc)];
    switch (command.type) {
    case op_reset:
        return cmd_reset(interp);
    case op_text:
        return cmd_text(interp, script, context, pads, draw, sounds);
    case op_wait:
        return cmd_wait(interp, command, pads);
    case op_sprite:
        return cmd_sprite(command, context, draw);
    case op_portrait:
        return cmd_portrait(interp, command, context, draw);
    case op_delay:
        return cmd_delay(interp, command, pads);
    case op_page_wait:
        return cmd_page_wait(interp, pads, sounds);
    case op_countdown_b:
        return cmd_countdown(interp, command, 2);
    case op_countdown_a:
        return cmd_countdown(interp, command, 1);
    case op_end:
        interp.running = false; // event_cmd_end kills the task
        return false;
    default:
        return true; // 6..9 read their record and do nothing
    }
}

} // namespace

void talk_interp_start(TalkInterp& interp, std::uint32_t script_flags, std::uint32_t seed) {
    interp = TalkInterp{};
    interp.running = true;
    interp.script_flags = script_flags;
    interp.random = seed;
    cmd_reset(interp);
}

bool talk_interp_step(TalkInterp& interp, const TalkScript& script, const TalkContext& context,
                      const TalkPads& pads, Draw2D& draw, std::vector<int>& sounds) {
    if (!interp.running) {
        return false;
    }
    // walk from the last passed wait; a handler returning false stops the frame, and a completed
    // wait moves the walk's start past itself and ends the frame too
    interp.pc = interp.pc_resume;
    bool more = true;
    while ((interp.cmd_type = script.commands[static_cast<std::size_t>(interp.pc)].type) !=
           op_end) {
        if (more) {
            more = run_command(interp, script, context, pads, draw, sounds);
        }
        interp.pc += 1;
        if (more) {
            if (interp.pc > interp.pc_high) {
                interp.pc_high = interp.pc;
                if (interp.cmd_type != op_text) {
                    interp.page_skip = false;
                }
            }
            if (interp.cmd_type == op_wait) {
                interp.pc_resume = interp.pc;
                more = false;
            }
        }
    }
    if (more) {
        run_command(interp, script, context, pads, draw, sounds);
    }
    return interp.running;
}
