// hud_draw_readout (decomp docs/port/hud.md "The weapon readout", "Warnings
// and the durability signs", "Hit and kill popups", "Kills, score, day and clock"): the four
// mounts' frames, icons, counts and bars from hud_weapon_layouts by view count and side, the lock
// warning and durability signs, the popup lines and the counters in number font 3.
#include "hud/hud_internal.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr std::uint32_t frame_green = 0xFF32C88C;
constexpr std::uint32_t blink_blue = 0xFF8080E1;
constexpr std::uint32_t bar_green = 0xFF32FF8C;
constexpr std::uint32_t bar_red = 0xFFC04040;
constexpr std::uint32_t charge_yellow = 0xFFFFC823;
constexpr std::uint32_t counter_white = 0xFFF0F0F0;

struct MountLayout {
    const HudWeaponLayout* rows = nullptr;
    const std::array<int, 8>* digits = nullptr;
};

// mounts 0 and 2 are the right side
MountLayout mount_layout(const HudView& hud, int mount) {
    const bool right = mount == 0 || mount == 2;
    const HudData& d = hud.data;
    switch (hud_views(hud)) {
    case 1:
        return right ? MountLayout{&d.weapon[1], &d.digits_1view_right}
                     : MountLayout{&d.weapon[0], &d.digits_1view_left};
    case 2:
        return right ? MountLayout{&d.weapon[4], &d.digits_2view_right}
                     : MountLayout{&d.weapon[2], &d.digits_left};
    default:
        return right ? MountLayout{&d.weapon[3], &d.digits_4view_right}
                     : MountLayout{&d.weapon[2], &d.digits_left};
    }
}

// digits_split 0x6D3E0: `count` digits, most significant first, each clamped 0..9
// decomp: digits_split 0x6D3C5
void digits_split(int value, int count, int out[4]) {
    value = std::max(value, 0);
    int place = 1;
    for (int i = 1; i < count; ++i) {
        place *= 10;
    }
    for (int i = 0; i < count; ++i) {
        const int d = value / place;
        value -= d * place;
        out[i] = std::clamp(d, 0, 9);
        place /= 10;
    }
}

// where a mount's rows go (HudElem::draw_either): the 2D screen from the frame's top left in
// pixels, or with the cockpit set a quad on the cockpit's panels from the frame's top left in
// metres (y up): at (size x scale / 2 x scale_x + world_x) x 0.02 across and (size / 2 + world_y)
// x 0.02 down from it, turned onto its panel (down 1.3, world_yaw about y, -0.4 about x, 3 ahead)
// under the HUD matrix, the cell's size x 0.02
struct Placer {
    Ui& ui;
    HudCockpit* cockpit = nullptr; // the in-world side, null for 2D
    Mat4 base;                     // the HUD matrix (hud_draw_readout's)
    float ox = 0, oy = 0;

    void quad(const HudRow& r, float scale, float at_x, float at_y, std::uint32_t colour) const {
        const float w = static_cast<float>(sprite_width(ui, r.sprite)) * 0.02f;
        const float h = static_cast<float>(sprite_height(ui, r.sprite)) * 0.02f;
        Mat4 m = mat_scaling(w * scale * std::fabs(r.scale_x), h, 1);
        m = mat_multiply(m, mat_translation(at_x, at_y, 3.0f));
        m = mat_multiply(m, mat_rotation_x(-0.4f));
        m = mat_multiply(m, mat_rotation_y(r.world_yaw));
        m = mat_multiply(m, mat_translation(0, -1.3f, 0));
        cockpit->quads.push_back({mat_multiply(m, base), sprite_texture(ui, r.sprite), colour});
    }
    void row(const HudRow& r, float scale = 1, std::uint32_t colour = 0, bool use = false) const {
        if (!cockpit) {
            hud_row_draw(ui, r, scale, 1, ox, oy, colour, use);
            return;
        }
        if (r.sprite == -1 || scale == 0) {
            return;
        }
        const float w = static_cast<float>(sprite_width(ui, r.sprite));
        const float h = static_cast<float>(sprite_height(ui, r.sprite));
        quad(r, scale, (w * scale * 0.5f * r.scale_x + r.world_x) * 0.02f + ox,
             oy - (h * 0.5f + r.world_y) * 0.02f, use ? colour : r.color);
    }
};

// one mount's frame, icon, count or percent and bar; cockpit the in-world readout's quads (the
// cockpit set) or null for the 2D one
void draw_mount(HudView& hud, Ui& ui, const Fighter& f, const HudSlot& s, int mount,
                HudCockpit* cockpit, const Mat4& base) {
    const Weapon& w = f.combat.weapons[mount];
    if (!w.present || !w.record) {
        return; // no weapon id on the roster entry: nothing for the mount
    }
    const MountLayout layout = mount_layout(hud, mount);
    const HudWeaponLayout& rows = *layout.rows;
    const HudRow& frame = rows[static_cast<std::size_t>(mount / 2)];
    const float shown = s.resource_shown[mount];
    const float resource = fx_float(w.resource);
    const bool refilling = resource > shown;
    const std::uint32_t frame_colour =
        refilling ? color_blink(hud, 10, frame_green, blink_blue) : frame_green;
    // the frame slides out to its side during a view change: 100 px, or 3 m on the panels
    const std::size_t m = static_cast<std::size_t>(mount);
    Placer place{ui, cockpit, base};
    if (!cockpit) {
        const float slide = hud.data.view_dx[m] * s.view_switch_alpha;
        place.ox = frame.x + slide, place.oy = frame.y;
        hud_row_draw(ui, frame, 1, 1, slide, 0, frame_colour, true);
    } else if (frame.sprite != -1) {
        const float x = frame.world_x + hud.data.view_dworld[m] * s.view_switch_alpha;
        place.ox = x - static_cast<float>(sprite_width(ui, frame.sprite)) * 0.5f * 0.02f;
        place.oy = frame.world_y + static_cast<float>(sprite_height(ui, frame.sprite)) * 0.5f * 0.02f;
        place.quad(frame, 1, x, frame.world_y, frame_colour);
    }
    if (!w.fitted) {
        place.row(rows[row_broken]);
        return;
    }
    int icon = -1;
    bool count = false, percent = false;
    switch (w.record->behavior) {
    case 6:
        icon = row_icon_melee;
        break;
    case 0:
        icon = row_icon_gun;
        count = true;
        break;
    case 2:
        icon = row_icon_2;
        count = true;
        break;
    case 3:
        icon = row_icon_grenade;
        count = true;
        break;
    case 5:
        icon = row_icon_laser;
        percent = true;
        break;
    case 1:
        icon = w.turn_rate.raw != 0 ? row_icon_homing : row_icon_missile;
        count = true;
        break;
    default:
        break;
    }
    if (icon != -1) {
        place.row(rows[static_cast<std::size_t>(icon)], 1, frame_colour, true);
    }
    const bool one_view = hud_views(hud) == 1;
    const float fill = w.resource_max ? shown / static_cast<float>(w.resource_max) : 0.0f;
    const auto draw_digits = [&](int value, int n) {
        int digits[4] = {};
        digits_split(value, n, digits);
        for (int i = 0; i < n; ++i) {
            HudRow glyph = rows[static_cast<std::size_t>(row_digit0 + digits[i])];
            glyph.x = static_cast<float>((*layout.digits)[static_cast<std::size_t>(2 * i)]);
            glyph.y = static_cast<float>((*layout.digits)[static_cast<std::size_t>(2 * i + 1)]);
            glyph.world_x = glyph.x;
            glyph.world_y = glyph.y;
            place.row(glyph);
        }
    };
    if (count) {
        std::uint32_t bar = fill < 0.25f ? bar_red : bar_green;
        if (refilling) {
            bar = color_blink(hud, 10, bar, blink_blue);
        }
        if (one_view) {
            place.row(rows[row_ammo_bar], fill * 9.0f, bar, true);
        }
        draw_digits(static_cast<int>(shown), 4);
    } else if (percent) {
        // a laser: steady while reloading or full, blinking toward yellow while it charges
        std::uint32_t bar;
        if (w.reload_left > 0 || static_cast<float>(w.resource_max) == resource) {
            bar = fill < 0.25f ? bar_red : bar_green;
        } else {
            bar = color_blink(hud, 10, bar_green, charge_yellow);
        }
        draw_digits(static_cast<int>(fill * 100.0f), 3);
        place.row(rows[row_percent]);
        if (one_view) {
            place.row(rows[row_ammo_bar], fill * 9.0f, bar, true);
        }
    }
}

// the lock warning: someone's missile or melee mark is on this fighter (hud_locks_update's
// locking_me and melee_locking_me, read here from the world's marks)
bool locked_on_by_someone(const World& world, int slot) {
    for (int i = 0; i < fighter_slots; ++i) {
        const Fighter& other = world.fighters[static_cast<std::size_t>(i)];
        if (i == slot || !other.present || !other.combat.alive) {
            continue;
        }
        if (world.huds[static_cast<std::size_t>(slot)].locking_me[static_cast<std::size_t>(i)] ||
            other.combat.hud.warning.target == slot) {
            return true;
        }
    }
    return false;
}

void draw_signs(HudView& hud, Ui& ui, const World& world, const Fighter& f) {
    const HudLayout& rows = hud_layout(hud);
    // MISSILE ALERT pulses once a second: alpha x (0.5 + 0.5 x a 0..1..0 triangle)
    if (locked_on_by_someone(world, f.slot)) {
        const int pulse = static_cast<int>(hud.frame % 60);
        const float tri = pulse < 30 ? static_cast<float>(pulse) / 30.0f
                                     : 1.0f - static_cast<float>(pulse - 30) / 30.0f;
        hud_row_draw(ui, rows[row_lock_warning], 1, tri * 0.5f + 0.5f);
    }
    // durability down: the hazard stripes and the 75% (red) or 50% (yellow) sign; the pairs
    // alternate by the two-second clock but are the same rows, so the sign is steady. Retail shows
    // the 50% sign at exactly 0; the port shows the 75% sign there (docs/bugs.md)
    const float armour = static_cast<float>(f.combat.durability) /
                         static_cast<float>(std::max(f.combat.durability_max, 1));
    const bool first = hud.frame % 120 < 60;
    if (armour < 0.25f) {
        hud_row_draw(ui, rows[row_hazard]);
        hud_row_draw(ui, rows[first ? row_down_75 : row_down_75 + 1]);
    } else if (armour < 0.5f) {
        hud_row_draw(ui, rows[row_hazard]);
        hud_row_draw(ui, rows[first ? row_down_50 : row_down_50 + 1]);
    }
}

// the popup lines: entering (frames over 110) from 30 below and fading in, leaving (under 10) to
// 150 above and fading out, `shown_scroll` pushing them up as new ones come
void draw_popups(Ui& ui, const HudLayout& rows, const HudSlot& s, float alpha) {
    const float scroll = lerpf(s.shown_scroll_prev, s.shown_scroll, alpha);
    for (int i = 0; i < hud_popup_lines; ++i) {
        const HudPopup& line = s.shown[i];
        if (line.kind < 0 || line.kind > 2) {
            continue;
        }
        float y = static_cast<float>(i) * -15.0f + scroll;
        float a = 1.0f;
        if (line.frames > 110.0f) {
            const float t = (line.frames - 110.0f) / 10.0f;
            y = (30.0f - y) * t + y;
            a = 1.0f - t;
        } else if (line.frames < 10.0f) {
            const float t = (10.0f - line.frames) / 10.0f;
            y = (-150.0f - y) * t + y;
            a = 1.0f - t;
        }
        // the points (line.value) aren't drawn: retail works out their place and draws nothing
        // there (docs/bugs.md)
        const int row = line.kind == 0 ? row_popup_taken
                        : line.kind == 1 ? row_popup_dealt
                                         : row_popup_kill;
        hud_row_draw(ui, rows[static_cast<std::size_t>(row)], 1, a, 0, y);
    }
}

// kills, score, day and clock
void draw_counters(HudView& hud, Ui& ui, const World& world, const Fighter& f,
                   const HudSlot& s) {
    const HudLayout& rows = hud_layout(hud);
    const auto right_of = [&](int row) {
        return static_cast<int>(rows[static_cast<std::size_t>(row)].x +
                                static_cast<float>(sprite_width(ui, rows[static_cast<std::size_t>(row)].sprite)));
    };
    const auto y_of = [&](int row) { return static_cast<int>(rows[static_cast<std::size_t>(row)].y); };
    // SHOOTDOWN x, four digits with zeroes, left-aligned after the label
    hud_row_draw(ui, rows[row_kills]);
    hud_number(ui, number_zeros, f.combat.kills, right_of(row_kills), y_of(row_kills), 0x108,
               counter_white, 4, 1);
    // MONEY: the rolling score x 10 with commas, right-aligned 88 past the label
    hud_row_draw(ui, rows[row_money]);
    hud_number(ui, number_right | number_commas, s.score_shown, right_of(row_money) + 88,
               y_of(row_money), 0x108, counter_white, 9, 10);
    // day_draw_at: its own DAY- label (0xEA7) at the row's right edge, day + 1 two digits 57 on
    const int day_x = right_of(row_day);
    hud_sprite(ui, 0xea7, static_cast<float>(day_x), static_cast<float>(y_of(row_day)), 1, 1, 0,
               0x108, 0xFFFFFFFF, 0);
    hud_number(ui, number_right | number_zeros, world.story.day + 1, day_x + 57, y_of(row_day),
               0x108, 0xFFFFFFFF, 2, 1);
    // TIME: Battle.frame as hh:mm:ss after the label
    hud_row_draw(ui, rows[row_time]);
    const long long seconds = hud.frame / steps_per_second;
    int x = right_of(row_time) - 10;
    const int y = y_of(row_time);
    const int advance = hud_font_advance(ui, 0);
    const long long parts[3] = {seconds / 3600, seconds / 60 % 60, seconds % 60};
    for (int i = 0; i < 3; ++i) {
        hud_number(ui, number_zeros, parts[i], x, y, 0x108, 0xFFFFFFFF, 2, 1);
        x += advance * 2;
        if (i < 2) {
            hud_glyph(ui, 15, x, y, 0x108, 0xFFFFFFFF);
        }
    }
}

} // namespace

// decomp: hud_draw_readout 0x4ACBB
void hud_draw_readout(HudView& hud, Ui& ui, const World& world, const HudFrame& frame) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(hud.viewer)];
    const HudSlot& s = hud.slots[static_cast<std::size_t>(hud.viewer)];
    // the cockpit set draws the mounts in the world: under the identity view, 8 ahead lifted by
    // the bob, turned by the aim against the view (the port's angles are the original's negated)
    HudCockpit* cockpit = s.cockpit_set ? &hud.cockpit : nullptr;
    Mat4 base;
    if (cockpit) {
        cockpit->shown = true;
        const float turn = -static_cast<float>(angle_signed(Angle{
                               f.heading.raw + f.aim_yaw.raw - f.camera.view_yaw.raw})) *
                           (6.28318530718f / 4294967296.0f);
        base = mat_multiply(mat_rotation_y(turn),
                            mat_translation(0, s.bob * 0.2f - 0.1f, 8.0f));
    }
    for (int mount = 0; mount < 4; ++mount) {
        draw_mount(hud, ui, f, s, mount, cockpit, base);
    }
    draw_signs(hud, ui, world, f);
    if (hud_views(hud) > 1) {
        return;
    }
    draw_popups(ui, hud_layout(hud), s, frame.alpha);
    draw_counters(hud, ui, world, f, s);
}
