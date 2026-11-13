// The HUD's entry points (hud.hpp): the battle entry's hud_init, hud_step after each world step
// and hud_draw once a frame, in hud_update's order (decomp docs/port/hud.md "One update"),
// The pieces every HUD file draws with (HudElem, color_blink, number font 3) are hud_sprites.cpp.
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "cook/pack_library.hpp"
#include "hud/hud_internal.hpp"
#include "view/cockpit_view.hpp"
#include "view/scene_camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

// the sheets every battle entry loads (battle_task_begin): game_tex 0x671, game_quest_tex 0x672,
// game_vs_tex 0x673
constexpr int hud_texture_sets[3] = {0x671, 0x672, 0x673};

// the world's screen units, x mirrored with the port's frame back the right way round
void sense_units(const HudSense& s, float out[2]) {
    out[0] = -fx_float(s.screen_x);
    out[1] = fx_float(s.screen_y);
}

// actor_durability_bar_start, the bar's roll (actor_step), the score's roll
// (fighter_task_update) and the weapons' refill roll (weapon_fire_controller's tail): ticks, one
// a step
// decomp: actor_durability_bar_start 0x762E8
void rolls_step(HudSlot& s, const Fighter& f) {
    const FighterCombat& c = f.combat;
    if (c.durability != s.durability_seen) {
        s.durability_seen = c.durability;
        const float lost = static_cast<float>(c.durability - s.bar_durability) /
                           static_cast<float>(std::max(c.durability_max, 1));
        s.bar_time = std::fabs(lost) * (std::fabs(lost) < 0.1f ? 300.0f : 90.0f);
        s.bar_from = s.bar_durability;
        s.bar_timer = s.bar_time;
    }
    if (s.bar_timer > 0) {
        s.bar_timer = std::max(s.bar_timer - 1.0f, 0.0f);
        s.bar_durability = static_cast<int>(static_cast<float>(c.durability - s.bar_from) *
                                            (1.0f - s.bar_timer / s.bar_time)) +
                           s.bar_from;
    }
    if (s.bar_timer == 0) {
        s.bar_durability = c.durability;
    }
    if (c.score > s.score_seen) {
        s.score_anim_len = static_cast<float>(std::min(c.score - s.score_seen, 30));
        s.score_from = s.score_shown;
        s.score_anim = s.score_anim_len;
    }
    s.score_seen = c.score;
    if (s.score_anim > 0) {
        s.score_anim = std::max(s.score_anim - 1.0f, 0.0f);
        s.score_shown = static_cast<int>(static_cast<float>(c.score - s.score_from) *
                                         (1.0f - s.score_anim / s.score_anim_len)) +
                        s.score_from;
    }
    if (s.score_anim == 0) {
        s.score_shown = c.score;
    }
    for (int m = 0; m < 4; ++m) {
        const float resource = fx_float(c.weapons[m].resource);
        if (s.resource_anim[m] > 0) {
            s.resource_anim[m] = std::max(s.resource_anim[m] - 1.0f, 0.0f);
            s.resource_shown[m] =
                static_cast<float>(static_cast<int>((resource - s.resource_from[m]) *
                                                    (1.0f - s.resource_anim[m] / 120.0f))) +
                s.resource_from[m];
        }
        if (s.resource_anim[m] == 0) {
            s.resource_shown[m] = resource;
        }
    }
}

// the screen points the draw blends between: the reticle's, the tracking point and the senses
void points_step(HudSlot& s, const World& world, int slot) {
    const FighterHud& h = world.huds[static_cast<std::size_t>(slot)];
    const LockHud& lock = world.fighters[static_cast<std::size_t>(slot)].combat.hud;
    std::copy(&s.center[1][0], &s.center[1][0] + 2, &s.center[0][0]);
    std::copy(&s.track[1][0], &s.track[1][0] + 2, &s.track[0][0]);
    std::copy(&s.sense[1][0][0], &s.sense[1][0][0] + 12, &s.sense[0][0][0]);
    s.center[1][0] = -fx_float(h.reticle_center_x);
    s.center[1][1] = fx_float(h.reticle_center_y);
    s.track[1][0] = s.center[1][0] - fx_float(lock.track_x);
    s.track[1][1] = s.center[1][1] + fx_float(lock.track_y);
    for (int i = 0; i < 6; ++i) {
        sense_units(h.sense[static_cast<std::size_t>(i)], s.sense[1][i]);
    }
}

// hud_update's view switch: in a view the cockpit set (view 0) or the gauge set, in place; leaving
// view 0 the cockpit set slides out over the first half and the gauge set in over the second,
// coming into it the other way round; between two other views the gauge set, the slide left as it
// was. view_switch_frames / 20 an update; R4, / 40 a step
void view_switch_step(HudSlot& s, const FighterCamera& c) {
    if (c.view == c.view_next) {
        s.view_switch_alpha = 0;
        s.cockpit_set = c.view == 0;
        return;
    }
    const float t = static_cast<float>(c.view_switch_frames) / 40.0f;
    if (c.view == 0 || c.view_next == 0) {
        const bool first_half = t > 0.5f;
        s.view_switch_alpha = first_half ? 1.0f - (t - 0.5f) * 2.0f : t * 2.0f;
        s.cockpit_set = first_half == (c.view == 0);
        return;
    }
    s.cockpit_set = false;
}

void slot_reset(HudSlot& s, const Fighter& f) {
    s = HudSlot{};
    s.bar_durability = s.durability_seen = f.combat.durability;
    s.score_shown = s.score_seen = f.combat.score;
    s.bob_body_y = fx_float(f.pose.head_position.y); // body_point from the spawn on
    for (int m = 0; m < 4; ++m) {
        s.resource_shown[m] = fx_float(f.combat.weapons[m].resource);
    }
}

} // namespace

const HudLayout& hud_layout(const HudView& hud) {
    const int views = hud_views(hud);
    return views == 1 ? hud.data.layout_1p : views == 2 ? hud.data.layout_2p : hud.data.layout_4p;
}

// decomp: hud_init 0x4E298
bool hud_init(HudView& hud, Ui& ui, const World& world, std::string& error) {
    if (!hud.ready) {
        if (!load_hud_data(hud.data, ui.data, error)) {
            return false;
        }
        hud.ready = true;
    }
    for (int set : hud_texture_sets) {
        texture_set_load(ui, set);
    }
    for (int slot = 0; slot < fighter_slots; ++slot) {
        slot_reset(hud.slots[static_cast<std::size_t>(slot)],
                   world.fighters[static_cast<std::size_t>(slot)]);
    }
    hud.frame = 0;
    hud.scenes = HudScenes{};
    hud.barks.running = false;
    hud.barks.pending.fill(0x23);
    if (!bark_tables_load(hud.barks.tables, ui.data.packs->core, error)) {
        std::fprintf(stderr, "hud: %s\n", error.c_str()); // no barks, the fight goes on
        error.clear();
    }
    return true;
}

bool hud_shows(const HudView& hud, const World& world, int slot) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    if (!f.present || f.controller == -1) {
        return false;
    }
    // the scenes that hide it: the intro and, outside VS, the death scene (the HUD's own intro
    // timer and a dead fighter in the viewer), and any scene the battle runs on the slot (the
    // entrance, the door, the death: battle_flow's FighterScene)
    // (the ending's last shot, 0x12, shows it: its cockpit view), and in free play (phase 4) every
    // scene but the door (6) and the intro (1..4): the lessons' 0x18..0x2E
    const int scene = world.battle_state.slots[slot].scene.state;
    const bool scene_shows = scene == 0 || scene == scene_ending_last ||
                             (world.battle_phase == 4 && scene != 6 && (scene < 1 || scene >= 5));
    if (hud.scenes.intro_timer > 0 || (!hud.scenes.vs_mode && !f.combat.alive) || !scene_shows) {
        return false;
    }
    // a paused slot (battle_slot_bookkeeping's 0x24, which a boss entrance sets on everyone else):
    // none, so the First Ranker's entrance shows no HUD under its letterbox (hud_update)
    if (world.battle_state.slots[slot].parked) {
        return false;
    }
    const int phase = world.battle_phase;
    if (phase != 0 && phase != 3 && phase != 4) {
        return false;
    }
    return !hud.scenes.vs_mode || hud.scenes.vs_in_play[slot];
}

void hud_step(HudView& hud, Ui& ui, const World& world) {
    if (!hud.ready || !world.battle) {
        return;
    }
    hud.task_timer += 1;
    hud.frame += 1;
    hud_vs_step(hud, ui, world);
    // this step's events: popups, finished locks (a sound for a human locker; the target's bark is
    // the world's), the lock alarm, refills
    bool locked[6] = {};
    for (int e = 0; e < world.combat.event_count; ++e) {
        const CombatEvent& ev = world.combat.events[static_cast<std::size_t>(e)];
        if (ev.slot < 0 || ev.slot >= fighter_slots) {
            continue;
        }
        HudSlot& s = hud.slots[static_cast<std::size_t>(ev.slot)];
        switch (ev.kind) {
        case event_popup: {
            // actor_hud_popup_push: from 7 queued on, the oldest goes
            HudPopup line{ev.value, ev.effect, 120.0f};
            if (s.popup_count >= 7) {
                std::copy(s.queued + 1, s.queued + hud_popup_lines, s.queued);
                s.queued[7] = line;
            } else {
                s.queued[s.popup_count++] = line;
            }
            break;
        }
        case event_lock_done: {
            const Fighter& locker = world.fighters[static_cast<std::size_t>(ev.slot)];
            locked[ev.slot] = locker.controller != -1 &&
                              (world.battle_phase == 0 || world.battle_phase == 4);
            break;
        }
        case event_weapon_open:
            // sound_start(3, 0x15) at the fighter's position (flags 2: placed)
            if (ui.audio) {
                const float at[3] = {fx_float(ev.position.x), fx_float(ev.position.y),
                                     fx_float(ev.position.z)};
                play_sound(*ui.audio, bank_battle_player, se_weapon_open, sound_positioned, at);
            }
            break;
        case event_lock_warning:
            // sound_play_at(3, 0x2A) at the fighter: a 3D cue (a 2D start is refused)
            if (ev.value && ui.audio) {
                const float at[3] = {fx_float(ev.position.x), fx_float(ev.position.y),
                                     fx_float(ev.position.z)};
                play_sound(*ui.audio, bank_battle_player, se_lock_warning, sound_positioned, at);
            } else if (ui.audio) {
                stop_sound(*ui.audio, bank_battle_player, se_lock_warning);
            }
            break;
        case event_gauge_refill:
            if (ev.value >= 0 && ev.value < 4) {
                s.resource_from[ev.value] = s.resource_shown[ev.value];
                s.resource_anim[ev.value] = 120.0f;
            }
            break;
        default:
            break;
        }
    }
    // hud_locks_update's sound: once an update for each human whose marks finished
    for (bool on : locked) {
        if (on) {
            ui_sound(ui, bank_battle_player, se_lock_complete);
        }
    }
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        HudSlot& s = hud.slots[static_cast<std::size_t>(slot)];
        if (!f.present) {
            continue;
        }
        rolls_step(s, f);
        points_step(s, world, slot);
        hud_marks_step(hud, world, slot);
        s.shows = hud_shows(hud, world, slot);
        // the bob once an update; its body height is taken every update, shown or not
        // (actor_frame_reset's body_point)
        if (world.step % 2 == 0) {
            hud_bob_step(s, f, s.shows);
        }
        if (s.shows) {
            // the view switch and the drawn set's own steps
            view_switch_step(s, f.camera);
            hud_popups_step(s, hud_views(hud) == 1);
            if (s.cockpit_set && hud_views(hud) == 1) {
                hud_meters_step(s, f);
            }
        }
    }
    hud_scenes_step(hud, ui, world);
    hud_barks_step(hud, ui, world);
}

void hud_draw(HudView& hud, Ui& ui, const World& world, const HudScreen& screen, float alpha) {
    hud.cockpit.shown = false;
    hud.cockpit.quads.clear();
    HudScreen one = screen;
    one.view = -1;
    // the drawn 3D: the safe rectangle, or the whole screen
    const float safe[4] = {32, 16, 576, 448}, whole[4] = {0, 0, 640, 480};
    for (int i = 0; i < 4; ++i) {
        one.rect[i] = safe[i];
        one.drawn[i] = screen.safe_rect ? safe[i] : whole[i];
    }
    hud_draw_player(hud, ui, world, one, alpha, hud.viewer);
    hud_draw_overlay(hud, ui, world, alpha);
}

void hud_draw_player(HudView& hud, Ui& ui, const World& world, const HudScreen& screen,
                     float alpha, int slot) {
    if (!hud.ready || !world.battle || slot < 0 || slot >= fighter_slots) {
        return;
    }
    // the drawn 3D in sprite pixels: its window rectangle (scenery.cpp) back through the sprite
    // layer's 4:3 fit (vk_sprites.cpp), then into the view's sprite space (a view's sprites are
    // placed from its rectangle's corner plus the safe origin)
    HudFrame frame;
    frame.alpha = alpha;
    frame.view_count = hud_views(hud);
    const float w = static_cast<float>(std::max(screen.width, 1));
    const float h = static_cast<float>(std::max(screen.height, 1));
    const float* view = screen.drawn;
    const float sx = w / 640.0f, sy = h / 480.0f;
    const float fit = std::min(sx, sy);
    const float ox = (w - 640.0f * fit) * 0.5f, oy = (h - 480.0f * fit) * 0.5f;
    frame.cx = (view[0] * sx + view[2] * sx * 0.5f - ox) / fit;
    frame.cy = (view[1] * sy + view[3] * sy * 0.5f - oy) / fit;
    if (screen.view >= 0) {
        frame.cx += 32.0f - screen.rect[0] - screen.sprite_shift;
        frame.cy += 16.0f - screen.rect[1];
    }
    // a screen unit in sprite pixels. The world's screen units are the one-view projection's for
    // every player in every mode (52 degrees over 576 x 448; docs/bugs.md, "lock-on width"), so a
    // unit spans half the drawn 3D's height and that times 576/448 across, whatever the view's
    // shape: in a wide two-player strip the lock-on and its boxes keep one view's width
    const float drawn_h = view[3] * sy / fit;
    frame.unit_y = drawn_h * 0.5f;
    frame.unit_x = frame.unit_y * 576.0f / 448.0f;
    // FIELD OF VIEW: the drawn view may be wider than the world's 52 degrees, which brings the
    // world's points in toward the centre (both ways alike: the drawn view is Hor+)
    frame.world = fight_fov_unit(screen.camera.widen);
    // the hit shake: the world's points shook with its look-at; CAMERA SHAKE under 100 takes them
    // back toward the plain view with the drawn camera (the world's screen x is the HUD's negated)
    float shake[2];
    cockpit_shake_hud(world, slot, screen.shake, shake);
    frame.cx -= shake[0] * frame.world * frame.unit_x;
    frame.cy -= shake[1] * frame.world * frame.unit_y;
    // retail's HUD viewport by view count (viewport_layouts), and the drawn 3D's scale to it, for
    // the radar's 640x480 offsets
    const float W = frame.view_count == 3 ? 284.0f : 576.0f;
    const float H = frame.view_count == 1 ? 448.0f : 220.0f;
    const float s = drawn_h / H;
    frame.scale_x = W / 640.0f * s;
    frame.scale_y = H / 480.0f * s;
    // the two-view radar keeps its shape (retail squashes it to half height; docs/bugs.md)
    if (frame.view_count == 2) {
        frame.scale_x = frame.scale_y;
    }
    frame.sprite_scale = frame.view_count == 1 ? 1.0f : 0.5f;

    const int viewer_before = hud.viewer;
    hud.viewer = slot;
    ui.view = screen.view;
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    if (f.present && f.controller != -1) {
        if (hud.slots[static_cast<std::size_t>(slot)].shows) {
            // the cockpit set (view 0 in one view: the instruments) or the gauge set, the
            // readout, the reticle, radar and marks
            if (hud.slots[static_cast<std::size_t>(slot)].cockpit_set) {
                if (frame.view_count == 1) {
                    hud_meters_build(hud, world);
                }
            } else {
                hud_draw_gauges(hud, ui, world);
            }
            hud_draw_readout(hud, ui, world, frame);
            if (f.camera.view == 0 || f.camera.view == 1) {
                hud_draw_reticle(hud, ui, world, frame);
            }
            if (hud.scenes.vs_mode && hud.scenes.vs_end_state >= 1) {
                hud_draw_vs_mark(hud, ui, world, slot);
            }
        } else if (hud.scenes.vs_mode && !hud.scenes.vs_in_play[slot]) {
            hud_draw_vs_mark(hud, ui, world, slot);
        }
    }
    ui.view = -1;
    hud.viewer = viewer_before;
}

void hud_draw_overlay(HudView& hud, Ui& ui, const World& world, float alpha) {
    if (!hud.ready || !world.battle) {
        return;
    }
    HudFrame frame;
    frame.alpha = alpha;
    frame.view_count = hud_views(hud);
    hud_draw_scenes(hud, ui, world, frame);
    hud_draw_barks(hud, ui);
}
