// The boot: the logo screen (logo_screen_* 0x28E51.., decomp src/o1/u_028e51.c), the movie states
// through src/movie's screens (the Phantagram and Dolby logos, the attract movie, the opening), and
// the attract demo (a demo battle in main's battle session).
#include "audio/audio.hpp"
#include "screens/flow/front_end.hpp"
#include "talk/draw2d.hpp"

namespace {

// logo_tex.bin (asset 0x675): its two cells, the company logo and the second one
constexpr int logo_asset = 0x675;
constexpr int first_logo = 0x5f2;
constexpr int second_logo = 0x5f3;

// logo_first_draw / logo_second_draw: centred at (320, 240), layer 0x101
void logo_draw(Ui& ui, int id) {
    Sprite sprite = sprite_of(ui, id);
    sprite.flags |= 8;
    sprite.pos[0] = 320.0f;
    sprite.pos[1] = 240.0f;
    sprite.blend = 0x101;
    sprite_submit(ui, sprite);
}

int half_second(const Ui& ui) {
    return static_cast<int>(static_cast<float>(ui.frame_cadence) * 0.5f);
}

} // namespace

void logo_spawn(FrontEnd& fe, Ui&) {
    fe.screen.step = 0;
}

void logo_step(FrontEnd& fe, Ui& ui) {
    int& counter = fe.screen.logo.counter;
    int& step = fe.screen.step;
    switch (step) {
    case 0: // logo_screen_update: the cabinet cache allowed, then step2
        step = 1;
        break;
    case 1: // step2: the sheet, every pad player 0's, half a second of fade in
        texture_set_load(ui, logo_asset);
        counter = half_second(ui);
        fade_in_start(ui, static_cast<float>(counter));
        step = 2;
        break;
    case 2: // step3: shown for the fade in
        logo_draw(ui, first_logo);
        if (--counter < 0 && fade_is_idle(ui)) {
            counter = ui.frame_cadence * 2;
            step = 3;
        }
        break;
    case 3: // step4: two seconds, or accept cuts to black at once
        logo_draw(ui, first_logo);
        if (menu_accept(ui)) {
            counter = 0;
            fade_out_start(ui, 0.0f);
        } else {
            if (--counter >= 0) {
                break;
            }
            counter = half_second(ui);
            fade_out_start(ui, static_cast<float>(counter));
        }
        step = 4;
        break;
    case 4: // step5: after the fade, the second logo, unless it has been shown since boot
        logo_draw(ui, first_logo);
        if (counter >= 0) {
            --counter;
        }
        if (counter < 0) {
            if (fe.second_logo_shown) {
                task_header_set(fe, 1);
            } else {
                counter = half_second(ui);
                fade_in_start(ui, static_cast<float>(counter));
                step = 5;
            }
        }
        break;
    case 5: // step6
        logo_draw(ui, second_logo);
        fe.second_logo_shown = true;
        if (--counter < 0) {
            counter = ui.frame_cadence * 2;
            step = 6;
        }
        break;
    case 6: // step7: accept counts only in the last second
        logo_draw(ui, second_logo);
        fe.second_logo_shown = true;
        if (static_cast<float>(counter) < static_cast<float>(ui.frame_cadence) && menu_accept(ui)) {
            counter = 0;
            fade_out_start(ui, 0.0f);
        } else {
            if (--counter >= 0) {
                break;
            }
            counter = half_second(ui);
            fade_out_start(ui, static_cast<float>(counter));
        }
        step = 7;
        break;
    case 7: // step8
        logo_draw(ui, second_logo);
        fe.second_logo_shown = true;
        if (counter >= 0) {
            --counter;
        }
        if (counter < 0) {
            task_header_set(fe, 0);
            step = 8;
        }
        break;
    default:
        logo_draw(ui, second_logo);
        break;
    }
}

void logo_destroy(FrontEnd&, Ui&) {
    // texture_set_release(0x675): the slot keeps its pixels
}

void movie_state_spawn(FrontEnd& fe, Ui&, MovieScreenKind kind) {
    start_movie_screen(fe.screen.movie, kind);
}

void movie_state_step(FrontEnd& fe, Ui& ui) {
    if (!ui.audio) {
        task_header_set(fe, 0); // no sound engine, no movie clock: skipped
        return;
    }
    MovieScreenInput input;
    input.accept = menu_accept(ui);
    // attract_input_any: a fresh press of any button (0xFFF0), or a click
    input.any = (ui.pad.b.rising & 0xfff0) != 0 || ui.pointer.left || ui.pointer.right;
    input.fade_black = fade_is_black(ui);
    const MovieScreenStep s =
        step_movie_screen(fe.screen.movie, fe.movies, *ui.audio, input, fe.subtitles, 1.0 / 60.0);
    if (s.fade_reset) {
        ui.fade = UiFade{};
    }
    if (s.fade_out >= 0.0f) {
        fade_out_start(ui, s.fade_out, static_cast<std::uint32_t>(s.fade_out_mode));
    }
    if (s.fade_in >= 0.0f) {
        fade_in_start(ui, s.fade_in);
    }
    if (s.draw) {
        // the picture into slot 0x225 (movie_draw_rect), drawn 640x480 at layer 0x104 with texel
        // coordinates (flag 2)
        const MovieImage& image = fe.movies.image;
        if (!image.rgba.empty()) {
            if (ui.movie.texture < 0 || ui.movie.width != image.width ||
                ui.movie.height != image.height) {
                ui.movie.texture = renderer_upload_sprite_texture(*ui.renderer, image.width,
                                                                  image.height, image.rgba);
                ui.movie.width = image.width;
                ui.movie.height = image.height;
                ui.movie_serial = image.serial;
            } else if (image.serial != ui.movie_serial) {
                renderer_update_sprite_texture(*ui.renderer, ui.movie.texture, image.rgba);
                ui.movie_serial = image.serial;
            }
            Sprite sprite;
            sprite.tex.slot = movie_slot;
            sprite.tex.rect[2] = static_cast<float>(image.width);
            sprite.tex.rect[3] = static_cast<float>(image.height);
            sprite.tex.sheet[0] = static_cast<float>(image.width);
            sprite.tex.sheet[1] = static_cast<float>(image.height);
            sprite.blend = 0x104;
            sprite.flags = 2;
            sprite_submit(ui, sprite);
        }
        // ending_movie_subtitles_draw: the row centred on x 320 at y 416, layer 0x105
        if (s.subtitle >= 0 && s.subtitle < static_cast<int>(fe.subtitles.size())) {
            const GlyphText text = utf8_to_glyphs(
                ui.data, fe.subtitles[static_cast<std::size_t>(s.subtitle)].text);
            text_draw(ui, text, 0x140 - text_width(ui, text) / 2, 0x1a0, 4, 18, 18, 0xfff0f0f0,
                      0x80);
        }
    }
    if (s.done) {
        task_header_set(fe, s.result);
    }
}

void movie_state_destroy(FrontEnd& fe, Ui& ui) {
    if (ui.audio) {
        end_movie_screen(fe.screen.movie, fe.movies, *ui.audio);
    }
}

void attract_demo_step(FrontEnd& fe, Ui& ui) {
    // attract_demo_enter .. attract_demo_enter_next4: a minute's AI-only demo
    // battle (main's battle session, as the result screen's) on one of the six Class A
    // tournaments (attract_demo_tournaments) in the last area, 12SENTOU.WAV, the blinking caption;
    // the minute out fades back to the boot (0), any press to the title return (1). Retail fills
    // the save's context for good; the demo battle puts it back
    Campaign& c = fe.campaign;
    World& world = *fe.world;
    switch (fe.screen.step) {
    case 0: {
        c.attract_left = ui.frame_cadence * 60;
        int& selector = world.story.rumble_selected;
        if (selector == 6 || selector == 7) {
            selector = 0;
        }
        const std::vector<double> tournaments =
            ui_values(ui.data, "attract", "attract_demo_tournaments", 6);
        const int pick = rand_table_next(world.random) % 6;
        c.demo_tournament = tournaments.size() == 6
                                ? static_cast<int>(tournaments[static_cast<std::size_t>(pick)])
                                : -1;
        c.battle_wanted = 2;
        fe.screen.step = 1;
        break;
    }
    case 1: // attract_demo_enter_next: the battle up, its music and the fade in
        if (c.battle_running || c.battle_wanted == 0) {
            ui_music(ui, 5, false);
            fade_in_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
            fe.screen.step = 2;
        }
        break;
    case 2: // _next2: until the fade is done
        if (fade_is_idle(ui)) {
            fe.screen.step = 3;
        }
        break;
    case 3: { // _next3: the caption, the minute, or a press
        Sprite caption = sprite_of(ui, 0xe91);
        caption.pos[0] = 225.0f;
        caption.pos[1] = 336.0f;
        caption.flags |= sprite_blink_hard;
        caption.blend = 0x108;
        sprite_submit(ui, caption);
        if (--c.attract_left <= 0) {
            fade_out_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
            fe.screen.step = 4;
        } else if ((ui.pad.b.rising & 0xfff0) != 0 || ui.pointer.left || ui.pointer.right) {
            fade_out_start(ui, 0.0f);
            task_header_set(fe, 1);
            fe.screen.step = 5;
        }
        break;
    }
    case 4: // _next4: black, then back to the boot
        if (fade_is_black(ui)) {
            ui_music_stop(ui);
            task_header_set(fe, 0);
            fe.screen.step = 5;
        }
        break;
    default:
        break;
    }
}

void attract_demo_destroy(FrontEnd& fe, Ui& ui) {
    // attract_demo_screen_destroy: the next demo in the next area (D_001D6F34), the
    // stream stopped; the demo battle stops with the state
    World& world = *fe.world;
    const std::vector<double> next = ui_values(ui.data, "attract", "attract_next_areas", 8);
    int& selector = world.story.rumble_selected;
    if (next.size() == 8 && selector >= 0 && selector < 8) {
        selector = static_cast<int>(next[static_cast<std::size_t>(selector)]);
    }
    fe.campaign.battle_stop = true;
    fe.campaign.demo_tournament = -1;
    ui_music_stop(ui);
}
