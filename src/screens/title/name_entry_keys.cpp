// The name entry's keyboard (port-only, docs/input.md "Name entry"): a typed letter, digit or
// symbol goes straight into the name as the grid's glyph for it, Backspace deletes the last glyph
// (retail's X; Back is B there, which turns to the END page), Enter ends the entry as the END
// tab's accept. The pad's buttons are retail's (name_entry.cpp), and so are Escape and the arrows.
#include "screens/flow/front_end.hpp"
#include "screens/pc/pc_settings.hpp"

#include <SDL3/SDL.h>

namespace {

void utf8_put(std::string& out, std::uint32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xc0 | c >> 6);
        out += static_cast<char>(0x80 | (c & 0x3f));
    } else {
        out += static_cast<char>(0xe0 | c >> 12);
        out += static_cast<char>(0x80 | (c >> 6 & 0x3f));
        out += static_cast<char>(0x80 | (c & 0x3f));
    }
}

// the grid's cell text for a typed character: ASCII as its full-width form (the grid's letters
// and digits are full-width), a few to the symbol drawn like them
std::string cell_text(std::uint32_t c) {
    std::string out;
    switch (c) {
    case '-':
        utf8_put(out, 0x30fc); // ー
        return out;
    case '.':
        utf8_put(out, 0x30fb); // ・
        return out;
    case '(':
    case '[':
        utf8_put(out, 0x3014); // 〔
        return out;
    case ')':
    case ']':
        utf8_put(out, 0x3015); // 〕
        return out;
    case '*':
        utf8_put(out, 0x2605); // ★
        return out;
    default:
        break;
    }
    utf8_put(out, c > 0x20 && c < 0x7f ? c + 0xfee0 : c);
    return out;
}

// the glyph of the cell showing `text` on the letters page, or the symbols page but for a
// garage's name (which skips it); -1 when no cell does
int typed_glyph(Ui& ui, int kind, const std::string& text) {
    const UiTable* grid = ui_table(ui.data, "name_entry", "name_entry_grid");
    if (!grid) {
        return -1;
    }
    const int pages = kind == 1 ? 1 : 2;
    for (int i = 0; i < pages * 90 && i < static_cast<int>(grid->strings.size()); ++i) {
        if (grid->strings[static_cast<std::size_t>(i)] == text) {
            return name_entry_cell_glyph(ui, i / 90, i % 90 / 15, i % 15);
        }
    }
    return -1;
}

// SDL's text event: each character as a glyph (a space as Y's blank, 0); ones the grid lacks
// are dropped
void type_text(FrontEnd& fe, Ui& ui, NameEntryScreen& n, const char* text) {
    for (const unsigned char* s = reinterpret_cast<const unsigned char*>(text); *s;) {
        std::uint32_t c = *s++;
        int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        c &= more == 3 ? 0x07u : more == 2 ? 0x0fu : more == 1 ? 0x1fu : 0x7fu;
        for (; more > 0 && (*s & 0xc0) == 0x80; --more) {
            c = c << 6 | (*s++ & 0x3fu);
        }
        if (c == ' ') {
            n.typed.push_back(0);
            continue;
        }
        const int glyph = typed_glyph(ui, fe.shop.name.kind, cell_text(c));
        if (glyph >= 0) {
            n.typed.push_back(glyph);
        }
    }
}

} // namespace

bool name_entry_event(FrontEnd& fe, Ui& ui, const SDL_Event& event) {
    NameEntryScreen& n = fe.screen.name_entry;
    if (fe.state != app_name_entry || !fe.screen_alive || fe.screen.step != 2) {
        return false;
    }
    if (event.type == SDL_EVENT_TEXT_INPUT) {
        type_text(fe, ui, n, event.text.text);
        return true;
    }
    if (event.type != SDL_EVENT_KEY_DOWN) {
        return false;
    }
    // (a scripted key has its scancode only)
    const SDL_Keycode key = event.key.key
                                ? event.key.key
                                : SDL_GetKeyFromScancode(event.key.scancode, SDL_KMOD_NONE, false);
    if (key == SDLK_BACKSPACE) {
        n.typed.push_back(name_typed_delete);
        return true;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (!event.key.repeat) {
            n.typed.push_back(name_typed_done);
        }
        return true;
    }
    // a key that types is the name's (its text event follows), never a pad button
    return key >= 0x20 && key < 0x7f;
}

void name_entry_text_input(FrontEnd& fe, bool on) {
    SDL_Window* window = fe.pc ? fe.pc->window : nullptr;
    if (!window || SDL_TextInputActive(window) == on) {
        return;
    }
    if (on) {
        SDL_StartTextInput(window);
    } else {
        SDL_StopTextInput(window);
    }
}
