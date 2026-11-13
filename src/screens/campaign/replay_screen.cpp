// The replay's application state (5; decomp src/od/u_049970.c replay_spawn ..
// replay_spawn_begin_next3, src/od/u_049bf0.c replay_update, replay_screen_draw,
// replay_view_select): 16 updates of black, then the replay asked of main's battle
// session (src/battle_replay.cpp) under a second's fade in, its REPLAY frame and the camera view's
// name, the view picked with left and right; A (or the replay's end) fades out and answers 0, back
// to the result screen, which skips its count for a state 5 return.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/pc/music_panel.hpp"

namespace {

constexpr int replay_asset = 0x67f; // replay_tex.bin
constexpr int common_asset = 0x690; // scr_common_tex.bin
constexpr int replay_views = 6;

// replay_screen_draw: replay_sprites (and the row after them, up to sprite -1), then the view's
// name from replay_sprites_2
void replay_draw(Ui& ui, int view) {
    const std::vector<SpriteSlot> rows = ui_slot_run(ui, "replay", "replay_sprites");
    if (!rows.empty()) {
        sprite_rows_draw(ui, rows.data());
    }
    if (const SpriteSlot* names = ui_slots(ui.data, "replay", "replay_sprites_2", view)) {
        sprite_row_draw(ui, *names);
    }
}

} // namespace

void replay_screen_spawn(FrontEnd& fe, Ui&) {
    fe.screen.campaign.replay = ReplayScreen{};
    fe.campaign.replay_done = false;
    fe.campaign.replay_view = 5; // replay_start: the rails
}

void replay_screen_step(FrontEnd& fe, Ui& ui) {
    ReplayScreen& r = fe.screen.campaign.replay;
    Campaign& c = fe.campaign;
    switch (fe.screen.step) {
    case 0: // replay_spawn_wait: 16 updates, then replay_spawn_begin and replay_start
        if (--r.wait < 0) {
            texture_set_load(ui, common_asset);
            texture_set_load(ui, replay_asset);
            c.battle_wanted = 3;
            // frame_cadence * 0.5 * 2 ticks
            fade_in_start(ui, static_cast<float>(ui.frame_cadence));
            fe.screen.step = 1;
        }
        break;
    case 1: { // replay_update: the frame, the view, the ends; then replay_spawn_begin_next2
        if (!c.battle_running && !c.replay_done) {
            break; // main starts it on its next step
        }
        replay_draw(ui, c.replay_view);
        fight_music_update(fe, ui); // replay_spawn_begin_next2's battle_music_update
        // pads_cursor_move(player 0, 0..5, horizontal and wrapping on a press, no sound); the
        // mouse wheel steps it too
        int view = c.replay_view;
        menu_cursor_move(ui.pad, view, 0, replay_views, cursor_horizontal | cursor_wrap_press);
        // PC: and so does a click on the view's name (any other click is A)
        const SpriteSlot* name = ui_slots(ui.data, "replay", "replay_sprites_2", view);
        const PointerRect named =
            name ? sprite_rect(ui, name->sprite, name->x, name->y) : PointerRect{};
        if (ui.pointer.wheel != 0 || (ui.pointer.left && pointer_inside(ui.pointer, named))) {
            view = (view + (ui.pointer.wheel > 0 ? replay_views - 1 : 1)) % replay_views;
            ui.pointer.left = false;
        }
        c.replay_view = view;
        // the record's end, the fight's end, or A (sound 5)
        const bool accept = fade_is_idle(ui) && menu_accept(ui);
        if (accept) {
            ui_sound(ui, bank_sys, se_menu_accept);
        }
        if (accept || c.replay_done) {
            fade_out_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
            fe.screen.step = 2;
        }
        break;
    }
    case 2: // replay_spawn_begin_next3: the fade, then the answer
        if (c.battle_running) {
            replay_draw(ui, c.replay_view);
        }
        if (fade_is_black(ui)) {
            ui_music_stop(ui);
            task_header_set(fe, 0);
            fe.screen.step = 3;
        }
        break;
    default:
        break;
    }
}

void replay_screen_destroy(FrontEnd& fe, Ui& ui) {
    // replay_task_destroy: the music; the battle stops with the state (replay_end)
    ui_music_stop(ui);
    fe.campaign.battle_stop = true;
}
