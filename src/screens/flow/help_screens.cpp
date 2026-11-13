// The help menu, Pepper's talk (pepper_talk_* state 0x13, decomp src/hand/pepper_talk_o1.c and
// src/o1/u_028e51.c 0x291C7..0x2997E): the item tree, Pepper's portrait on the interpreter, the
// topics (conversation states), the lessons and free play (state 0x2A, which reads
// MenuGlobals::free_play_lesson), and End back to Club Wired or the town map.
#include "audio/sound_cues.hpp"
#include "input/menu_input.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/ui/screen_header.hpp"
#include "world/world.hpp"

#include <cstdio>

namespace {

// pepper_talk_textures: scr_common, talk_tex, club_beginner_tex
constexpr int help_textures[3] = {0x690, 0x641, 0x66a};
constexpr int top_page = 0x23;
constexpr int end_row = 0x22;
constexpr int pepper = 12; // portrait actor 12, mht37_pepper_tex
// portraits load at 0x216 + portrait_bias_table[cast slot] (talk_task.cpp)
constexpr int portrait_slot = 0x216;

int page_count(const HelpScreen& h, int page) {
    int n = 0;
    for (const HelpItem& item : h.items) {
        n += item.parent == page ? 1 : 0;
    }
    return n;
}

// the index-th row of a page, 35 when none
// decomp: pepper_talk_row 0x291E4
int page_row(const HelpScreen& h, int index, int page) {
    int seen = 0;
    for (int row = 0; row < static_cast<int>(h.items.size()); ++row) {
        if (h.items[static_cast<std::size_t>(row)].parent == page && seen++ == index) {
            return row;
        }
    }
    return static_cast<int>(h.items.size());
}

const HelpItem* item_at(const HelpScreen& h, int row) {
    return row >= 0 && row < static_cast<int>(h.items.size()) ? &h.items[static_cast<std::size_t>(row)]
                                                              : nullptr;
}

void load_items(Ui& ui, HelpScreen& h) {
    h.items.clear();
    gsexp::ParseResult parsed;
    std::string error;
    if (!ui_sexp(ui.data, "tables/pepper_talk.sexp", parsed, error)) {
        std::fprintf(stderr, "help: %s\n", error.c_str());
        return;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("item")) {
            continue;
        }
        HelpItem item;
        item.parent = form.get_int("parent").value_or(top_page);
        item.state = form.get_int("state").value_or(0x9f);
        item.label = utf8_to_glyphs(ui.data, form.get_string("label").value_or(""));
        h.items.push_back(item);
    }
}

// Pepper's script: portrait, a wait that never passes, end (the words at pepper_talk_script:
// type 4, actor 12, expression 2, at 329, 37, order 10, colour -1, flag 1 fade in)
// decomp: pepper_talk_interp_start 0xCE870
void pepper_start(FrontEnd& fe, Ui& ui, HelpScreen& h) {
    TalkScript& s = h.script;
    s = TalkScript{};
    s.name = "pepper_talk_script";
    TalkCommand portrait;
    portrait.type = op_portrait;
    portrait.actor = table_int(ui, "pepper_talk", "pepper_talk_script", 1, pepper);
    portrait.expression = table_int(ui, "pepper_talk", "pepper_talk_script", 2, 2);
    portrait.x = table_int(ui, "pepper_talk", "pepper_talk_script", 3, 329);
    portrait.y = table_int(ui, "pepper_talk", "pepper_talk_script", 4, 37);
    portrait.order = table_int(ui, "pepper_talk", "pepper_talk_script", 5, 10);
    portrait.color = static_cast<std::uint32_t>(table_int(ui, "pepper_talk", "pepper_talk_script", 6, -1));
    portrait.flags = static_cast<std::uint32_t>(table_int(ui, "pepper_talk", "pepper_talk_script", 7, 1));
    TalkCommand wait;
    wait.type = op_wait;
    wait.frames = -1;
    TalkCommand end;
    end.type = op_end;
    s.commands = {portrait, wait, end};
    // event_bind_actors(pepper_talk_cast "0C FF", the slot list): Pepper alone, cast slot 0
    for (int& slot : fe.talk_context.cast_slot) {
        slot = -1;
    }
    fe.talk_context.cast_slot[pepper] = 0;
    if (pepper < static_cast<int>(ui.registry.actors.size())) {
        bind_texture_set(ui.slots, ui.registry,
                         ui.registry.actors[static_cast<std::size_t>(pepper)].texture_asset,
                         portrait_slot + ui.registry.bias[0]);
    }
    talk_interp_start(h.interp, 0, ui.task_timer + 1);
}

// the interpreter redraws the portrait every frame: Pepper blinks, her mouth stays shut
void pepper_step(FrontEnd& fe, Ui& ui, HelpScreen& h) {
    fe.talk_context.input_age = menu_input_age(ui);
    fe.talk_context.frame_cadence = ui.frame_cadence;
    std::vector<int> sounds;
    talk_interp_step(h.interp, h.script, fe.talk_context, TalkPads{}, ui.draw, sounds);
}

// TalkList: the page's count and shown rows, the cursor's row (list_index_wrap)
void list_refresh(FrontEnd& fe, HelpScreen& h) {
    MenuGlobals& g = fe.menu;
    h.count = page_count(h, g.help_page);
    h.shown = h.count < 5 ? h.count : 5;
    const int depth = g.help_depth;
    h.cursor = h.count > 0 ? (g.help_top[depth] + g.help_sel[depth]) % h.count : 0;
}

// pepper_talk_label_draw over talk_list_each: the shown rows of the ring from the top one
void labels_draw(FrontEnd& fe, Ui& ui, const HelpScreen& h) {
    const int top = fe.menu.help_top[fe.menu.help_depth];
    for (int i = 0; i < h.shown; ++i) {
        const int row = page_row(h, h.count > 0 ? (top + i) % h.count : 0, fe.menu.help_page);
        if (const HelpItem* item = item_at(h, row)) {
            text_draw(ui, item->label, 0x47, i * 0x1e + 0x8e, 8, 18, 18, 0xffffffff, 0x7fffffff);
        }
    }
}

// decomp: pepper_talk_screen_draw 0x2925A
void screen_draw(FrontEnd& fe, Ui& ui, const HelpScreen& h, bool in_folder) {
    if (in_folder) {
        icon_on_plate_draw(ui, 0xe6, 0x11e, 0x101, 0xc3c, 0x14);
    }
    const int sel = fe.menu.help_sel[fe.menu.help_depth];
    sprite_draw_at(ui, 0x43a, 71.0f, static_cast<float>(sel * 0x1e + 0x8e), 7, 0xffffffff, 0x2000);
    day_draw(ui, *fe.world, 0x108);
    money_draw(ui, *fe.world, 0x108);
    sprite_rows_draw(ui, ui_slots(ui.data, "pepper_talk", "pepper_talk_sprites"));
    garage_banner_draw(ui, *fe.world, h.banner, 0x18f, 0x102);
    const std::vector<SpriteSlot> rows = ui_slot_run(ui, "pepper_talk", "pepper_talk_sprites_2");
    if (rows.size() >= 6) {
        // the list panel drawn 1.1 wide (D_0019B230), the frame
        Sprite panel = sprite_of(ui, rows[0].sprite);
        panel.pos[0] = rows[0].x;
        panel.pos[1] = rows[0].y;
        panel.blend = rows[0].blend;
        panel.scale[0] = 1.1f;
        sprite_submit(ui, panel);
        sprite_row_draw(ui, rows[1]);
        // the scroll arrows when the page has more than five rows, whatever the scroll
        if (h.count > h.shown) {
            sprite_rows_draw(ui, &rows[3]);
            sprite_rows_draw(ui, &rows[5]);
        }
    }
}

void children_update(FrontEnd& fe, Ui& ui, HelpScreen& h) {
    // the shuffled pool once an update (retail pumped it twice from here: docs/bugs.md)
    music_shuffle_pump(fe.music, ui, 1.0f, fe.updates);
    level_meter_update(ui, fe.screen.meter);
    pepper_step(fe, ui, h);
    now_playing_update(ui, fe.screen.strip);
}

void leave(FrontEnd& fe, Ui& ui, HelpScreen& h, int next) {
    fade_out_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
    h.next = next;
    fe.screen.step = 3;
}

// rows 0, 14, 15, 16 give lessons 3, 0, 1, 2; any other -1
// decomp: free_play_lesson_set 0x2920C
void lesson_set(FrontEnd& fe, int row) {
    fe.menu.free_play_lesson = row == 0 ? 3 : row == 0xe ? 0 : row == 0xf ? 1 : row == 0x10 ? 2 : -1;
}

// decomp: pepper_talk_menu_update 0x29507
void menu_update(FrontEnd& fe, Ui& ui, HelpScreen& h) {
    MenuGlobals& g = fe.menu;
    const int depth = g.help_depth;
    list_refresh(fe, h);
    // the pointer: a row hovered becomes the cursor (no scroll), as the d-pad's move
    PointerRect rows[5];
    for (int i = 0; i < 5; ++i) {
        rows[i] = sprite_rect(ui, 0x43a, 71.0f, static_cast<float>(i * 0x1e + 0x8e));
    }
    // and entering a row lights it, the scroll arrows (when the page has more rows) step it
    const std::vector<SpriteSlot> art = ui_slot_run(ui, "pepper_talk", "pepper_talk_sprites_2");
    const bool arrows = h.count > h.shown && art.size() >= 6;
    const PointerRect up =
        arrows ? sprite_rect(ui, art[3].sprite, art[3].x, art[3].y) : PointerRect{};
    const PointerRect down =
        arrows ? sprite_rect(ui, art[5].sprite, art[5].x, art[5].y) : PointerRect{};
    int moved = 0;
    menu_list_move(ui.pad, g.help_sel[depth], g.help_top[depth], 0, h.count, h.shown, 0x16, moved);
    if (moved == 0) {
        moved = menu_list_pointer(ui, g.help_sel[depth], g.help_top[depth], h.count, h.shown, 0x16,
                                  rows, arrows ? &up : nullptr, arrows ? &down : nullptr);
    }
    if (moved != 0) {
        ui_sound(ui, bank_sys, se_menu_cursor_vertical);
        menu_input_mark(ui);
    }
    list_refresh(fe, h);
    const int row = page_row(h, h.cursor, g.help_page);
    const HelpItem* item = item_at(h, row);
    const bool in_folder = item && item->parent != top_page;
    labels_draw(fe, ui, h);
    screen_draw(fe, ui, h, in_folder);
    const bool beaten = fe.world->story.game_beaten;
    if (menu_accept(ui, &rows[g.help_sel[depth] < 5 ? g.help_sel[depth] : 0])) {
        ui_sound(ui, bank_sys, se_menu_accept);
        if (row < end_row && item) {
            if (beaten && row == 0x21) {
                lesson_set(fe, row);
                leave(fe, ui, h, 0x80); // Mona Lisa, post-game
            } else if (beaten && row == 0xa) {
                lesson_set(fe, row);
                leave(fe, ui, h, 0x9d); // What is a Ranker?, post-game
            } else if (item->state != app_none) {
                // a topic (a conversation state), or free play and its lessons (0x2A)
                lesson_set(fe, row);
                leave(fe, ui, h, item->state);
            } else {
                // a folder: one deeper, its page the folder's row
                g.help_depth = depth + 1 < 3 ? depth + 1 : 2;
                g.help_page = row;
            }
        } else {
            // End: Club Wired, or the town map after the new game's name-entry talk (0x31)
            leave(fe, ui, h, g.help_came_from == 0x31 ? 0x29 : 8);
        }
    } else if (menu_cancel(ui) && item) {
        if (item->parent != top_page) {
            ui_sound(ui, bank_sys, se_menu_cancel);
            g.help_top[depth] = 0;
            g.help_sel[depth] = 0;
            const HelpItem* folder = item_at(h, item->parent);
            g.help_page = folder ? folder->parent : top_page;
            g.help_depth = depth > 0 ? depth - 1 : 0;
        } else if (g.help_sel[depth] != h.shown - 1 || g.help_top[depth] != h.count - h.shown) {
            // on the top page cancel jumps to End
            ui_sound(ui, bank_sys, se_menu_cursor_vertical);
            g.help_sel[depth] = h.shown - 1;
            g.help_top[depth] = h.count - h.shown;
        }
    }
}

} // namespace

void help_spawn(FrontEnd& fe, Ui&) {
    // pepper_talk_spawn: came_from unless a help topic (0x69..0x80) or free play (0x2A); retail
    // missed the post-game topic 0x9D, which then overwrote it (docs/bugs.md)
    const int from = fe.previous;
    const bool topic = (from >= 0x69 && from <= 0x80) || from == 0x9d;
    if (!topic && from != app_free_play) {
        fe.screen.help.from = from;
        fe.menu.help_came_from = from;
    }
}

void help_step(FrontEnd& fe, Ui& ui) {
    HelpScreen& h = fe.screen.help;
    switch (fe.screen.step) {
    case 0: { // pepper_talk_task
        for (const int asset : help_textures) {
            texture_set_load(ui, asset);
        }
        load_items(ui, h);
        level_meter_spawn(ui, fe.screen.meter, "level_meter_b");
        pepper_start(fe, ui, h);
        // the cursors come back from the session's globals; the page from the cursor's row
        list_refresh(fe, h);
        int moved = 0;
        MenuGlobals& g = fe.menu;
        menu_list_move(ui.pad, g.help_sel[g.help_depth], g.help_top[g.help_depth], 0, h.count,
                       h.shown, 0x16, moved);
        list_refresh(fe, h);
        if (const HelpItem* item = item_at(h, page_row(h, h.cursor, g.help_page))) {
            g.help_page = item->parent;
        }
        // music_scene_policy_spawn's now-playing strip at the hub height
        now_playing_spawn(ui, fe.screen.strip, 0, 0x153, 0x17);
        h.banner = -1;
        fade_in_start(ui, static_cast<float>(ui.frame_cadence) * 0.5f);
        fe.screen.step = 1;
        break;
    }
    case 1: // pepper_talk_fade_in
        labels_draw(fe, ui, h);
        screen_draw(fe, ui, h, false);
        if (fade_is_idle(ui)) {
            h.banner = static_cast<int>(ui.task_timer);
            fe.screen.step = 2;
        }
        break;
    case 2:
        menu_update(fe, ui, h);
        break;
    case 3: { // pepper_talk_leave
        list_refresh(fe, h);
        const HelpItem* item = item_at(h, page_row(h, h.cursor, fe.menu.help_page));
        labels_draw(fe, ui, h);
        screen_draw(fe, ui, h, item && item->parent != top_page);
        if (fade_is_black(ui)) {
            task_header_set(fe, h.next);
            fe.screen.step = 4;
        }
        break;
    }
    default:
        labels_draw(fe, ui, h);
        screen_draw(fe, ui, h, false);
        break;
    }
    children_update(fe, ui, h);
}

void help_destroy(FrontEnd& fe, Ui& ui) {
    // the cast unbound, the stream stopped
    for (int& slot : fe.talk_context.cast_slot) {
        slot = -1;
    }
    ui_music_stop(ui);
}
