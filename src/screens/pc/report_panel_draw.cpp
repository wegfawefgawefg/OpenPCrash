// The bug reports' panel drawn (report_panel.hpp): a dim over the screen, a plate framed in white,
// the key config's letters, and blank option buttons with their words in the same letters (the
// buttons' own font has no Y). Over everything but the fade.
#include "screens/flow/front_end.hpp"
#include "screens/pc/report_panel.hpp"
#include "screens/pc/report_panel_layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

// the pause's pass, over it and the HUD (the fade and a scene's letterbox are over this one: the
// note's panel stays between the letterbox's bars)
constexpr int layer = 0x7ffd;
constexpr int solid = 0xe7c; // the fade's solid sprite
constexpr std::uint32_t white = 0xffffffff, grey = 0xffb0bad6, yellow = 0xfffcc400;
constexpr std::uint32_t black = 0xff000000;
constexpr char strip[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,-/:+";
constexpr float strip_x = 68, strip_y = 178, strip_pitch = 12;

void plate(Ui& ui, float x, float y, float w, float h, std::uint32_t colour, int at) {
    Sprite s = sprite_of(ui, solid);
    const float sw = static_cast<float>(std::max(sprite_width(ui, solid), 1));
    const float sh = static_cast<float>(std::max(sprite_height(ui, solid), 1));
    s.pos[0] = x;
    s.pos[1] = y;
    s.scale[0] = w / sw;
    s.scale[1] = h / sh;
    s.blend = at;
    s.color = colour;
    s.flags = 4;
    sprite_submit(ui, s);
}

// a plate with a white frame round it
void framed(Ui& ui, float x, float y, float w, float h) {
    plate(ui, x - 2, y - 2, w + 4, h + 4, white, layer);
    plate(ui, x, y, w, h, 0xf0060c24, layer);
}

// the dim over the whole window, as the pause's
void dim(Ui& ui) {
    Sprite s = sprite_of(ui, solid);
    s.pos[0] = -32.0f;
    s.pos[1] = -32.0f;
    s.blend = layer;
    s.scale[0] = 22.0f;
    s.scale[1] = 17.0f;
    s.color = 0xa0000000;
    s.flags |= quad_full_window;
    sprite_submit(ui, s);
}

void text(FrontEnd& fe, Ui& ui, const std::string& words, float x, float y, std::uint32_t colour,
          int align = -1) {
    const float w = static_cast<float>(pc_text_width(fe.pc_art, words));
    const float left = align < 0 ? x : align == 0 ? std::floor(x - w * 0.5f) : x - w;
    pc_text_draw(ui, fe.pc_art, words, left, y, layer, colour);
}

// words wrapped at width, a line every 17; the lines' count
int wrapped(FrontEnd& fe, Ui& ui, const std::string& words, float x, float y, float width,
            std::uint32_t colour, int align = -1) {
    std::vector<std::string> lines(1);
    std::size_t at = 0;
    while (at < words.size()) {
        std::size_t end = words.find(' ', at);
        end = end == std::string::npos ? words.size() : end;
        const std::string word = words.substr(at, end - at);
        const std::string tried = lines.back().empty() ? word : lines.back() + " " + word;
        if (!lines.back().empty() && pc_text_width(fe.pc_art, tried) > width) {
            lines.push_back(word);
        } else {
            lines.back() = tried;
        }
        at = end + 1;
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        text(fe, ui, lines[i], x, y + 17.0f * static_cast<float>(i), colour, align);
    }
    return static_cast<int>(lines.size());
}

// a blank option button with its word in the label letters, black on the lit one's yellow
void button(FrontEnd& fe, Ui& ui, const PointerRect& r, const char* word, bool lit) {
    pc_button_draw(ui, fe.pc_art, "", r.left, r.top, lit, layer, lit ? sprite_blink_soft : 0);
    text(fe, ui, word, (r.left + r.right) * 0.5f, r.top + 12, lit ? black : yellow, 0);
}

void draw_question(FrontEnd& fe, Ui& ui) {
    const ReportPanel& p = fe.report;
    framed(ui, 90, 150, 460, 190);
    text(fe, ui, "SEND CRASH REPORTS TO HELP FIX BUGS", 320, 166, white, 0);
    wrapped(fe, ui,
            "WHEN THE GAME CRASHES OR HANGS IT SENDS THE END OF ITS LOG, THE BUILD, YOUR "
            "SYSTEM AND GPU, YOUR SETTINGS AND THE FIGHT INPUTS. NO NAMES. CHANGE IT IN PC "
            "CONFIG, GAME.",
            320, 194, 420, grey, 0);
    button(fe, ui, report_question_button(0), "YES", p.button == 0);
    button(fe, ui, report_question_button(1), "NO", p.button == 1);
}

void draw_note(FrontEnd& fe, Ui& ui) {
    const ReportPanel& p = fe.report;
    framed(ui, 48, 66, 544, 316);
    text(fe, ui, "REPORT A BUG", 320, 72, white, 0);
    text(fe, ui, "TYPE WHAT WENT WRONG, OR PICK LETTERS: A TYPES, X DELETES", 320, 92, grey, 0);
    // the note's box, the cursor at its end
    const bool on_note = p.row == 0 && p.said_ticks == 0;
    plate(ui, 62, 112, 516, 60, on_note ? 0xff2a3c78 : 0xff141e40, layer);
    const bool blink = (SDL_GetTicks() / 400) % 2 == 0;
    wrapped(fe, ui, p.note + (on_note && blink ? "_" : ""), 70, 116, 500, white);
    // the pad's strip, its letter lit
    if (on_note) {
        for (int i = 0; i < report_strip_size(); ++i) {
            const PointerRect r = report_strip_rect(i);
            if (i == p.letter) {
                plate(ui, r.left, r.top, r.right - r.left, r.bottom - r.top, yellow, layer);
            }
            const char c[2] = {strip[i], 0};
            text(fe, ui, c[0] == ' ' ? "_" : c, (r.left + r.right) * 0.5f, r.top + 2,
                 i == p.letter ? black : white, 0);
        }
    }
    // the picture, from the pause only
    std::string with = "WITH IT: THE END OF THE LOG, THE BUILD, YOUR SYSTEM AND GPU, YOUR "
                       "SETTINGS";
    if (p.picture_offered) {
        const PointerRect r = report_picture_rect();
        const bool lit = p.row == 1;
        text(fe, ui, "PICTURE OF THE FIGHT", r.left + 2, r.top + 3, lit ? yellow : white);
        text(fe, ui, p.picture ? "YES" : "NO", r.right - 4, r.top + 3, lit ? yellow : white, 1);
        if (p.picture) {
            with += ", THE PICTURE";
        }
    }
    with += fe.campaign.battle_running ? " AND THE FIGHT INPUTS." : ".";
    const int lines = wrapped(fe, ui, with, 320, 230, 500, grey, 0);
    if (p.said_ticks > 0) {
        wrapped(fe, ui, p.said, 320, 232 + 17.0f * static_cast<float>(lines), 500, yellow, 0);
    }
    button(fe, ui, report_note_button(0), "SEND", p.row == 2 && p.button == 0);
    button(fe, ui, report_note_button(1), "CANCEL", p.row == 2 && p.button == 1);
}

} // namespace

int report_strip_size() {
    return static_cast<int>(sizeof strip - 1);
}

char report_strip_letter(int i) {
    return i >= 0 && i < report_strip_size() ? strip[i] : ' ';
}

bool report_letter_allowed(char c) {
    return c != '\0' && std::strchr(strip, c) != nullptr;
}

PointerRect report_strip_rect(int i) {
    const float x = strip_x + strip_pitch * static_cast<float>(i);
    return {x - 1, strip_y, x + strip_pitch - 1, strip_y + 20};
}

PointerRect report_question_button(int i) {
    return pc_button_rect(i == 0 ? 114.0f : 334.0f, 280.0f);
}

PointerRect report_note_button(int i) {
    return pc_button_rect(i == 0 ? 104.0f : 344.0f, 334.0f);
}

PointerRect report_picture_rect() {
    return {64, 202, 576, 224};
}

void report_panel_draw(FrontEnd& fe, Ui& ui) {
    const ReportPanel& p = fe.report;
    if (p.mode == 0 || p.shot == 1 || p.shot == 3) {
        return;
    }
    pc_art_load(fe.pc_art, ui);
    dim(ui);
    if (p.mode == 1) {
        draw_question(fe, ui);
    } else {
        draw_note(fe, ui);
    }
}
