// Day, money, the garage banner, icon plates (decomp src/hand/u_0b1a50.c, src/hand/u_0bbb00.c,
// src/od/u_06c3e0.c).
#include "screens/ui/screen_header.hpp"

#include "world/world.hpp"

#include <cmath>
#include <map>

namespace {

// NumberText: the pen and what it draws
struct NumberPen {
    std::uint32_t flags = 0;
    long long value = 0;
    int x = 0, y = 0;
    int blend = 0;
    std::uint32_t color = 0;
    int digits = 0;
    int font = 0;
};

// mask 1 steps left first, 2 only measures
// decomp: glyph_draw_advance 0xBCCFD
int glyph_step(Ui& ui, const UiTable& fonts, NumberPen& pen, int glyph, int mask) {
    const std::size_t row = static_cast<std::size_t>(pen.font * 16 + glyph);
    int sprite_id = 0, advance = 0;
    if (row < fonts.pairs.size()) {
        sprite_id = fonts.pairs[row][0];
        advance = fonts.pairs[row][1];
    } else if (pen.font == 4 && glyph < 10) {
        // font 4 (the result's destruction count): its digits run on from 2282 with advance 28
        // in the words after the table (D_0027DC38, read as short pairs)
        sprite_id = 2282 + glyph;
        advance = 28;
    } else {
        return 0;
    }
    const int x0 = pen.x;
    if (mask & 1) {
        pen.x -= advance;
    }
    if (!(mask & 2)) {
        Sprite sprite = sprite_of(ui, sprite_id);
        sprite.pos[0] = static_cast<float>(pen.x);
        sprite.pos[1] = static_cast<float>(pen.y);
        sprite.blend = pen.blend;
        sprite.color = pen.color;
        sprite_submit(ui, sprite);
    }
    if (!(mask & 1)) {
        pen.x += advance;
    }
    return pen.x - x0;
}

// decomp: digit_count 0xBCDF8
int digit_count(long long v) {
    int n = 1;
    for (v = v < 0 ? -v : v; v >= 10; v /= 10) {
        ++n;
    }
    return n;
}

// decomp: number_digits_draw_grouped 0xBC9D2
int digits_grouped(Ui& ui, const UiTable& fonts, NumberPen& pen) {
    long long v = pen.value < 0 ? -pen.value : pen.value;
    int total = 0;
    const int mask = 1 | (pen.flags & 0x100 ? 2 : 0);
    for (int i = 0; i < pen.digits; ++i) {
        const int ch = pen.flags & 0x80 ? 9 : static_cast<int>(v % 10);
        v /= 10;
        if (v == 0 && ch == 0 && i != 0 && !(pen.flags & 0x10)) {
            break;
        }
        if ((pen.flags & 4) && i >= 3 && i % 3 == 0) {
            total += glyph_step(ui, fonts, pen, 12, mask);
        }
        total += glyph_step(ui, fonts, pen, ch, mask);
    }
    return total;
}

// trailing zeros hidden unless 0x20 (the last digit always)
// decomp: number_digits_draw_padded 0xBCBA9
int digits_padded(Ui& ui, const UiTable& fonts, NumberPen& pen) {
    long long v = pen.value;
    int total = 0;
    bool found = false;
    const int mask = 1 | (pen.flags & 0x100 ? 2 : 0);
    for (int i = 0; i < pen.digits; ++i) {
        const int ch = static_cast<int>(v % 10);
        v /= 10;
        bool show = true;
        if (ch == 0) {
            if (!found && !(pen.flags & 0x20) && i != pen.digits - 1) {
                show = false;
            }
        } else {
            found = true;
        }
        if (show) {
            total += glyph_step(ui, fonts, pen, ch, mask);
        }
    }
    return total;
}

// decomp: number_draw_pass 0xBC6E9
int number_pass(Ui& ui, const UiTable& fonts, std::uint32_t flags, long long value, float frac,
                int x, int y, int blend, std::uint32_t color, int digits, int decimals,
                int scale, int font) {
    int sgn = 0;
    if (value != 0 || frac != 0.0f) {
        sgn = 1;
        if (value < 0) {
            sgn = -1;
            value = -value;
        }
        if (frac < 0.0f) {
            sgn = -1;
            frac = -frac;
        }
    }
    NumberPen pen;
    pen.x = x;
    pen.y = y;
    pen.blend = blend;
    pen.color = color;
    pen.font = font;
    float f = 0.0f, whole = 0.0f;
    if (decimals > 0) {
        float ipart = 0.0f;
        f = std::modf(frac, &ipart);
        f = static_cast<float>(scale) * f;
        f = std::modf(f, &whole);
    }
    long long num = value * scale + static_cast<long long>(whole);
    if (digits < digit_count(num) && (flags & 0x40)) {
        flags |= 0x80;
    }
    int total = 0;
    if (decimals > 0) {
        const float power = std::pow(10.0f, static_cast<float>(decimals));
        const float r = std::floor(f * power + 0.5f);
        if (r >= power) {
            num += 1;
        }
        pen.value = static_cast<long long>(r);
        pen.flags = (flags | 0x10) & ~7u;
        pen.digits = decimals;
        total += digits_padded(ui, fonts, pen);
        total += glyph_step(ui, fonts, pen, 13, 1 | (flags & 0x100 ? 2 : 0));
    }
    pen.flags = flags;
    pen.value = num;
    pen.digits = digits;
    // retail drops what the decimals measured here: an aligned number with decimals is placed by
    // its whole part's width
    total = digits_grouped(ui, fonts, pen);
    const int mask = 1 | (flags & 0x100 ? 2 : 0);
    if (sgn > 0 && (flags & 2)) {
        total += glyph_step(ui, fonts, pen, 11, mask);
    } else if (sgn < 0 && (flags & 1)) {
        total += glyph_step(ui, fonts, pen, 10, mask);
    }
    return total;
}

} // namespace

// decomp: number_draw 0xBC652
int number_draw_full(Ui& ui, std::uint32_t flags, long long value, float frac, int x, int y,
                     int blend, std::uint32_t color, int digits, int decimals, int scale,
                     int font) {
    const UiTable* fonts = ui_table(ui.data, "font", "number_fonts");
    if (!fonts) {
        return 0;
    }
    int pass = 0;
    if (!(flags & 8)) {
        pass = number_pass(ui, *fonts, flags | 0x100, value, frac, x, y, blend, color, digits,
                           decimals, scale, font);
    }
    number_pass(ui, *fonts, flags, value, frac, x - pass, y, blend, color, digits, decimals, scale,
                font);
    return pass;
}

int number_draw_ex(Ui& ui, std::uint32_t flags, long long value, int x, int y, int blend,
                   std::uint32_t color, int digits, int scale, int font) {
    return number_draw_full(ui, flags, value, static_cast<float>(value), x, y, blend, color, digits,
                            0, scale, font);
}

// decomp: day_draw_at 0xB1B25
void day_draw_at(Ui& ui, int day, int x, int y, int blend) {
    // day_label_draw, then day_number_draw(8, day): + 1, flags 0x18, two digits, font 3
    Sprite label = sprite_of(ui, 0xea7);
    label.pos[0] = static_cast<float>(x);
    label.pos[1] = static_cast<float>(y);
    label.blend = blend;
    sprite_submit(ui, label);
    number_draw_ex(ui, 8 | 0x10, day + 1, x + 0x39, y, blend, 0xffffffff, 2, 1, 3);
}

// decomp: day_draw 0xB1AD6
void day_draw(Ui& ui, const World& world, int blend) {
    day_draw_at(ui, world.story.day, 0x33, 0x31, blend);
}

// decomp: money_amount_draw 0xB1CB7
void money_amount_draw(Ui& ui, long long amount, int x, int y, int blend, int digits,
                       std::uint32_t color) {
    // money_number_draw(8): flags 0xC, units of 10 NY times 10, font 3; then the NY mark
    number_draw_ex(ui, 8 | 4, amount, x, y, blend, color, digits, 10, 3);
    Sprite mark = sprite_of(ui, 0xeaa);
    mark.pos[0] = static_cast<float>(x + 1);
    mark.pos[1] = static_cast<float>(y);
    mark.blend = blend;
    sprite_submit(ui, mark);
}

// decomp: money_draw 0xB1C5D
void money_draw(Ui& ui, const World& world, int blend) {
    money_amount_draw(ui, world.story.money, 0xfd, 0x31, blend, 9, 0xfff0f0f0);
}

GlyphText glyph_name_text(const std::int16_t* name, int length) {
    GlyphText text;
    for (int i = 0; i < length && name[i] >= 0; ++i) {
        text.push_back(name[i]);
    }
    text.push_back(-1);
    return text;
}

// decomp: garage_name_banner_draw 0x6DA83
void garage_name_banner_draw(Ui& ui, const Garage& garage, int timer, int y, int blend) {
    if (timer == -1) {
        return;
    }
    const int ticks = 8 * (60 / ui.frame_cadence);
    unsigned elapsed = static_cast<unsigned>(static_cast<int>(ui.task_timer) - timer);
    if (elapsed > static_cast<unsigned>(ticks)) {
        elapsed = static_cast<unsigned>(ticks);
    }
    const int x = static_cast<int>(static_cast<float>(static_cast<unsigned>(ticks) - elapsed) /
                                   static_cast<float>(ticks) * -320.0f);
    Sprite plate = sprite_of(ui, 0xc08);
    plate.pos[0] = static_cast<float>(x);
    plate.pos[1] = static_cast<float>(y);
    plate.blend = blend;
    sprite_submit(ui, plate);
    text_draw(ui, glyph_name_text(garage.name, glyph_name_length), x + 0x6a, y + 5, blend - 0x100,
              18, 18, 0xfff0f0f0, 9);
}

// decomp: selected_garage_banner_draw 0x6DCF4
void garage_banner_draw(Ui& ui, const World& world, int timer, int y, int blend, bool plate) {
    const int selected = world.garages.selected;
    if (selected < 0 || selected >= garage_count) {
        return;
    }
    if (!plate) {
        garage_name_banner_draw(ui, world.garages.garages[selected], timer, y, blend);
        return;
    }
    // flag 1, the area select's plate 0x367: from x 0x172 + 688 in to 0x172 over 8 updates
    if (timer == -1) {
        return;
    }
    const int ticks = 8 * (60 / ui.frame_cadence);
    unsigned elapsed = ui.task_timer - static_cast<std::uint32_t>(timer);
    if (elapsed > static_cast<unsigned>(ticks)) {
        elapsed = static_cast<unsigned>(ticks);
    }
    const float left = static_cast<float>(ticks - static_cast<int>(elapsed)) /
                       static_cast<float>(ticks);
    const int x = static_cast<int>(left * 688.0f) + 0x172;
    Sprite s = sprite_of(ui, 0x367);
    s.pos[0] = static_cast<float>(x);
    s.pos[1] = static_cast<float>(y);
    s.blend = blend;
    sprite_submit(ui, s);
    text_draw(ui, glyph_name_text(world.garages.garages[selected].name, glyph_name_length),
              x + 0x4c, y + 8, blend - 0x100, 18, 18, 0xfff0f0f0, 9);
}

std::string text_string(Ui& ui, const std::string& id) {
    static std::map<std::string, std::string> strings;
    if (strings.empty()) {
        gsexp::ParseResult parsed;
        std::string error;
        if (ui_sexp(ui.data, "text/strings.sexp", parsed, error)) {
            for (std::size_t i = 0; i < parsed.root_count(); ++i) {
                const gsexp::Node form = parsed.root(i);
                if (form.is_list() && form.child_count() >= 3) {
                    strings[std::string(form.child_at(1).text())] =
                        std::string(form.child_at(2).text());
                }
            }
        }
    }
    const auto found = strings.find(id);
    return found == strings.end() ? std::string() : found->second;
}

std::string table_text(Ui& ui, const std::string& screen, const std::string& table, int index) {
    const UiTable* t = ui_table(ui.data, screen, table);
    return t && index >= 0 && index < static_cast<int>(t->strings.size())
               ? t->strings[static_cast<std::size_t>(index)]
               : std::string();
}

int table_int(Ui& ui, const std::string& screen, const std::string& table, int index,
              int fallback) {
    const UiTable* t = ui_table(ui.data, screen, table);
    return t && index >= 0 && index < static_cast<int>(t->values.size())
               ? static_cast<int>(t->values[static_cast<std::size_t>(index)])
               : fallback;
}

std::vector<SpriteSlot> ui_slot_run(Ui& ui, const std::string& screen, const std::string& name) {
    std::vector<SpriteSlot> out;
    const UiFile& file = ui_file(ui.data, screen);
    std::size_t i = 0;
    while (i < file.tables.size() && file.tables[i].name != name) {
        ++i;
    }
    unsigned next_va = 0;
    for (; i < file.tables.size(); ++i) {
        const UiTable& table = file.tables[i];
        if (table.layout != "SpriteSlot" || (next_va && table.va != next_va)) {
            break;
        }
        out.insert(out.end(), table.slots.begin(), table.slots.end());
        next_va = table.va + static_cast<unsigned>(0x1c * table.slots.size());
    }
    return out;
}

// decomp: SpriteRow::draw_offset 0xBBF9C
void sprite_row_offset(Ui& ui, SpriteSlot row, float dx, float dy) {
    row.x += dx;
    row.y += dy;
    sprite_row_draw(ui, row);
}

int pointer_hover(const Ui& ui, const PointerRect* rects, int count) {
    if (!ui.pointer.moved && !ui.pointer.left) {
        return -1;
    }
    return pointer_pick(ui.pointer, rects, count);
}

// decomp: icon_on_plate_draw 0x6DE53
void icon_on_plate_draw(Ui& ui, int x, int y, int blend, int icon, int dx) {
    Sprite plate = sprite_of(ui, 0xc32);
    plate.pos[0] = static_cast<float>(x);
    plate.pos[1] = static_cast<float>(y);
    plate.blend = blend;
    sprite_submit(ui, plate);
    Sprite mark = sprite_of(ui, icon);
    mark.pos[0] = static_cast<float>(x + dx);
    mark.pos[1] = static_cast<float>(y + 2);
    mark.blend = blend;
    sprite_submit(ui, mark);
}
