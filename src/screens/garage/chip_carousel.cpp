// The Chip carousel and the Uplift card (decomp src/o1/u_02edc5.c plus_tech_chip_list_draw,
// plus_tech_chip_card_draw, plus_tech_screen_step10).
#include "screens/garage/chip_carousel.hpp"

#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"

#include <array>
#include <cmath>

namespace {

constexpr float clip_left = 104, clip_top = 110, clip_right = 536, clip_bottom = 302;
// reticle_box_size's rows (0x203B68 the tracking box, hud_lock_box_ranges the lock
// box): per kind the half width at size 0 and 999, then the half height
constexpr float track_rows[3][4] = {{40, 120, 40, 120}, {60, 180, 20, 60}, {20, 60, 60, 180}};
constexpr float lock_rows[3][4] = {{70, 160, 70, 160}, {135, 210, 45, 70}, {45, 70, 135, 210}};
// D_001DBB80 / D_001DBB8C: the kinds' icons
constexpr int track_icons[3] = {3663, 3661, 3665};

// the box's corner in -1..1 screen units
// decomp: reticle_box_size 0x4DAE9
void reticle_box(int kind, float size, bool lock, float out[2]) {
    const float t = size / 999.0f;
    const float* row = (lock ? lock_rows : track_rows)[kind >= 0 && kind < 3 ? kind : 0];
    out[0] = 320.0f - ((row[1] - row[0]) * t + row[0]);
    out[1] = 240.0f - ((row[3] - row[2]) * t + row[2]);
    out[0] = out[0] / 640.0f * 2.0f - 1.0f;
    out[1] = -(out[1] / 480.0f * 2.0f) + 1.0f;
}

void card_sprite(Ui& ui, int id, float x, float y, float scale, std::uint32_t colour, int blend) {
    Sprite s = sprite_of(ui, id);
    s.pos[0] = x;
    s.pos[1] = y;
    s.scale[0] = s.scale[1] = scale;
    s.color = colour;
    s.blend = blend;
    s.flags |= 8;
    sprite_submit_clipped(ui, s, clip_left, clip_top, clip_right, clip_bottom);
}

} // namespace

void carousel_draw(Ui& ui, const PartTables& t, const CarouselLook& look, ChipStrip& c,
                   const std::vector<CarouselCard>& cards, bool input, bool pulse) {
    const int count = static_cast<int>(cards.size());
    if (input) {
        sprite_draw_at(ui, look.leave, static_cast<float>(look.leave_x),
                       static_cast<float>(look.leave_y), 3);
        if (c.on_leave) {
            sprite_draw_at(ui, look.leave_lit, static_cast<float>(look.leave_x),
                           static_cast<float>(look.leave_y), 4, 0xffffffff, 0x2000);
        }
    }
    if (count == 0) {
        return;
    }
    const bool dim = c.on_leave || !input;
    // the frame pops open with a wobble
    float pop[2];
    box_pop_scale(pop, 8, 16, c.wobble);
    if (c.wobble < 16) {
        ++c.wobble;
    }
    Sprite frame = sprite_of(ui, look.frame);
    frame.pos[0] = 326;
    frame.pos[1] = 218;
    frame.scale[0] = pop[0];
    frame.scale[1] = pop[1];
    frame.flags |= 8;
    frame.blend = 2;
    frame.color = dim ? 0xff646464 : 0xffffffff;
    sprite_submit(ui, frame);
    if (c.wobble < 16) {
        return;
    }
    sprite_draw_at(ui, look.black, static_cast<float>(look.black_x), static_cast<float>(look.black_y),
                   3, 0xff000000);
    sprite_draw_at(ui, look.deco_a, 205, 128, 3);
    sprite_draw_at(ui, look.deco_b, 412, 128, 3);
    Sprite belt = sprite_of(ui, look.belt);
    belt.pos[0] = 106;
    belt.pos[1] = 169.0f + static_cast<float>(c.belt);
    belt.blend = 3;
    sprite_submit_clipped(ui, belt, 106, 169, 536, 276);
    if (++c.belt > 107) {
        c.belt = -48;
    }
    const bool still = c.slide >= 8;
    if (still && !c.on_leave) {
        sprite_draw_at(ui, look.highlight, 321, 211, 0x102, 0xffffffff, pulse ? 0x2008 : 8);
        if (look.ring || c.cursor > 0) {
            sprite_draw_at(ui, look.arrow_left, 194, 117, 6);
        }
        if (look.ring || c.cursor < count - 1) {
            sprite_draw_at(ui, look.arrow_right, 400, 117, 6);
        }
    }
    // the cards: the centre one full size, the sides smaller, lower and darker, sliding
    const int lo = look.ring ? -3 : c.shown - 3, hi = look.ring ? 3 : c.shown + 2;
    for (int k = lo; k <= hi; ++k) {
        const int i = look.ring ? c.shown + k : k;
        const int index = look.ring ? ((i % count) + count) % count : i;
        if (index < 0 || index >= count) {
            continue;
        }
        const int rel = i - c.shown;
        const int a = rel < 0 ? -rel : rel;
        const float x = 321.0f + static_cast<float>(look.spacing * rel) +
                        static_cast<float>(c.slide * c.dir * look.spacing) / 8.0f;
        const int target = c.shown + (c.dir < 0 ? 1 : c.dir > 0 ? -1 : 0);
        float drop = 12.0f, scale = 0.65f;
        if (still) {
            drop = rel == 0 ? 0.0f : 12.0f;
            scale = rel == 0 ? 1.0f : 0.65f;
        } else if (i == target) {
            drop = 12.0f - 12.0f * static_cast<float>(c.slide) / 8.0f;
            scale = 0.65f + 0.35f / 8.0f * static_cast<float>(c.slide);
        } else if (rel == 0) {
            drop = 12.0f * static_cast<float>(c.slide) / 8.0f;
            scale = 1.0f - 0.35f / 8.0f * static_cast<float>(c.slide);
        }
        const int flash = a < 4 ? c.flash[a] : 10;
        const int alpha = std::max(0, static_cast<int>(255.0f - static_cast<float>(flash) * 25.5f));
        int rgb = 0xff - 0x3c * a;
        if (dim) {
            rgb /= 2;
        }
        rgb = std::max(0, rgb);
        const std::uint32_t colour = static_cast<std::uint32_t>(alpha) << 24 |
                                     static_cast<std::uint32_t>(rgb) << 16 |
                                     static_cast<std::uint32_t>(rgb) << 8 |
                                     static_cast<std::uint32_t>(rgb);
        card_sprite(ui, look.plate, x, 211.0f + drop, scale, colour, 0x101);
        const ChipPart* chip = chip_part(t, cards[static_cast<std::size_t>(index)].part);
        Sprite face = sprite_of(ui, chip ? identity_sprite(t, chip->identity) : -1);
        face.pos[0] = x;
        face.pos[1] = 223.0f + drop;
        face.color = colour;
        face.blend = 0x102;
        face.flags |= 8;
        sprite_submit_clipped(ui, face, clip_left, clip_top, clip_right, clip_bottom);
    }
    // the centre card's name, price, level and weight
    if (still) {
        const CarouselCard& card = cards[static_cast<std::size_t>(c.shown % count)];
        text_draw(ui, card.name, 192, 322, 0, 18, 18, 0xfff0f0f0, 9);
        money_amount_draw(ui, card.price, 299, 351, 0x101, 9, 0xfff0f0f0);
        if (!c.on_leave) {
            const ChipPart* chip = chip_part(t, card.part);
            sprite_draw_at(ui, 0xa96, 292, 153, 0x103);
            number_draw_full(ui, 8, card.tune, static_cast<float>(card.tune), 333, 171, 0x103,
                             0xff000000, 3, 0, 1, 3);
            sprite_draw_at(ui, 0xab0, 267, 264, 0x103);
            const float weight = chip ? curve_value(chip->weight, card.tune) * 0.01f : 0.0f;
            number_draw_full(ui, 0x28, static_cast<long long>(weight), weight, 364, 263, 0x103,
                             0xff000000, 1, 2, 1, 3);
            sprite_draw_at(ui, 0xaad, 367, 262, 0x103);
        }
    }
    // the card's number and the count
    if (c.on_leave) {
        sprite_draw_at(ui, 0xea4, 461, 285, 0x101);
        sprite_draw_at(ui, 0xea4, 471, 285, 0x101);
    } else {
        number_draw_full(ui, 8, c.cursor + 1, static_cast<float>(c.cursor + 1), 483, 285, 0x101,
                         0xffffffff, 2, 0, 1, 3);
    }
    sprite_draw_at(ui, 0xea6, 483, 285, 0x101);
    number_draw_full(ui, 8, count, static_cast<float>(count), 527, 285, 0x101, 0xffffffff, 2, 0, 1,
                     3);
    // the cards fade in, the centre first
    for (int& flash : c.flash) {
        if (flash > 0) {
            --flash;
            break;
        }
    }
}

int carousel_input(Ui& ui, ChipStrip& c, int count, bool ring) {
    if (c.slide < 8) {
        menu_input_mark(ui);
        if (++c.slide >= 8) {
            c.shown = c.cursor;
        }
        return 0;
    }
    if (c.flash[3] != 0 || count == 0) {
        return 0;
    }
    int step = 0;
    if (!c.on_leave) {
        const int before = c.cursor;
        step = menu_cursor(ui, c.cursor, 0, count, ring ? 0x17 : 0x13, 0, 4);
        // the pointer: a click on a side card steps towards it
        if (step == 0 && ui.pointer.left && ui.pointer.y > 136 && ui.pointer.y < 290) {
            const int to = ui.pointer.x < 246 ? -1 : ui.pointer.x > 396 ? 1 : 0;
            if (to != 0 && (ring || (c.cursor + to >= 0 && c.cursor + to < count))) {
                c.cursor = ((c.cursor + to) % count + count) % count;
                step = to;
                ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
                menu_input_mark(ui);
                ui.pointer.left = false;
            }
        }
        if (step != 0) {
            c.slide = 0;
            // the wrap from last to first keeps the strip's direction
            c.dir = step > 0 ? -1 : 1;
            (void)before;
        }
    }
    int leave = c.on_leave ? 1 : 0;
    if (menu_cursor(ui, leave, 0, 2, 0x12, 0, 3)) {
        c.on_leave = leave != 0;
        return 2;
    }
    return step;
}

void uplift_card_draw(Ui& ui, const PartTables& t, const ChipStrip& c, int part, int tune_left,
                      int tune_from, int tune_to, int cost, int money, bool selected) {
    const ChipPart* chip = chip_part(t, part);
    if (!chip) {
        return;
    }
    const int limits[3] = {10, 10, 6};
    const float off = -179.0f * static_cast<float>(c.card_frames[0]) / static_cast<float>(limits[0]);
    const float left = 321.0f + off;
    const float ratio = static_cast<float>(c.card_frames[1]) / static_cast<float>(limits[1]);
    const float width = (405.0f - left) * ratio;
    float pop[2];
    pop_scale_peak(pop, limits[2] / 2, limits[2], c.card_frames[2], 1.0f, 0.05f);
    // the left side: the Chip as it is
    sprite_draw_at(ui, 0xaa1, left, 211, 0x102, 0xffffffff, 8);
    sprite_draw_at(ui, identity_sprite(t, chip->identity), left, 223, 0x103, 0xffffffff, 8);
    sprite_draw_at(ui, 0xa96, off + 292.0f, 153, 0x103);
    const int ioff = static_cast<int>(off);
    number_draw_full(ui, 8, tune_left, static_cast<float>(tune_left), ioff + 0x14d, 0xab, 0x103,
                     0xff000000, 3, 0, 1, 3);
    sprite_draw_at(ui, 0xab0, off + 270.0f, 264, 0x103);
    const float weight = curve_value(chip->weight, tune_left) * 0.01f;
    number_draw_full(ui, 0x28, static_cast<long long>(weight), weight, ioff + 0x16c, 0x107, 0x103,
                     0xff000000, 1, 2, 1, 3);
    sprite_draw_at(ui, 0xaad, off + 367.0f, 262, 0x103);
    // the connector and the panels
    Sprite link = sprite_of(ui, 0xaa9);
    link.pos[0] = width + left;
    link.pos[1] = 223;
    link.scale[0] = link.scale[1] = ratio;
    link.blend = c.card_frames[1] < limits[1] ? 0x104 : 0x102;
    sprite_submit(ui, link);
    for (const auto& panel : {std::array<float, 3>{0xaaa, 311, 218}, {0xaab, 492, 224},
                              {0xa99, 403, 329}}) {
        Sprite s = sprite_of(ui, static_cast<int>(panel[0]));
        s.pos[0] = panel[1];
        s.pos[1] = panel[2];
        s.scale[0] = pop[0];
        s.scale[1] = pop[1];
        s.flags |= 8;
        s.blend = 0x103;
        sprite_submit(ui, s);
    }
    if (c.card_frames[2] != limits[2]) {
        return;
    }
    // the right side at the new level: the two reticle boxes, crit and shield
    sprite_draw_at(ui, 0xab1, 392, 144, 0x104);
    sprite_draw_at(ui, track_icons[chip->reticle_kind_a % 3], 406, 173, 0x104);
    sprite_draw_at(ui, track_icons[chip->reticle_kind_b % 3], 406, 208, 0x104);
    for (int box = 0; box < 2; ++box) {
        float r[2];
        reticle_box(box ? chip->reticle_kind_b : chip->reticle_kind_a,
                    curve_value(box ? chip->reticle_size_b : chip->reticle_size_a, tune_to),
                    box != 0, r);
        const float px[2] = {std::floor(std::fabs(r[0]) * 640.0f + 0.5f),
                             std::floor(std::fabs(r[1]) * 480.0f + 0.5f)};
        const float dx = std::fabs(r[0] * 80.0f), dy = std::fabs(r[1] * 60.0f);
        const int corners[2][4] = {{0xe45, 0xe47, 0xe44, 0xe46}, {0xe57, 0xe59, 0xe56, 0xe58}};
        sprite_draw_at(ui, corners[box][0], 310.0f - dx, 217.0f - dy, 0x104, 0xffffffff, 8);
        sprite_draw_at(ui, corners[box][1], 310.0f + dx, 217.0f - dy, 0x104, 0xffffffff, 8);
        sprite_draw_at(ui, corners[box][2], 310.0f - dx, 217.0f + dy, 0x104, 0xffffffff, 8);
        sprite_draw_at(ui, corners[box][3], 310.0f + dx, 217.0f + dy, 0x104, 0xffffffff, 8);
        const int y = box ? 0xd2 : 0xaf;
        number_draw_full(ui, 8, static_cast<long long>(px[0]), px[0], 0x209, y, 0x104, 0xffffffff,
                         3, 0, 1, 3);
        number_draw_full(ui, 8, static_cast<long long>(px[1]), px[1], 0x239, y, 0x104, 0xffffffff,
                         3, 0, 1, 3);
        Sprite centre = sprite_of(ui, box ? 0xe61 : 0xe60);
        centre.pos[0] = 310;
        centre.pos[1] = 217;
        centre.scale[0] = dx * 0.25f;
        centre.scale[1] = dy * 0.25f;
        centre.flags |= 8;
        centre.blend = 0x104;
        sprite_submit(ui, centre);
    }
    const float crit = curve_value(chip->crit, tune_to);
    const float shield = curve_value(chip->shield, tune_to);
    number_draw_full(ui, 8, static_cast<long long>(crit), crit, 0x22b, 242, 0x104, 0xffffffff, 3,
                     0, 1, 3);
    number_draw_full(ui, 8, static_cast<long long>(shield), shield, 0x22b, 264, 0x104, 0xffffffff,
                     3, 0, 1, 3);
    // the gauge, the levels and the cost
    Sprite gauge = sprite_of(ui, 0xa97);
    gauge.pos[0] = 235;
    gauge.pos[1] = 347;
    gauge.scale[0] = static_cast<float>(tune_to) * 2.0f;
    gauge.blend = 0x104;
    sprite_submit(ui, gauge);
    number_draw_full(ui, 0x18, tune_from, static_cast<float>(tune_from), 0x137, 0x13b, 0x104,
                     0xffffffff, 2, 0, 1, 3);
    number_draw_full(ui, 0x18, tune_to, static_cast<float>(tune_to), 0x16c, 0x13b, 0x104,
                     0xffffffff, 2, 0, 1, 3);
    money_amount_draw(ui, cost, 0x229, 0x13b, 0x104, 9,
                      static_cast<unsigned>(money) < static_cast<unsigned>(cost) ? 0xffff0000
                                                                                 : 0xfff0f0f0);
    if (selected) {
        icon_on_plate_draw(ui, 0x1ea, 0x155, 0x104, 0xc3d, 0x1a);
    }
}
