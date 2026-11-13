// The scenes' 2D over a battle (decomp docs/port/hud.md "READY, GO, the letterbox, the death
// card, the VS end menu", "VS marks"; battle.md "Split-screen VS"): READY / GO from a human's
// intro (scene_intro_update), the letterbox (battle_letterbox_draw), the death
// card (scene_player_death_update), the VS marks (hud_update) and the end menu and empty
// quarter (battle_update). The battle owns the scenes and their states; the HUD keeps the timers
// that only time pictures and draws.
#include "audio/sound_cues.hpp"
#include "hud/hud_internal.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr int ready_sprite = 0xe80;
constexpr int go_sprite = 0xe7f;
constexpr int crasher_sprite = 0xe8c;
constexpr int letterbox_sprite = 0xe7c;
// the card shows while the death scene's timer is over 330 of its 510 ticks
constexpr int death_card_ticks = 510 - 330;
// the end menu's rows come in 6 ticks apart, 30 in all
constexpr float menu_row_ticks = 6.0f;

void centred(Ui& ui, int id, float x, float y, int blend) {
    Sprite sprite = sprite_of(ui, id);
    sprite.pos[0] = x;
    sprite.pos[1] = y;
    sprite.blend = blend;
    sprite.flags |= 8;
    sprite_submit(ui, sprite);
}

// the end menu's chosen row: |frame % 60 / 60 - 0.5| + 0.5 of 0x14FFF, less 0x15000, which
// borrows into the alpha and gives white at the ends and a yellow in the middle
std::uint32_t menu_pulse(std::int64_t frame) {
    const float pulse = std::fabs(static_cast<float>(frame % 60) / 60.0f - 0.5f) + 0.5f;
    const std::uint32_t base = 0x14FFF;
    std::uint32_t colour = 0;
    colour |= static_cast<std::uint32_t>(static_cast<float>((base >> 16) & 0xFF) * pulse) << 16;
    colour |= static_cast<std::uint32_t>(static_cast<float>((base >> 8) & 0xFF) * pulse) << 8;
    colour |= static_cast<std::uint32_t>(static_cast<float>(base & 0xFF) * pulse);
    return colour - 0x15000u;
}

// SpriteRow::draw_anchored(1, fill, 0): the row grown from its middle to `fill` of its height
// decomp: SpriteRow::draw_anchored 0xBBE73
void row_grown(Ui& ui, const SpriteSlot& row, float fill, std::uint32_t colour) {
    Sprite sprite = sprite_of(ui, row.sprite);
    const float h = static_cast<float>(sprite_height(ui, row.sprite));
    sprite.pos[0] = row.x;
    sprite.pos[1] = (1.0f - fill) * h * 0.5f + row.y;
    sprite.scale[1] = fill;
    sprite.blend = row.blend;
    sprite.color = colour;
    sprite.z = row.z;
    sprite.flags |= row.flags;
    if (fill > 0) {
        sprite_submit(ui, sprite);
    }
}

void draw_vs_menu(HudView& hud, Ui& ui, const World& world) {
    const HudScenes& sc = hud.scenes;
    const std::vector<SpriteSlot>& rows = hud.data.battle_sprites;
    if (rows.size() < 6) {
        return;
    }
    // a quarter no pad has (three players): the empty picture over it
    if (hud_views(hud) != 2 && hud.view_count > 1) {
        unsigned humans = 0;
        for (int i = 0; i < fighter_slots; ++i) {
            const int player = battle_roster_entry(world, i).player;
            if (world.fighters[static_cast<std::size_t>(i)].present && player >= 0) {
                humans |= 1u << player;
            }
        }
        for (int j = 0; j < 4; ++j) {
            if (!(humans & (1u << j))) {
                SpriteSlot empty = rows[5];
                empty.x = hud.data.vs_empty_view[static_cast<std::size_t>(j)][0];
                empty.y = hud.data.vs_empty_view[static_cast<std::size_t>(j)][1];
                sprite_row_draw(ui, empty);
            }
        }
    }
    if (sc.vs_end_state != 2 && sc.vs_end_state != 3) {
        return;
    }
    // the quit box over the menu, drawn from a copy so drawing doesn't grow it
    if (sc.vs_end_state == 2 && sc.vs_quit_open && sc.vs_quit_box.valid) {
        MsgBox box = sc.vs_quit_box;
        msg_box_update_choice(ui, box, sc.vs_quit_cursor);
    }
    // the title and the four rows (RETRY, SELECT AREA, SELECT SV, EXIT) each grow in over 6
    // ticks; going out they shrink the same way
    const std::uint32_t pulse = menu_pulse(hud.frame);
    for (int pos = 0; pos < 5; ++pos) {
        const float start = static_cast<float>(pos) * menu_row_ticks;
        float fill = std::clamp((sc.vs_end_timer - start) / menu_row_ticks, 0.0f, 1.0f);
        if (sc.vs_end_state == 3) {
            fill = 1.0f - fill;
        }
        std::uint32_t colour = rows[static_cast<std::size_t>(pos)].color;
        if (pos > 0) {
            colour = sc.vs_end_state == 2 && sc.vs_end_cursor == pos - 1 ? pulse : 0xFF808080;
        }
        row_grown(ui, rows[static_cast<std::size_t>(pos)], fill, colour);
    }
}

} // namespace

void hud_vs_step(HudView& hud, Ui& ui, const World& world) {
    HudScenes& sc = hud.scenes;
    const VsBattle& vs = world.vs;
    sc.vs_mode = vs.mode;
    if (!vs.mode) {
        return;
    }
    for (int slot = 0; slot < fighter_slots; ++slot) {
        sc.vs_in_play[slot] = vs.in_play[static_cast<std::size_t>(slot)];
    }
    // the menu's sounds (battle_update's pads_cursor_move 3, accept 5, the quit box's 4)
    if (vs.end_state == 2 && sc.vs_end_state == 2) {
        if (vs.cursor != sc.vs_end_cursor) {
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        }
        if (vs.quit_open && sc.vs_quit_open && vs.quit_cursor != sc.vs_quit_cursor) {
            ui_sound(ui, bank_sys, se_menu_cursor_horizontal);
        }
        if (vs.quit_open != sc.vs_quit_open) {
            ui_sound(ui, bank_sys, se_menu_accept);
        }
    }
    if (vs.end_state == 3 && sc.vs_end_state == 2) {
        ui_sound(ui, bank_sys, se_menu_accept);
    }
    // the quit box opens with the world's, and grows once a step (its draw is a copy)
    if (vs.quit_open && !sc.vs_quit_open) {
        if (!sc.vs_quit_box.valid) {
            sc.vs_quit_box = msg_box_from(ui, "pause", "vs_quit_box", 0);
        }
        msg_box_open(sc.vs_quit_box);
    }
    if (vs.quit_open) {
        msg_box_update(ui, sc.vs_quit_box);
    }
    sc.vs_quit_open = vs.quit_open;
    sc.vs_quit_cursor = vs.quit_cursor;
    sc.vs_end_state = vs.end_state;
    sc.vs_end_timer = static_cast<float>(vs.end_timer);
    sc.vs_end_cursor = vs.cursor;
}

void hud_intro_start(HudView& hud, int ticks) {
    hud.scenes.intro_timer = ticks;
}

void hud_letterbox_set(HudView& hud, int state) {
    hud.scenes.letterbox_state = state;
}

void hud_death_card_start(HudView& hud, int killer) {
    hud.scenes.death_killer = killer;
    hud.scenes.death_timer = death_card_ticks;
}

void hud_scenes_step(HudView& hud, Ui& ui, const World& world) {
    HudScenes& sc = hud.scenes;
    // the intro counts down a tick a step: READY's sound as it comes in at 90, GO's at 30; the
    // scene ends as its timer runs out
    if (sc.intro_timer > 0) {
        sc.intro_timer -= 1;
        if (sc.intro_timer == 90) {
            ui_sound(ui, bank_sys, se_battle_intro_first);
        } else if (sc.intro_timer == 30) {
            ui_sound(ui, bank_sys, se_battle_intro_second);
        }
        if (sc.intro_timer <= 1) {
            sc.intro_timer = 0;
        }
    }
    // the letterbox: in over 30 ticks (2), held (1), out (3), off (0)
    sc.letterbox_prev = sc.letterbox_time;
    switch (sc.letterbox_state) {
    case 0:
        sc.letterbox_time = 0;
        break;
    case 1:
        sc.letterbox_time = 30;
        break;
    case 2:
        sc.letterbox_time += 1;
        if (sc.letterbox_time >= 30) {
            sc.letterbox_state = 1;
        }
        break;
    case 3:
        sc.letterbox_time -= 1;
        if (sc.letterbox_time <= 0) {
            sc.letterbox_state = 0;
        }
        break;
    default:
        break;
    }
    sc.letterbox_time = std::clamp(sc.letterbox_time, 0.0f, 30.0f);
    if (sc.death_timer > 0) {
        sc.death_timer -= 1;
    }
    // each human's VS mark scales in while it shows
    for (int me = 0; me < fighter_slots; ++me) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(me)];
        if (!f.present || f.controller == -1) {
            continue;
        }
        HudSlot& s = hud.slots[static_cast<std::size_t>(me)];
        const bool mark = sc.vs_mode && ((s.shows && sc.vs_end_state >= 1) || !sc.vs_in_play[me]);
        if (mark && s.fade_in < 30) {
            s.fade_in = std::min(s.fade_in + 1.0f, 30.0f);
        }
    }
}

void hud_draw_vs_mark(HudView& hud, Ui& ui, const World& world, int slot) {
    (void)world;
    const HudScenes& sc = hud.scenes;
    const HudSlot& s = hud.slots[static_cast<std::size_t>(slot)];
    const float t = s.fade_in / 30.0f;
    int row = 0; // YOU WIN: still in play once the round is decided
    if (!sc.vs_in_play[slot]) {
        bool anyone = false;
        for (bool in : sc.vs_in_play) {
            anyone = anyone || in;
        }
        row = anyone ? 1 : 2; // LOSE while someone plays on, DRAW once nobody does
    }
    const std::array<HudRow, 3>& rows =
        hud_views(hud) == 2 ? hud.data.vs_marks_2view : hud.data.vs_marks_1view;
    hud_row_draw_scaled(ui, rows[static_cast<std::size_t>(row)], t * t);
}

void hud_draw_scenes(HudView& hud, Ui& ui, const World& world, const HudFrame& frame) {
    const HudScenes& sc = hud.scenes;
    // READY while the intro's timer is 90..31, GO from 30, centred over everything (pass 14)
    if (sc.intro_timer > 0) {
        if (sc.intro_timer <= 30) {
            centred(ui, go_sprite, 320, 240, 0x1000);
        } else if (sc.intro_timer <= 90) {
            centred(ui, ready_sprite, 320, 240, 0x1000);
        }
    }
    // the letterbox, not in VS or replays: two black bars of 0xE7C scaled 22 x 3.5 (pass 15).
    // Retail slides them 32 px, so they pop in half way; the port slides each from fully out
    // (docs/bugs.md). Over the whole window (quad_full_window), as the 3D under them
    const float t = lerpf(sc.letterbox_prev, sc.letterbox_time, frame.alpha);
    if (!sc.vs_mode && world.battle_phase != 2 && (sc.letterbox_state != 0 || t > 0)) {
        const float k = (30.0f - t) / 30.0f;
        hud_sprite(ui, letterbox_sprite, -32.0f, -32.0f - k * 64.0f, 22.0f, 3.5f, 0, 0x7fff,
                   0xFF000000, quad_full_window);
        hud_sprite(ui, letterbox_sprite, -32.0f, 400.0f + k * 96.0f, 22.0f, 3.5f, 0, 0x7fff,
                   0xFF000000, quad_full_window);
    }
    // the death card: the killer's tag and "Crasher!" while the killer is on camera
    if (sc.death_timer > 0 && sc.death_killer >= 0 && sc.death_killer < fighter_slots &&
        world.fighters[static_cast<std::size_t>(sc.death_killer)].present) {
        hud_fighter_tag_draw(hud, ui, world, hud.viewer, sc.death_killer, 300.0f, 280.0f);
        hud_sprite(ui, crasher_sprite, 207.0f, 92.0f, 1, 1, 0, 0x101, 0xFFFFFFFF, 0);
    }
    if (sc.vs_mode) {
        draw_vs_menu(hud, ui, world);
    }
}
