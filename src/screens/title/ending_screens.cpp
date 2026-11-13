// The ending's scene (0xA, the battle in phase 3 through its shots, then the last frame switched
// off) and its movie states (decomp docs/port/progression.md "The ending", docs/movies.md): 0xB
// the ending movie with its subtitles, 0x25 the credits (staff, epilogue, the_end looped), 0xC the
// secret movies (kudryavka, cloud), all through src/movie's screens and boot_screens.cpp's movie
// step. Entering the credits sets the beaten bit; the credits and the secret movies go on to the
// night (0x18), where the game carries on: there is no New Game+ restart, the 60-day cycle counts
// completed_cycles from then on (day_advance) and opponents level up with it.
//
// And free play (0x2A, state_free_play_enter): the help menu's FreePlay and three lessons.
// The fight is the battle entry's; this file has the setup free_play_screen_update hands
// it (FreePlaySetup), from GameState.free_play_lesson.
#include "audio/audio.hpp"
#include "audio/sound_cues.hpp"
#include "battle/battle_data.hpp"
#include "battle/battle_ending.hpp"
#include "input/menu_input.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/pause_settings.hpp"
#include "story/roster_tables.hpp"
#include "story/rumble.hpp"
#include "talk/draw2d.hpp"
#include "view/view.hpp"
#include "world/world.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace {

// the ending scene's texture sets: the battle's (game_tex, game_quest_tex), scr_common and
// ending_data_tex
constexpr int ending_assets[4] = {0x671, 0x672, 0x690, 0x68f};
// the capture texture's slot (0x225, frame_to_texture: Ui::capture), and the scene timer (curve
// frames) from which each frame is copied there
constexpr int capture_slot = movie_slot;
constexpr float capture_from_frames = 4.0f;
// the last shot's signal (0xBF7 over 0xC02... drawn centred, layers 0x1002 and 0x1003) from 60
// frames left, every other update
constexpr int signal_frames = 60;
// music_play(0xB2) as the last shot ends
constexpr int ending_end_track = 0xb2;
// scale_alpha_keys: (scale x, scale y, alpha, update) of the last frame switching off
struct TintKey {
    float sx, sy, alpha;
    int at;
};
constexpr TintKey tint_keys[7] = {{0.25f, 1, 1, 2}, {0.25f, 1, 1, 3}, {1, 1, 1, 4},
                                  {1, 0.05f, 1, 6}, {1, 0.05f, 1, 7}, {1, 1, 1, 8},
                                  {0, 0, 0, 11}};

// a black quad (0xE7C, 32 x 32) over a rectangle of the 640 x 480 screen, stretched with it over
// the whole window (quad_full_window) as the 3D it masks
void black_quad(Ui& ui, float x, float y, float w, float h, float alpha, int blend) {
    if (w <= 0 || h <= 0 || alpha <= 0) {
        return;
    }
    Sprite s = sprite_of(ui, 0xe7c);
    s.pos[0] = x;
    s.pos[1] = y;
    s.scale[0] = w / 32.0f;
    s.scale[1] = h / 32.0f;
    s.blend = blend;
    s.color = static_cast<std::uint32_t>(std::min(alpha, 1.0f) * 255.0f) << 24;
    s.flags |= quad_full_window;
    sprite_submit(ui, s);
}

// screen_tint_draw on the frame the last shot left (texture 0x225): centred, scaled and
// faded by the keys, on black. The port keeps the stopped battle drawn and masks it to the same
// rectangle; false once the keys have run out
// decomp: screen_tint_draw 0x6C8B3
bool tint_draw(Ui& ui, std::uint32_t start) {
    const int frame = static_cast<int>((ui.task_timer - start) / (60u / static_cast<unsigned>(ui.frame_cadence)));
    int key = 0;
    while (key < 7 && frame >= tint_keys[key].at) {
        ++key;
    }
    if (key == 7) {
        black_quad(ui, -32, -32, 704, 544, 1, 0x1004);
        return false;
    }
    const TintKey& to = tint_keys[key];
    const TintKey& from = tint_keys[key > 0 ? key - 1 : 0];
    const int span = to.at - from.at;
    const float a = span > 0 ? std::clamp(static_cast<float>(frame - from.at) / static_cast<float>(span), 0.0f, 1.0f) : 0.0f;
    const float sx = from.sx * (1 - a) + to.sx * a;
    const float sy = from.sy * (1 - a) + to.sy * a;
    const float alpha = from.alpha * (1 - a) + to.alpha * a;
    const float w = 640.0f * sx, h = 480.0f * sy;
    const float left = 320.0f - w * 0.5f, top = 240.0f - h * 0.5f;
    black_quad(ui, -32, -32, left + 32, 544, 1, 0x1004);
    black_quad(ui, left + w, -32, 672 - left - w, 544, 1, 0x1004);
    black_quad(ui, left, -32, w, top + 32, 1, 0x1004);
    black_quad(ui, left, top + h, w, 512 - top - h, 1, 0x1004);
    black_quad(ui, left, top, w, h, 1.0f - alpha, 0x1004);
    return true;
}

// the captured frame (slot 0x225) over the screen, fading out
// over the capture's length (retail: capture_fade_frames counted down a main loop, 30 a second
// in a battle). Retail's pass 14 is clipped to the safe area its 3D fills; the port's 3D fills
// the screen, so the frame goes in pass 15 (the whole 640 x 480) under the fade, submitted after
// the letterbox, which the captured frame holds as well
// decomp: frame_capture_fade_draw 0x9FA05
void dissolve_draw(Ui& ui, EndingSceneScreen& e) {
    if (e.dissolve_ticks <= 0 || !ui.renderer) {
        return;
    }
    const int elapsed = static_cast<int>(ui.task_timer - e.dissolve_start);
    if (elapsed >= e.dissolve_ticks) {
        e.dissolve_ticks = 0;
        ui.capture = SlotTexture{};
        return;
    }
    const int texture = renderer_capture_texture(*ui.renderer);
    if (texture < 0) {
        return;
    }
    ui.capture = SlotTexture{texture, 640, 480};
    Sprite s;
    s.tex.slot = capture_slot;
    s.tex.rect[2] = 640.0f;
    s.tex.rect[3] = 480.0f;
    s.tex.sheet[0] = 640.0f;
    s.tex.sheet[1] = 480.0f;
    s.pos[0] = 31.0f; // the whole screen: pass 15 places from (32, 16) as the others
    s.pos[1] = 15.0f;
    s.blend = 0x7fff;
    s.flags |= sprite_smooth | quad_full_window; // filtered; the capture is the whole window
    s.z = static_cast<float>(e.dissolve_ticks - 1 - elapsed) / static_cast<float>(e.dissolve_ticks);
    sprite_submit(ui, s);
}

// what the player's shot does to the screen, followed from the world: its fade in as it starts,
// its fade out at its frame, the dissolve into the next shot, the letterbox, and the last shot's
// signal
void shots_follow(FrontEnd& fe, Ui& ui, EndingSceneScreen& e) {
    const World& world = *fe.world;
    if (!world.battle) {
        return;
    }
    letterbox_draw(ui, world.battle_state);
    const int slot = std::max(seat_slot(world, 0), 0);
    const FighterScene& scene = world.battle_state.slots[slot].scene;
    const int row = scene.state - scene_ending_first;
    if (row < 0 || row >= ending_shot_count) {
        return;
    }
    const EndingShot& shot = world.battle->ending.shots[row];
    const float timer = fx_float(scene.timer);
    // scene_ending_exit's frame_to_texture_for: the last frame of a shot with a capture is kept
    // (the renderer copies each frame through the shot's last few) and dissolves into the next
    const int prev_row = e.shot_state - scene_ending_first;
    if (e.shot_state != scene.state && prev_row >= 0 && prev_row < ending_shot_count) {
        const EndingShot& prev = world.battle->ending.shots[prev_row];
        if (prev.capture > Fx{}) {
            e.dissolve_start = ui.task_timer;
            // capture_fade_frames counts the battle's main loops, 30 a second: two ticks each
            e.dissolve_ticks = static_cast<int>(fx_float(prev.capture)) * 2;
        }
    }
    // (the world ends a shot in the step its timer reaches 2, so the frames from 4 down are kept)
    if (ui.renderer) {
        const bool keep = shot.capture > Fx{} && scene.step >= 1 && timer <= capture_from_frames;
        if (keep) {
            renderer_capture_texture(*ui.renderer); // made before its first copy
        }
        renderer_capture_frames(*ui.renderer, keep);
    }
    dissolve_draw(ui, e);
    if (scene.step == 1 && (e.shot_step != 1 || e.shot_state != scene.state) &&
        shot.fade_in > Fx{}) {
        fade_in_start(ui, fx_float(shot.fade_in));
    }
    const float fade_out_at = fx_float(shot.fade_out_at);
    if (scene.step == 1 && e.shot_step == 1 && e.shot_state == scene.state &&
        fade_out_at != 0.0f && timer <= fade_out_at && e.shot_timer > fade_out_at) {
        fade_out_start(ui, timer);
    }
    e.shot_state = scene.state;
    e.shot_step = scene.step;
    e.shot_timer = timer;
    if (scene.state == scene_ending_last && scene.step == 1 && timer <= signal_frames &&
        (world.step / 2) % 2 == 1) {
        for (const auto& [id, blend] : {std::pair{0xbf7, 0x1002}, std::pair{0xc02, 0x1003}}) {
            Sprite s = sprite_of(ui, id);
            s.pos[0] = 320.0f;
            s.pos[1] = 240.0f;
            s.blend = blend;
            s.flags |= 8;
            sprite_submit(ui, s);
        }
    }
}

} // namespace

// decomp: ending_scene_spawn 0x1C2BA
void ending_scene_spawn(FrontEnd& fe, Ui&) {
    fe.screen.campaign.ending_scene = EndingSceneScreen{};
}

void ending_scene_step(FrontEnd& fe, Ui& ui) {
    EndingSceneScreen& e = fe.screen.campaign.ending_scene;
    Campaign& c = fe.campaign;
    World& world = *fe.world;
    switch (fe.screen.step) {
    case 0: { // ending_scene_enter: Aqualine by day, rumble[2] on tournament 0 with the player's
              // entry kept, battle phase 3, a fade to black over 2 ticks
        world.story.rumble_selected = 4;
        const int slot = rumble_slot_of_selector(4);
        const RosterEntry player = world.rumble[slot].roster[0];
        fill_opponent_queues(world, fe.story_tables, fe.campaign.roster, slot, 0);
        world.rumble[slot].roster[0] = player;
        world.rumble[slot].roster[0].player = 0;
        for (int asset : ending_assets) {
            texture_set_load(ui, asset);
        }
        c.battle_phase = 3;
        c.paused = false;
        c.battle_wanted = 1;
        fade_out_start(ui, 2.0f);
        fe.screen.step = 1;
        break;
    }
    case 1: // ending_scene_enter_next: the shots until the last one's exit sets phase 6
        if (!c.battle_running) {
            break;
        }
        scene_text_step(fe, ui); // the shots' lines
        shots_follow(fe, ui, e);
        if (world.battle_phase == 6) {
            // the stopped battle stands for the captured frame (frame_to_texture_on)
            if (ui.renderer) {
                renderer_capture_frames(*ui.renderer, false);
            }
            c.paused = true;
            e.tint_start = ui.task_timer;
            ui_music(ui, ending_end_track, false);
            ui.fade = UiFade{}; // the frame shows as it was
            fe.screen.step = 2;
        }
        break;
    case 2: // ending_scene_enter_next2: the last frame switches off
        if (!tint_draw(ui, e.tint_start)) {
            e.counter = ui.frame_cadence * 2;
            fe.screen.step = 3;
        }
        break;
    case 3: // ending_scene_enter_next3: two seconds of black, then the fade
        black_quad(ui, -32, -32, 704, 544, 1, 0x1004);
        if (--e.counter < 0) {
            fade_out_start(ui, 0.0f);
            fe.screen.step = 4;
        }
        break;
    case 4: // ending_scene_enter_next4: the answer, on to the ending movie
        black_quad(ui, -32, -32, 704, 544, 1, 0x1004);
        ui_music_stop(ui);
        task_header_set(fe, 0);
        fe.screen.step = 5;
        break;
    default:
        black_quad(ui, -32, -32, 704, 544, 1, 0x1004);
        break;
    }
}

void ending_scene_destroy(FrontEnd& fe, Ui& ui) {
    // ending_scene_task_destroy: the textures, rumble_selected back to 0; the battle stops
    fe.campaign.battle_stop = true;
    fe.campaign.paused = false;
    fe.campaign.battle_phase = 0;
    fe.world->story.rumble_selected = 0;
    ui_music_stop(ui);
}

void ending_spawn(FrontEnd& fe, Ui& ui, int state) {
    switch (state) {
    case app_ending:
        movie_state_spawn(fe, ui, MovieScreenKind::ending);
        break;
    case app_credits:
        // state_credits_enter: game_beaten_set, the money word's top bit (kept by saves,
        // new_game_reset keeps it too)
        game_beaten_set(fe.world->story);
        movie_state_spawn(fe, ui, MovieScreenKind::credits);
        break;
    default:
        movie_state_spawn(fe, ui, MovieScreenKind::secret);
        break;
    }
}

void ending_step(FrontEnd& fe, Ui& ui) {
    movie_state_step(fe, ui);
}

void ending_destroy(FrontEnd& fe, Ui& ui) {
    movie_state_destroy(fe, ui);
}

FreePlaySetup free_play_setup(int lesson) {
    FreePlaySetup s;
    s.lesson = lesson;
    // fill_opponent_queues(0) for lesson 0 and plain free play, 0x37 for the attack and
    // camouflage lessons (tournament 55's rule)
    s.tournament = lesson == 0 || lesson == 3 ? 0 : 0x37;
    // the second fighter: lesson 2's camouflage target, the same build, AI, team 0xB
    s.second = lesson == 2;
    // actor_scene_entry_start: the tutorial scenes; plain free play enters through a gate
    switch (lesson) {
    case 1:
        s.scene = 0x23;
        break;
    case 2:
        s.scene = 0x2a;
        break;
    case 3:
        s.scene = -1;
        break;
    default:
        s.scene = 0x18;
        break;
    }
    // player_config_load_defaults_a for the lessons: their scripts name the default buttons
    s.default_pads = lesson != 3;
    return s;
}

// free_play_screen_spawn / _update: the setup, then the fight through the battle state's entry
// (src/screens/campaign/battle_screen.cpp, src/battle_run.cpp) in phase 4: rumble[3] on the setup's
// tournament, roster entry 0 the player in loadout 0x411 (the rental path), lesson 2's AI beside
// it. The pause menu continues or quits (free_play_screen_step3: the fade, then answer 0, the help
// menu); the lessons are their tutorial scenes (src/battle/battle_tutorial.cpp) with their lines
// (scene_text.cpp), and end free play with phase 6.
void free_play_spawn(FrontEnd& fe, Ui& ui) {
    fe.extra.free_play = free_play_setup(fe.menu.free_play_lesson);
    const FreePlaySetup& setup = fe.extra.free_play;
    World& world = *fe.world;
    const RosterTables& roster = fe.campaign.roster;
    world.story.rumble_selected = setup.selector;
    world.battle_state.free_play_lesson = setup.lesson;
    const int slot = rumble_slot_of_selector(setup.selector);
    fill_opponent_queues(world, fe.story_tables, roster, slot, setup.tournament);
    RumbleSlot& rumble = world.rumble[slot];
    RosterEntry entry;
    entry.pilot = pilot_row(roster, 0);
    entry.build = roster_build_row(roster, setup.build);
    entry.player = 0;
    rumble.roster[0] = entry;
    if (setup.second) {
        RosterEntry ai = entry;
        ai.player = -1;
        ai.ai_profile = 4;
        ai.pilot.team = static_cast<std::int16_t>(setup.second_team);
        rumble.roster[1] = ai;
    }
    // the entrants stay as the fill left them (free_play_screen_update sets none): one for
    // tournament 0, six for the lessons' 55, whose empty slots draw its drones as replacements
    BattleState& b = world.battle_state;
    b.player_gate[0] = 0;
    b.result = BattleResult{};
    b.result.rented = true;
    fe.campaign.rental_build = setup.build;
    fe.campaign.battle_phase = setup.phase;
    battle_screen_spawn(fe, ui);
}

void free_play_step(FrontEnd& fe, Ui& ui) {
    Campaign& c = fe.campaign;
    if (c.paused && fe.screen.step < 3) {
        // pause_menu_update's free play arm: continue or quit on player 0's pad; accept always
        // ends the pause, quit also sets phase 6 under the half-second fade (B does nothing).
        // The port's PC items follow (pause_settings.hpp)
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
            ui_sound(ui, bank_sys, se_pause); // pause_leave
            if (c.pause_cursor == 1) {
                fe.world->battle_phase = 6;
                battle_screen_quit(fe, ui);
                return;
            }
            c.paused = false;
            if (ui.audio) {
                pause_music(*ui.audio, 1);
                pause_sounds(*ui.audio, 1);
            }
        }
    } else if (c.battle_running && !c.paused && fe.screen.step == 2 &&
               fe.world->battle_phase == 6) {
        // free_play_screen_step3: the door, a death or the lesson's end set phase 6
        battle_screen_quit(fe, ui);
        return;
    }
    if (c.battle_running) {
        scene_text_step(fe, ui); // the lessons' lines
    }
    battle_screen_step(fe, ui);
}
