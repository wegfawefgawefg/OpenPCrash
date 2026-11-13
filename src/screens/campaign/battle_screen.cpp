// The battle's application state (6): battle_spawn's own task (decomp src/od/u_04a150.c
// battle_enter .. battle_enter_next3; docs/port/battle.md "The battle's own parent
// in state 6"), the 2D the fight draws over the 3D (battle_letterbox_draw's letterbox, READY and
// GO from the entrance, docs/port/battle.md "The entrance") and the pause (src/od/u_0b10c0.c
// pause_enter, pause_menu_update, pause_screen_draw; docs/port/screens.md "Pause in battle").
// Main's battle session (src/battle_run.hpp) loads and steps the fight the state asks for.
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "input/seats.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/extra_screens.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/music_panel.hpp"
#include "screens/pc/pause_settings.hpp"
#include "view/view.hpp"
#include "world/world.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>

namespace {

constexpr int fade_sprite = 0xe7c;
constexpr int sprite_ready = 0xe80;
constexpr int sprite_go = 0xe7f;
constexpr int sprite_pause = 0xe82;
constexpr int letterbox_ticks = 30;
constexpr int free_play_track = 5; // 12SENTOU.WAV, unlooped (free_play_screen_step2)

} // namespace

// two black bars (0xE7C scaled 22 x 3.5) 32 pixels in at full,
// over the whole window (quad_full_window): retail's span its screen's width
// decomp: battle_letterbox_draw 0x49000
void letterbox_draw(Ui& ui, const BattleState& b) {
    if (b.letterbox_state == 0) {
        return;
    }
    const float t = static_cast<float>(fx_float(b.letterbox_time));
    const float offset = (static_cast<float>(letterbox_ticks) - t) / 30.0f * 32.0f;
    for (int bar = 0; bar < 2; ++bar) {
        Sprite s = sprite_of(ui, fade_sprite);
        s.pos[0] = -32.0f;
        // D_0019D390 for the lower bar: 32 bars' height from the bottom (448 + 32 - 3.5 x 32)
        s.pos[1] = bar == 0 ? -32.0f - offset : 480.0f - 80.0f + offset;
        s.blend = 0x7fff;
        s.scale[0] = 22.0f;
        s.scale[1] = 3.5f;
        s.color = 0xff000000;
        s.flags |= quad_full_window;
        sprite_submit(ui, s);
    }
}

namespace {

// scene_intro_update's READY (from 90 ticks left) and GO (from 30) for the human's entrance
void ready_go_draw(FrontEnd& fe, Ui& ui) {
    const World& world = *fe.world;
    for (int slot = 0; slot < fighter_slots; ++slot) {
        const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
        const FighterScene& scene = world.battle_state.slots[slot].scene;
        if (!f.present || f.controller == -1 || scene.state < 1 || scene.state > 4 ||
            scene.step != 1) {
            continue;
        }
        const Fx timer = scene.timer;
        int id = -1;
        if (timer <= fx(30)) {
            id = sprite_go;
        } else if (timer <= fx(90)) {
            id = sprite_ready;
        }
        if (id >= 0) {
            Sprite s = sprite_of(ui, id);
            s.pos[0] = 320.0f;
            s.pos[1] = 240.0f;
            s.blend = 0x1000;
            s.flags |= 8;
            sprite_submit(ui, s);
            return; // once over every view (VS's entrances run together)
        }
    }
}

} // namespace

PadRecord pause_pads(const FrontEnd& fe, const Ui& ui) {
    PadRecord out = ui.pad;
    if (!fe.vs_mode) {
        return out;
    }
    // seat 0's own record carries the scripted presses (--press) too
    for (int seat = 0; seat < max_pad_players; ++seat) {
        if (!fe.vs.entered[static_cast<std::size_t>(seat)]) {
            continue;
        }
        const PadButtons& b = ui.pads[static_cast<std::size_t>(seat)].b;
        out.b.cur |= b.cur;
        out.b.rising |= b.rising;
        out.b.repeat |= b.repeat;
        out.b.falling |= b.falling;
        out.connected = true;
    }
    return out;
}

namespace {

// pause_leave: sound 0xD, the streams and effects resumed
void pause_release(FrontEnd& fe, Ui& ui) {
    fe.campaign.paused = false;
    ui_sound(ui, bank_sys, se_pause);
    if (ui.audio) {
        pause_music(*ui.audio, 1);
        pause_sounds(*ui.audio, 1);
    }
}

// the seats watched for a lost pad (players_without_pad): outside VS player 0's pad, the one that
// pressed START on the title (pad_watch.cpp); in VS each entered seat's own device
unsigned pads_missing_now(FrontEnd& fe) {
    if (fe.vs_mode) {
        unsigned mask = 0;
        for (int seat = 0; seat < max_pad_players; ++seat) {
            if (fe.vs.entered[static_cast<std::size_t>(seat)] && fe.seats &&
                !seat_connected(*fe.seats, seat)) {
                mask |= 1u << seat;
            }
        }
        return mask;
    }
    return pad_watch_lost(fe) ? 1u : 0u;
}

// the frozen frame at half brightness (the port draws the stopped
// battle under a half black quad over the whole window rather than a captured texture), then
// PAUSE, or free play's menu (cursor >= 0): the plate 0xEB1, continue 0xE8F and quit 0xE90, the
// chosen one blinking; the dim alone under the pause's settings panel (port-only)
// decomp: pause_screen_draw 0xB155A
void pause_draw(Ui& ui, int cursor, bool dim_only) {
    Sprite dim = sprite_of(ui, fade_sprite);
    dim.pos[0] = -32.0f;
    dim.pos[1] = -32.0f;
    dim.blend = 0x1000;
    dim.scale[0] = 22.0f;
    dim.scale[1] = 17.0f;
    dim.color = 0x80000000;
    dim.flags |= quad_full_window;
    sprite_submit(ui, dim);
    if (dim_only) {
        return;
    }
    if (cursor >= 0) {
        Sprite plate = sprite_of(ui, 0xeb1);
        plate.pos[0] = 261.0f;
        plate.pos[1] = 193.0f;
        plate.blend = 0x1001;
        sprite_submit(ui, plate);
        for (int i = 0; i < 2; ++i) {
            Sprite item = sprite_of(ui, 0xe8f + i);
            item.pos[0] = 272.0f;
            item.pos[1] = i == 0 ? 226.0f : 248.0f;
            item.blend = 0x1002;
            if (i == cursor) {
                item.flags |= 0x20;
            } else {
                item.color = 0xff808080;
            }
            sprite_submit(ui, item);
        }
        return;
    }
    Sprite pause = sprite_of(ui, sprite_pause);
    pause.pos[0] = 320.0f;
    pause.pos[1] = 240.0f;
    pause.blend = 0x1001;
    pause.flags |= 8;
    sprite_submit(ui, pause);
}

} // namespace

void battle_screen_spawn(FrontEnd& fe, Ui&) {
    fe.screen.campaign.battle = BattleScreen{};
    if (fe.state == app_battle) {
        fe.campaign.battle_phase = 0;
    }
    fe.campaign.paused = false;
    fe.campaign.battle_stop = false;
}

void battle_screen_step(FrontEnd& fe, Ui& ui) {
    BattleScreen& s = fe.screen.campaign.battle;
    Campaign& c = fe.campaign;
    // PHANTOMCRASH_DEV_PAUSE=U (port-only, headless captures) pauses at front end update U
    static const int pause_at = std::getenv("PHANTOMCRASH_DEV_PAUSE")
                                    ? std::atoi(std::getenv("PHANTOMCRASH_DEV_PAUSE"))
                                    : -1;
    if (fe.updates == pause_at && !c.paused) {
        battle_pause_toggle(fe, ui, true);
    }
    const BattleState& b = fe.world->battle_state;
    if (c.battle_running) {
        // battle_update's, not in VS; nor under the pause's settings panel, which it would cut
        if (!fe.vs_mode && !fe.pause_settings) {
            letterbox_draw(ui, b);
        }
        ready_go_draw(fe, ui);
        if (c.paused && !report_panel_hides_pause(fe)) {
            // the menu's items, then the PC's (pause_settings.hpp), or the dim alone under the
            // settings panel
            const bool menu = fe.state == app_free_play || fe.vs_mode;
            pause_draw(ui, menu ? c.pause_cursor : -1, fe.pause_settings);
            pause_items(fe, ui, menu);
        }
        if (c.paused) {
            if (fe.vs_mode && s.quit_open) {
                msg_box_update_choice(ui, s.quit_box, s.quit_cursor);
            }
            // a missing pad's box over the pause (pad_missing_*, kind 2); not over the settings
            // panel, after which it shows again
            if (c.pads_missing != 0 && !fe.pause_settings) {
                pad_missing_box_draw(fe, ui, 1);
            }
            if (fe.vs_mode && fe.screen.step < 3) {
                vs_pause_step(fe, ui);
            }
            return; // the game subtree stands still under the pause
        }
    }
    switch (fe.screen.step) {
    case 0: // battle_enter: the battle task asked of main, 5 s without the battle music
        c.battle_wanted = 1;
        s.counter = ui.frame_cadence * 5;
        fe.screen.step = 1;
        break;
    case 1: // battle_enter_next, once the battle is up (main loads it in one update)
        if (!c.battle_running) {
            break;
        }
        if (s.counter == ui.frame_cadence * 5) {
            // retail's frame_cadence * 0.5 ticks is a quarter second at 30 Hz; the half second
            // meant (docs/bugs.md)
            fade_in_start(ui, 30.0f);
        }
        if (--s.counter < 0) {
            fe.screen.step = 2;
            if (fe.state == app_free_play) {
                ui_music(ui, free_play_track, false); // free_play_screen_step2
            }
        }
        break;
    case 2: // battle_enter_next2: until the battle writes its result
        // the music: the playlist's next song after each 3 s gap (free play's step 3 too; the
        // test drive plays its own track, test_drive_step)
        if (fe.state != app_test_drive) {
            fight_music_update(fe, ui);
        }
        if (fe.vs_mode) {
            s.result = fe.world->vs.result; // the end menu's 2..5 (battle_update)
        } else if (b.end_state == 1 || b.end_state == 2) {
            s.result = 1; // battle_replacements_update's battle_result_set
        } else if (b.ending_requested) {
            s.result = 6; // battle_ending_request: the First Ranker's defeat
        }
        if (s.result != 0) {
            s.counter = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
            fade_out_start(ui, static_cast<float>(s.counter));
            fe.screen.step = 3;
        }
        break;
    case 3: // battle_enter_next3: the fade, then the answer (retail counts 15 updates against a
            // 15-tick fade and holds black a quarter second; here the answer comes once black)
        if (fe.state != app_test_drive) {
            fight_music_update(fe, ui);
        }
        if (fade_is_black(ui)) {
            ui_music_stop(ui);
            if (fe.vs_mode) {
                // state_battle_update's VS answers: the VS screen resumes at the area page (3) or
                // the machine page (4); the title (5) ends VS
                if (s.result == 3 || s.result == 4) {
                    fe.vs.return_page = s.result == 3 ? 2 : 1;
                } else if (s.result != 2) {
                    fe.vs_mode = false;
                    seats_alone(*fe.seats);
                }
            }
            task_header_set(fe, s.result - 1);
            fe.screen.step = 4;
        }
        break;
    default:
        break;
    }
}

void battle_screen_quit(FrontEnd& fe, Ui& ui) {
    BattleScreen& s = fe.screen.campaign.battle;
    Campaign& c = fe.campaign;
    if (c.paused) {
        c.paused = false;
        if (ui.audio) {
            pause_music(*ui.audio, 1);
            pause_sounds(*ui.audio, 1);
        }
    }
    s.result = 1;
    s.counter = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
    fade_out_start(ui, static_cast<float>(s.counter));
    fe.screen.step = 3;
}

void battle_screen_destroy(FrontEnd& fe, Ui& ui) {
    World& world = *fe.world;
    BattleState& b = world.battle_state;
    // fighter_task_destroy: a human whose task isn't parked builds the result again at teardown,
    // so the final elapsed includes the end scene and the fade
    for (const Fighter& f : world.fighters) {
        if (b.live && f.present && f.controller != -1 && !b.slots[f.slot].parked) {
            battle_result_build(world, f);
        }
    }
    b.live = false;
    fe.campaign.battle_stop = true;
    fe.campaign.paused = false;
    fight_lists_commit(fe); // the playlists edited over the pause (music_panel.hpp)
    ui_music_stop(ui);
    if (ui.audio) {
        pause_sounds(*ui.audio, 1);
    }
}

// decomp: pause_toggle_update 0xE0590
void battle_pause_toggle(FrontEnd& fe, Ui& ui, bool start_pressed) {
    Campaign& c = fe.campaign;
    if (fe.report.mode != 0 || fe.pause_settings) {
        return; // REPORT A BUG or the settings are up over the pause
    }
    const StateInfo& info = fe.states[static_cast<std::size_t>(fe.state)];
    // Start pauses only in states 6, 0x2A and 0x2F, never while a fade blanks the pads, nor in
    // VS's end sequence (its menu has the quit)
    const bool may = c.battle_running && fade_is_idle(ui) && info.pause_allowed &&
                     !(fe.vs_mode && fe.world->vs.end_state >= 1) && fe.screen.step < 3;
    // the lost-pad watch (app_pad_watch, pad_pause 2): a pad pulled pauses the fight once with
    // the reconnect box; Start on whatever is left goes on (the pulled seat reads a pad at rest),
    // where retail waits for the pad
    c.pads_missing = c.battle_running && info.pad_pause != 0 ? pads_missing_now(fe) : 0;
    const bool pulled = (c.pads_missing & ~c.pads_missing_before) != 0;
    c.pads_missing_before = c.pads_missing;
    if (pulled && may && !c.paused) {
        start_pressed = true;
    } else if (start_pressed && c.paused && c.pads_missing != 0 && !fe.vs_mode) {
        pad_watch_keyboard(fe); // the keyboard took over: the box goes
        c.pads_missing = c.pads_missing_before = 0;
    }
    if (!start_pressed || !may) {
        return;
    }
    BattleScreen& s = fe.screen.campaign.battle;
    s.quit_open = false;
    s.quit_cursor = 1;
    c.paused = !c.paused;
    c.pause_cursor = 0;
    // pause_enter / pause_leave: the sound 0xD, the streams and effects paused and resumed
    ui_sound(ui, bank_sys, se_pause);
    if (ui.audio) {
        pause_music(*ui.audio, c.paused ? 0 : 1);
        pause_sounds(*ui.audio, c.paused ? 0 : 1);
    }
    menu_input_mark(ui);
}

void battle_events_play(FrontEnd& fe, Ui& ui) {
    const BattleState& b = fe.world->battle_state;
    for (int i = 0; i < b.event_count; ++i) {
        const BattleEvent& e = b.events[static_cast<std::size_t>(i)];
        switch (e.kind) {
        case BattleEventKind::Sound:
            if (ui.audio && fe.world->battle_phase == 3 && e.a == 3 && e.b == 0x27) {
                // the ending's last shot's signal: sound_start_3d at the shot's focus
                // (scene_ending_update), the player's scene
                const int slot = std::max(seat_slot(*fe.world, 0), 0);
                const FxVec3& focus = b.slots[slot].scene.focus;
                const float at[3] = {fx_float(focus.x), fx_float(focus.y), fx_float(focus.z)};
                play_sound(*ui.audio, e.a, e.b, sound_positioned, at);
                break;
            }
            ui_sound(ui, e.a, e.b);
            break;
        case BattleEventKind::Music:
            ui_music(ui, e.a, e.b != 0);
            break;
        case BattleEventKind::Theme:
            tournament_theme_play(ui, e.a);
            break;
        case BattleEventKind::SceneText:
            break; // the HUD's bark window plays it (src/hud/hud_barks.cpp)
        }
    }
}

void vs_pause_step(FrontEnd& fe, Ui& ui) {
    BattleScreen& s = fe.screen.campaign.battle;
    Campaign& c = fe.campaign;
    const PadRecord saved = ui.pad;
    ui.pad = pause_pads(fe, ui);
    if (!s.quit_open) {
        // continue or quit (sound 3 on a move, 5 on accept), then the PC's items; cancel is read
        // nowhere
        PointerRect items[2 + pause_items_max] = {sprite_rect(ui, 0xe8f, 272, 226),
                                                  sprite_rect(ui, 0xe90, 272, 248)};
        const int count = 2 + pause_items_count();
        for (int i = 2; i < count; ++i) {
            items[i] = pause_item_rect(i - 2, true);
        }
        menu_cursor(ui, c.pause_cursor, 0, count, cursor_menu_vertical, 0, 3, items);
        if (c.pause_cursor >= 2) {
            if (pause_item_accepted(ui, items[c.pause_cursor])) {
                pause_item_open(fe, ui, c.pause_cursor - 2);
            }
        } else if (menu_accept(ui, &items[c.pause_cursor])) {
            ui_sound(ui, bank_sys, se_menu_accept);
            if (c.pause_cursor == 0) {
                pause_release(fe, ui);
            } else {
                s.quit_open = true;
                s.quit_cursor = 1; // No first
                s.quit_box = msg_box_from(ui, "pause", "vs_quit_box", 0);
                msg_box_open(s.quit_box);
            }
        }
    } else {
        // the question: Yes quits to the title with nothing counted (vs_end_state 5), No goes
        // back to the menu. Retail also moved the hidden menu cursor here (docs/bugs.md)
        PointerRect answers[2];
        msg_box_choice_rects(ui, s.quit_box, answers);
        menu_cursor(ui, s.quit_cursor, 0, 2, cursor_menu_horizontal, 0, 4, answers);
        if (menu_accept(ui, &answers[s.quit_cursor])) {
            ui_sound(ui, bank_sys, se_menu_accept);
            s.quit_open = false;
            if (s.quit_cursor == 0) {
                pause_release(fe, ui);
                s.result = 5;
                s.counter = static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
                fade_out_start(ui, static_cast<float>(s.counter));
                fe.screen.step = 3;
            } else {
                c.pause_cursor = 0;
            }
            s.quit_cursor = 1;
        }
    }
    ui.pad = saved;
}
