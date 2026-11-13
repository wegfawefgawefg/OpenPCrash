#pragma once
// The event script interpreter (decomp src/od/u_0c5740.c, docs/port/progression.md "The script
// interpreter"): one EventInterp walked once a frame from the last passed wait, so everything since
// that wait is drawn again each frame. Presentation: it only draws and asks for sounds; nothing it
// does reaches the world (conversations change no progress).
#include "talk/draw2d.hpp"
#include "talk/sprite_registry.hpp"
#include "talk/talk_script.hpp"

#include <cstdint>
#include <vector>

// script_flags (EventInterp +0x9C)
enum TalkScriptFlags : std::uint32_t {
    talk_quiet = 1,            // no blip, no mouth: battle barks
    talk_flag_2 = 2,           // passed to the text draw, which sets it anyway
    talk_fading_in = 4,        // the interpreter's own: a portrait fade in has started
    talk_fading_out = 8,       // the same for a fade out
    talk_no_skip = 0x80000000, // the skip chord and the line-finishing press do nothing
};

// what player 0's pads say this frame
struct TalkPads {
    bool accept = false; // a new press of A or START (input_accept_pressed)
    bool chord = false;  // both triggers and both stick clicks held (pads_button_test 0xC0C0)
};

// the sounds a frame asks for, as (bank, cue) on the system bank
enum TalkSound : int {
    sound_text_blip = 0x10, // wide_text_sound(0): a glyph appears
    sound_page = 0x13,      // se_play: a page wait passed
    sound_skip_done = 0x14, // event_text_sound: an A-skipped line came out whole
};

// what the text and sprites need from outside the script
struct TalkContext {
    const TalkFont* font = nullptr;
    const SpriteRegistry* registry = nullptr;
    int cast_slot[28] = {};     // portrait_actor_slots: the cast slot of each actor, -1 not cast
    GlyphText pilot_name;       // control code -5 (GameState.pilot_name)
    GlyphText chip_name;        // -6, the boarded chip's
    GlyphText speaker_a;        // -7, a bark's pilot
    GlyphText speaker_b;        // -8, a bark's Scoobee
    int identity_sprite_a = -1; // sprite -1 of a bark: the speaker's identity picture
    int identity_sprite_b = -1; // sprite -2
    int input_age = 0;          // frames since the last menu input (the blink flags' clock)
    int frame_cadence = 60;     // the state's cadence: talks run at 60
};

struct TalkInterp {
    bool running = false;
    std::uint32_t script_flags = 0;
    int pc = 0;
    int pc_high = 0;   // one past the furthest command completed
    int pc_resume = 0; // where each frame's walk starts
    int cmd_type = 0;
    int timer[13] = {}; // one per command type
    int glyphs_shown = 0;
    bool text_done = true;
    bool page_skip = false;
    int flags = 0; // bit 0 countdown A running, bit 1 countdown B
    int fade_in = 0;
    int fade_out = 0;
    int blink[6] = {};
    int blink_wait[6] = {};
    int mouth_wait[6] = {};
    int mouth[6] = {};
    int uncast[4] = {};   // blink, blink wait, mouth wait, mouth of a portrait outside the cast
    bool talking = false; // event_interp_flags bit 3: a glyph printed with the blip on
    bool press_mark_up = false; // D_00A0EA20: a -4 drew its mark and won't again until reset
    std::uint32_t random = 1;   // stands in for CRT rand (blinks); presentation only
};

// event_interp_start: script_flags 0 for conversations, talk_quiet for barks,
// talk_no_skip | talk_quiet for battle scene text, talk_no_skip | talk_flag_2 for bios and dealers
void talk_interp_start(TalkInterp& interp, std::uint32_t script_flags, std::uint32_t seed);
// event_interp_step: one frame; false once the script has ended (the task is dead)
bool talk_interp_step(TalkInterp& interp, const TalkScript& script, const TalkContext& context,
                      const TalkPads& pads, Draw2D& draw, std::vector<int>& sounds);

// wide_text_draw: tokens from x, y, at most budget glyphs (colour codes don't count);
// true when it reached the end. opts bit 0 blips (and marks talking) on the last glyph drawn.
bool wide_text_draw(TalkInterp& interp, const TalkContext& context, const GlyphText& text,
                    std::uint32_t flags, int x, int y, int texture, std::uint32_t color, int budget,
                    int opts, const TalkPads& pads, Draw2D& draw, std::vector<int>& sounds);
