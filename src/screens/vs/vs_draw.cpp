// VS setup's panels (decomp docs/port/screens.md "The four panels", "Machine page";
// vs_panels_draw, vs_panel_machine_draw, vs_panel_name_draw,
// vs_record_digits_draw): their parts rolling and growing, the machine lists, the
// records, and the layout's rows and numbers.
#include "screens/flow/front_end.hpp"
#include "screens/vs/vs_setup.hpp"

#include "input/seats.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

namespace {

constexpr std::uint32_t orange = 0xFFFEB000;
constexpr std::uint32_t text_colour = 0xFFF0F0F0;
// the vs_mode file's numbers by address, tables that run into each other read as retail's code
// indexes past them (Words rows field by field)
const std::map<unsigned, double>& numbers(Ui& ui) {
    static std::map<unsigned, double> by_address;
    if (by_address.empty()) {
        for (const UiTable& t : ui_file(ui.data, "vs_mode").tables) {
            for (std::size_t k = 0; k < t.values.size(); ++k) {
                by_address[t.va + 4 * static_cast<unsigned>(k)] = t.values[k];
            }
            if (!t.rows.empty() && t.values.empty()) {
                for (std::size_t k = 0; k < t.rows[0].size(); ++k) {
                    by_address[t.va + 4 * static_cast<unsigned>(k)] = t.rows[0][k];
                }
            }
        }
    }
    return by_address;
}

} // namespace

float vs_number(Ui& ui, unsigned address) {
    const auto& map = numbers(ui);
    const auto at = map.find(address);
    return at == map.end() ? 0.0f : static_cast<float>(at->second);
}

void vs_pair_at(Ui& ui, unsigned table, int i, float& x, float& y) {
    x = vs_number(ui, table + 8u * static_cast<unsigned>(i));
    y = vs_number(ui, table + 8u * static_cast<unsigned>(i) + 4u);
}

namespace {


// a part's fill: rolling in, open, rolling out over 10 frames
float part_fill(const VsPanel& p, int part) {
    switch (p.anim[part]) {
    case 1:
        return std::min(p.clock[part] / 10.0f, 1.0f);
    case 2:
        return 1.0f;
    case 3:
        return std::max(1.0f - p.clock[part] / 10.0f, 0.0f);
    default:
        return 0.0f;
    }
}

// the record digits (vs_record_digits_draw): three places, leading zeros, a place over 9 shown 9
void digits_draw(Ui& ui, int value, float x, float y, float fill) {
    const int places[3] = {value / 100, value / 10 % 10, value % 10};
    for (int k = 0; k < 3; ++k) {
        const SpriteSlot* row = vs_row(ui, 72 + std::min(places[k], 9));
        if (!row) {
            continue;
        }
        SpriteSlot at = *row;
        at.x = x + 13.0f * static_cast<float>(k);
        at.y = y;
        Sprite sprite = sprite_of(ui, at.sprite);
        const float h = static_cast<float>(sprite_height(ui, at.sprite));
        sprite.pos[0] = at.x;
        sprite.pos[1] = at.y + (1 - fill) * h * 0.5f;
        sprite.scale[1] = fill;
        sprite.blend = at.blend;
        sprite_submit(ui, sprite);
    }
}

// PC: the device an entered panel's player joined with ("KEYBOARD AND MOUSE", "PAD 2") in PC
// CONFIG's label letters, dark, along the edge of the "You entered." box away from the hint tab
// (the panels have no other room: the tab and the PLAYER tag hug the box)
void device_draw(FrontEnd& fe, Ui& ui, int i) {
    const Seats& seats = *fe.seats;
    const SpriteSlot* box = vs_row(ui, 24 + i);
    if (!box || !seats.seats[static_cast<std::size_t>(i)].used) {
        return;
    }
    std::string text = seat_device_label(seats, seats.seats[static_cast<std::size_t>(i)].device);
    for (char& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    pc_art_load(fe.pc_art, ui);
    const PointerRect r = sprite_rect(ui, box->sprite, box->x, box->y, (box->flags & 8) != 0);
    const float w = static_cast<float>(pc_text_width(fe.pc_art, text));
    const float y = i < 2 ? r.bottom - 18.0f : r.top + 3.0f; // clear of the hint tab
    pc_text_draw(ui, fe.pc_art, text, std::floor((r.left + r.right - w) * 0.5f), y,
                 0x104, 0xFF384048);
}

// a panel has a device for it on the join page: its seat's, or one of the devices nobody holds,
// handed out to the empty panels in order
bool panel_connected(const FrontEnd& fe, int i) {
    const Seats& seats = *fe.seats;
    if (seats.seats[static_cast<std::size_t>(i)].used) {
        return seat_connected(seats, i);
    }
    int empty_before = 0;
    for (int k = 0; k < i; ++k) {
        empty_before += seats.seats[static_cast<std::size_t>(k)].used ? 0 : 1;
    }
    return empty_before < seats_free_devices(seats);
}

} // namespace

const SpriteSlot* vs_row(Ui& ui, int row) {
    const SpriteSlot* rows = ui_slots(ui.data, "vs_mode", "vs_sprites_2");
    return rows && row >= 0 && row < 121 ? rows + row : nullptr;
}

void vs_row_draw(Ui& ui, int row, float scale_x, float scale_y, int anchor, std::uint32_t colour,
                 bool use_colour) {
    const SpriteSlot* slot = vs_row(ui, row);
    if (!slot || slot->sprite < 0 || scale_x <= 0 || scale_y <= 0) {
        return;
    }
    // SpriteRow::draw_anchored: kind 0 about the middle, 1..4 a corner held (bottom right,
    // bottom left, top right, top left); a centred row (flag 8) scales about its middle anyway
    Sprite sprite = sprite_of(ui, slot->sprite);
    const float w = static_cast<float>(sprite_width(ui, slot->sprite));
    const float h = static_cast<float>(sprite_height(ui, slot->sprite));
    float x = slot->x, y = slot->y;
    if (!(slot->flags & 8)) {
        const bool hold_right = anchor == 1 || anchor == 3;
        const bool hold_bottom = anchor == 1 || anchor == 2;
        const bool hold_left = anchor == 2 || anchor == 4;
        const bool hold_top = anchor == 3 || anchor == 4;
        x += hold_left ? 0 : hold_right ? (1 - scale_x) * w : (1 - scale_x) * w * 0.5f;
        y += hold_top ? 0 : hold_bottom ? (1 - scale_y) * h : (1 - scale_y) * h * 0.5f;
    }
    sprite.pos[0] = x;
    sprite.pos[1] = y;
    sprite.scale[0] = scale_x;
    sprite.scale[1] = scale_y;
    sprite.blend = slot->blend;
    sprite.color = use_colour ? colour : slot->color;
    sprite.z = slot->z;
    sprite.flags |= slot->flags;
    sprite_submit(ui, sprite);
}

int vs_stock_loadout(Ui& ui, int row) {
    return row >= 0 && row < vs_stock_count
               ? static_cast<int>(vs_number(ui, 0x1FFBB8 + 4u * static_cast<unsigned>(row)))
               : -1;
}

void vs_panel_anim(VsPanel& panel, int part, int anim) {
    // vs_panel_anim_start: the clock restarts only on a change; closing what is off stays off
    if (panel.anim[part] == anim || (anim == 3 && panel.anim[part] == 0)) {
        return;
    }
    panel.anim[part] = anim;
    panel.clock[part] = 0;
}

void vs_panel_idle(FrontEnd& fe, int page, int i) {
    VsPanel& p = fe.vs.panels[static_cast<std::size_t>(i)];
    const bool list = page == 0 ? panel_connected(fe, i) : fe.vs.entered[static_cast<std::size_t>(i)];
    // a part opening or open stays so (step11 opens the list only when it is neither): asked
    // every update, it restarted and the panels flickered
    const int open = list ? vs_list : vs_hint;
    if (p.anim[open] != 1 && p.anim[open] != 2) {
        vs_panel_anim(p, open, 1);
    }
    vs_panel_anim(p, list ? vs_hint : vs_list, 3);
}

void vs_panels_tick(VsSession& vs) {
    for (VsPanel& p : vs.panels) {
        for (int part = 0; part < 4; ++part) {
            if (p.anim[part] == 0 || p.anim[part] == 2) {
                continue;
            }
            p.clock[part] += 1;
            const float end = part == vs_frame ? 30.0f : 10.0f;
            if (p.clock[part] >= end) {
                p.anim[part] = p.anim[part] == 1 ? 2 : 0;
                p.clock[part] = 0;
            }
        }
    }
}

void vs_panels_stagger(FrontEnd& fe, int page, int clock) {
    for (int i = 0; i < vs_panel_count; ++i) {
        if (clock == 75 + 4 * i) {
            VsPanel& p = fe.vs.panels[static_cast<std::size_t>(i)];
            if (p.anim[vs_frame] == 0) {
                vs_panel_anim(p, vs_frame, 1);
            } else {
                vs_panel_idle(fe, page, i);
            }
        }
        // a frame done opening opens its contents (vs_panel_idle_anim at 30)
        VsPanel& p = fe.vs.panels[static_cast<std::size_t>(i)];
        if (p.anim[vs_frame] == 2 && p.anim[vs_list] == 0 && p.anim[vs_hint] == 0) {
            vs_panel_idle(fe, page, i);
        }
    }
}

void vs_panels_close_all(VsSession& vs, int clock) {
    for (int i = 0; i < vs_panel_count; ++i) {
        VsPanel& p = vs.panels[static_cast<std::size_t>(i)];
        if (clock == 4 * i) {
            vs_panel_anim(p, vs_list, 3);
            vs_panel_anim(p, vs_title, 3);
            vs_panel_anim(p, vs_hint, 3);
        }
        if (clock == 4 * (i + 4)) {
            vs_panel_anim(p, vs_frame, 3);
        }
    }
}

void vs_panels_close_contents(VsSession& vs, int clock) {
    for (int i = 0; i < vs_panel_count; ++i) {
        if (clock == 4 * i) {
            VsPanel& p = vs.panels[static_cast<std::size_t>(i)];
            vs_panel_anim(p, vs_list, 3);
            vs_panel_anim(p, vs_title, 3);
            vs_panel_anim(p, vs_hint, 3);
        }
    }
}

// decomp: vs_panels_draw 0x3FF49
void vs_panels_draw(FrontEnd& fe, Ui& ui, int page) {
    const World& world = *fe.world;
    for (int i = 0; i < vs_panel_count; ++i) {
        const VsPanel& p = fe.vs.panels[static_cast<std::size_t>(i)];
        const bool entered = fe.vs.entered[static_cast<std::size_t>(i)];
        // the frame grows out of the screen's middle, then the player tag rolls in
        const float c = p.clock[vs_frame];
        float frame = 0, tag = 0;
        if (p.anim[vs_frame] == 1) {
            frame = std::min(c * 0.05f, 1.0f);
            tag = std::clamp((c - 20.0f) / 10.0f, 0.0f, 1.0f);
        } else if (p.anim[vs_frame] == 2) {
            frame = tag = 1;
        } else if (p.anim[vs_frame] == 3) {
            tag = std::max(1.0f - c / 10.0f, 0.0f);
            frame = std::clamp(1.0f - (c - 10.0f) / 20.0f, 0.0f, 1.0f);
        }
        vs_row_draw(ui, 8 + i, frame, frame, i + 1, entered ? orange : 0xFFFFFFFF, true);
        vs_row_draw(ui, 12 + i, 1, tag);
        // the list: who can join and who has, or the machine list and record
        const float list = part_fill(p, vs_list);
        if (list > 0 && page == 0) {
            if (entered) {
                vs_row_draw(ui, 24 + i, 1, list);
                vs_row_draw(ui, 28 + i, 1, list);
                if (list >= 1.0f) {
                    device_draw(fe, ui, i);
                }
            } else {
                vs_row_draw(ui, 16 + i, 1, list);
            }
            vs_row_draw(ui, 20 + i, 1, list);
        } else if (list > 0 && page == 1 && entered) {
            vs_row_draw(ui, 36 + i, 1, list);
            vs_row_draw(ui, 40 + i, 1, list);
            vs_row_draw(ui, 44 + i, 1, list);
            float x = 0, y = 0;
            vs_pair_at(ui, 0x1FFB38, i, x, y);
            digits_draw(ui, world.vs.wins[static_cast<std::size_t>(i)], x, y, list);
            vs_pair_at(ui, 0x1FFB58, i, x, y);
            digits_draw(ui, world.vs.losses[static_cast<std::size_t>(i)], x, y, list);
            vs_machine_draw(fe, ui, i, list);
        }
        // the title: "I'm in" on the join page, the locked machine on the machine page
        const float title = part_fill(p, vs_title);
        if (title > 0) {
            vs_row_draw(ui, (page == 0 ? 32 : 56) + i, 1, title, i < 2 ? 1 : 3);
            if (page == 1) {
                vs_locked_draw(fe, ui, i, title);
            }
        }
        const float hint = part_fill(p, vs_hint);
        if (hint > 0) {
            vs_row_draw(ui, (page == 0 ? 60 : 68) + i, 1, hint);
        }
    }
}

