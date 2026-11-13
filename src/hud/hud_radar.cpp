// The radar round the reticle (hud_draw_radar, decomp docs/port/hud.md "The radar"): the
// off-screen arrows on a 96 px ring, the name tags (hud_fighter_tag_draw), the ring and
// the four cloak arrows, lit when a camouflaged fighter is on screen within 75: the camouflage
// detector. The ring's 640x480 offsets scale with the view (W/640, H/480).
#include "hud/hud_internal.hpp"

#include "ai/ai_links.hpp"
#include "screens/ui/screen_text.hpp"

#include <cmath>

namespace {

constexpr float half_turn = 1.5707964f;
constexpr std::uint32_t reticle_flags = 0x1C;
// the off-screen arrows and the cloak detector reach (hud_draw_radar's 75.0f), kept
constexpr float radar_range = 75.0f;
constexpr float ring_radius = 96.0f;
// the radar's sprites (game_tex)
constexpr int sprite_ring = 0x570;        // the reticle's ring, 160 x 160
constexpr int sprite_arrow = 0x571;       // a solid arrow, coloured by distance or the cloak
constexpr int sprite_arrow_frame = 0x573; // its outline, always white
constexpr int sprite_tag_skull = 0x567;   // a beaten opponent's skull before its name
constexpr int sprite_tag_1p = 0x5B8;      // 1P, 2P, 3P, 4P in turn (game_vs_tex)
constexpr int sprite_tag_plate = 0x560;   // the name tag's frame
constexpr int sprite_tag_bar = 0x56A;     // its durability bar, scaled by what is left

std::uint32_t distance_colour(std::uint32_t base, float distance) {
    const int green = static_cast<int>(230.0f * distance / radar_range);
    return base | static_cast<std::uint32_t>(green & 0xFF) << 8;
}

} // namespace

// decomp: hud_draw_radar 0x50339
void hud_draw_radar(HudView& hud, Ui& ui, const World& world, const HudFrame& frame) {
    const int me = hud.viewer;
    const Fighter& f = world.fighters[static_cast<std::size_t>(me)];
    const FighterHud& senses = world.huds[static_cast<std::size_t>(me)];
    const HudSlot& s = hud.slots[static_cast<std::size_t>(me)];
    const float one = frame.sprite_scale;
    // radar_center: the reticle's point in pixels
    const float cu = lerpf(s.center[0][0], s.center[1][0], frame.alpha);
    const float cv = lerpf(s.center[0][1], s.center[1][1], frame.alpha);
    const float cx = hud_px(frame, cu), cy = hud_py(frame, cv);

    // off-screen arrows, from the last visible to the first: alive, off screen, near and ahead.
    // The angle is the other's offset in the aim frame, sideways and up (port frame: the
    // screen's right is (-cos a, 0, sin a) of the aim's heading a); cloaked fighters get one too
    const float aim = fx_float(angle_radians(f.heading + f.aim_yaw));
    const float right_x = -std::cos(aim), right_z = std::sin(aim);
    for (int k = senses.visible_count - 1; k >= 0; --k) {
        const int other = senses.visible[static_cast<std::size_t>(k)];
        const HudSense& sense = senses.sense[static_cast<std::size_t>(other)];
        const float distance = fx_float(sense.distance);
        if (!fighter_alive(world, other) || sense.on_screen || distance >= radar_range ||
            std::fabs(fx_float(sense.yaw)) >= half_turn) {
            continue;
        }
        const FxVec3 d = world.fighters[static_cast<std::size_t>(other)].combat.aim_point -
                         f.combat.aim_point;
        const float side = fx_float(d.x) * right_x + fx_float(d.z) * right_z;
        const float angle = std::atan2(-fx_float(d.y), side);
        const float x = cx + std::cos(angle) * ring_radius * frame.scale_x;
        const float y = cy + std::sin(angle) * ring_radius * frame.scale_y;
        hud_sprite(ui, sprite_arrow, x, y, one, one, angle - half_turn, 0x101,
                   distance_colour(0x80C80000, distance), reticle_flags);
        hud_sprite(ui, sprite_arrow_frame, x, y, one, one, angle - half_turn, 0x101, 0x80FFFFFF,
                   reticle_flags);
    }

    // name tags: every running other fighter in slot order, teammates too, alive, on screen,
    // near and uncloaked, through walls
    for (int i = 0; i < fighter_slots; ++i) {
        const HudSense& sense = senses.sense[static_cast<std::size_t>(i)];
        if (i == me || !fighter_alive(world, i) || !sense.on_screen ||
            fx_float(sense.distance) >= radar_range || camo_hides(world, i)) {
            continue;
        }
        const float tu = lerpf(s.sense[0][i][0], s.sense[1][i][0], frame.alpha);
        const float tv = lerpf(s.sense[0][i][1], s.sense[1][i][1], frame.alpha);
        hud_fighter_tag_draw(hud, ui, world, me, i, hud_px(frame, tu), hud_py(frame, tv));
    }

    // the ring
    hud_sprite(ui, sprite_ring, cx, cy, one, one, 0, 0x101, 0x4CFFFFFF, reticle_flags);

    // the cloak arrows: lit by the first visible fighter in camouflage that is alive, on screen
    // and within 75 (retail reads sense[-1] for the colour when there is none; unused then)
    int cloaked = -1;
    for (int k = senses.visible_count - 1; k >= 0; --k) {
        const int other = senses.visible[static_cast<std::size_t>(k)];
        const HudSense& sense = senses.sense[static_cast<std::size_t>(other)];
        if (fighter_alive(world, other) && sense.on_screen &&
            fx_float(sense.distance) < radar_range && camo_hides(world, other)) {
            cloaked = other;
        }
    }
    const std::uint32_t lit =
        cloaked >= 0 ? distance_colour(0xCCC80000,
                                       fx_float(senses.sense[static_cast<std::size_t>(cloaked)]
                                                    .distance))
                     : 0;
    for (const auto& arrow : hud.data.cloak_arrows) {
        const float x = cx + arrow[0] * frame.scale_x;
        const float y = cy + arrow[1] * frame.scale_y;
        if (cloaked >= 0) {
            hud_sprite(ui, sprite_arrow, x, y, one, one, arrow[2], 0x101, lit, reticle_flags);
        }
        hud_sprite(ui, sprite_arrow_frame, x, y, one, one, arrow[2], 0x101, 0xCCFFFFFF,
                   reticle_flags);
    }
}

// decomp: hud_fighter_tag_draw 0x4DDD0
void hud_fighter_tag_draw(HudView& hud, Ui& ui, const World& world, int viewer, int fighter,
                          float x, float y) {
    (void)viewer;
    const Fighter& f = world.fighters[static_cast<std::size_t>(fighter)];
    const RosterEntry& entry = battle_roster_entry(world, fighter);
    const float fill = static_cast<float>(f.combat.durability) /
                       static_cast<float>(std::max(f.combat.durability_max, 1));
    // the colour by the roster's role: the fixed opponent red, a pool's primary blue, anyone else
    // yellow, green when drawn from the reserve queue
    std::uint32_t colour = 0xFFFFFF00;
    switch (entry.ticket.progress.role) {
    case 0:
        colour = entry.queue != 3 ? 0xFFFFFF00 : 0xFF00E100;
        break;
    case 1:
        colour = 0xFF8080FF;
        break;
    case 2:
        colour = 0xFFFF8080;
        break;
    default:
        break;
    }
    const int identity = entry.pilot.identity;
    if (identity >= 0) {
        // the pilot's Latin name in 12 px glyphs, roughly centred (under 15 full-width glyphs);
        // a beaten opponent's skull before it (retail places the skull from an unset length when
        // there is no name; the port leaves it out then)
        const std::string& name =
            identity < static_cast<int>(hud.data.identity_name_en.size())
                ? hud.data.identity_name_en[static_cast<std::size_t>(identity)]
                : std::string();
        const GlyphText glyphs = utf8_to_glyphs(ui.data, name);
        if (!name.empty() && glyphs.size() < 15) {
            const int len = (static_cast<int>(glyphs.size()) + 1) / 2;
            text_draw(ui, glyphs, static_cast<int>(x - 72.0f + 37.0f - static_cast<float>(len * 12)),
                      static_cast<int>(y - 18.0f - 12.0f), 1, 12, 12, colour, 15);
            if (entry.queue == 2) {
                hud_sprite(ui, sprite_tag_skull,
                           x - 72.0f + 42.0f - static_cast<float>(len * 12) - 32.0f, y - 30.0f, 1,
                           1, 0, 0x103, 0xFFFFFFFF, 0);
            }
        }
    } else if (f.controller != -1 && hud.scenes.vs_mode) {
        // a VS player without a pilot: 1P..4P
        hud_sprite(ui, sprite_tag_1p + f.controller, x - 54.0f, y - 34.0f, 1, 1, 0, 0x103,
                   0xFFFFFFFF, 0);
    }
    hud_sprite(ui, sprite_tag_plate, x - 72.0f, y - 18.0f, 1, 1, 0, 0x101, colour, 0);
    hud_sprite(ui, sprite_tag_bar, x - 66.0f, y - 13.0f, fill, 1, 0, 0x101, colour, 1);
}
