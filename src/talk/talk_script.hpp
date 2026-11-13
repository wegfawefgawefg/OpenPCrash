#pragma once
// A conversation as data: the event script's commands (data/scripts/*.sexp) with each line's
// text turned back into the original's glyph tokens through the font (data/text/font.sexp), so the
// interpreter (talk_interp.cpp) and the text draw (text_draw.cpp) work on what the game worked on
// (docs/text-and-scripts.md; decomp include/event.h, docs/port/progression.md).
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// glyph tokens: 0.. a glyph of the font, negative a control code, -1 the end
using GlyphText = std::vector<std::int16_t>;

// the font's glyph widths (glyph_widths) and the character each glyph draws
struct TalkFont {
    std::map<std::uint32_t, int> glyph_of; // code point -> first glyph drawing it
    std::vector<std::uint8_t> left;        // texels into the cell the drawn part starts
    std::vector<std::uint8_t> advance;     // pixels the pen moves
    int sheet_width = 512;                 // font.png (ascii_font_sys_tex, slot 518)
    int sheet_height = 2048;
    int cell = 18;
    int per_row = 28;
};

// the command types (event_cmd_handlers)
enum TalkOp : std::uint8_t {
    op_reset = 0,
    op_text = 1,
    op_wait = 2,
    op_sprite = 3,
    op_portrait = 4,
    op_delay = 5,
    op_page_wait = 10,
    op_countdown_b = 11,
    op_countdown_a = 12,
    op_end = 13,
};

// one command; a text pair is merged into one op_text (the furigana is a blank in every US
// script), which the interpreter treats as both halves of the pair (talk_interp.cpp)
struct TalkCommand {
    std::uint8_t type = op_end;
    int text = -1;           // op_text: index into TalkScript::lines
    int speed = 0;           // frames a glyph, 0 or less draws the line whole
    std::uint32_t flags = 0; // op_text: bit 31 accept can't finish it, low bits to the draw;
                             // op_portrait: 1 fade in, 2 fade out, 4 no mouth
    int x = 0;
    int y = 0;
    std::uint32_t color = 0xFFFFFFFF;
    int texture = 0;    // op_text: added to the text's layer 0x101
    int sprite = -1;    // op_sprite: render registry id
    int order = 0;      // op_sprite, op_portrait: the sprite's layer (Sprite.blend)
    int actor = 0;      // op_portrait: portrait actor 0..27
    int expression = 0; // op_portrait: 0..8
    int frames = 0;     // op_wait, op_delay, the countdowns
};

struct TalkScript {
    std::string name;                  // talk_5b, bark_2c4b60...
    int state = -1;                    // the conversation state for talks, else -1
    std::vector<TalkCommand> commands; // ends with op_end
    std::vector<GlyphText> lines;
};

bool load_talk_font(TalkFont& font, const std::string& data_dir, std::string& error);
// name: a script name (talk_5b, bark_2c4b60, bio_..., maker_...) or a talk state (0x5b)
bool load_talk_script(TalkScript& script, const TalkFont& font, const std::string& data_dir,
                      const std::string& name, std::string& error);
// UTF-8 with the text markup ({c:ffeeee66}, {player}, \n...) to glyph tokens, -1 ended
GlyphText glyphs_from_text(const TalkFont& font, const std::string& text);
// the portrait actors by the stems the scripts name them with (progression.md, "The cast")
int portrait_actor_index(const std::string& stem);
