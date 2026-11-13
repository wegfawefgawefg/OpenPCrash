#include "screens/flow/front_end.hpp"

#include "audio/audio.hpp"
#include "cook/sexp_read.hpp"
#include "input/menu_input.hpp"
#include "save/save.hpp"
#include "story/rumble.hpp"
#include "world/world.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

// the SE banks game_state_task loads at its first update (se_bank_load_resource)
constexpr const char* bank_names[4] = {"sys", "battle", "battle_old", "battle_player"};
// the default pilot name game_state_new copies in (D_0025BAF0 -> str.25bae0)
constexpr const char* default_pilot_name = "str.25bae0";

bool load_states(FrontEnd& fe, UiData& data, std::string& error) {
    gsexp::ParseResult parsed;
    if (!ui_sexp(data, "tables/states.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("state")) {
            continue;
        }
        const int id = form.get_int("id").value_or(-1);
        if (id < 0 || id >= app_state_count) {
            continue;
        }
        StateInfo& info = fe.states[static_cast<std::size_t>(id)];
        info.name = std::string(form.find_arg("name", 0).text());
        info.cadence = form.get_int("cadence").value_or(60);
        info.pause_allowed = form.get_int("pause_allowed").value_or(0) != 0;
        info.restart = atom_int(form.find_arg("restart", 0));
        info.music = form.get_int("music").value_or(-1);
        info.pad_pause = form.get_int("pad_pause").value_or(0);
    }
    return true;
}

GlyphText table_string(UiData& data, const char* id) {
    const TalkFont& font = data.font;
    gsexp::ParseResult parsed;
    std::string error;
    if (ui_sexp(data, "text/strings.sexp", parsed, error)) {
        for (std::size_t i = 0; i < parsed.root_count(); ++i) {
            const gsexp::Node form = parsed.root(i);
            if (form.is_list() && form.child_count() >= 3 && form.child_at(1).text() == id) {
                return glyphs_from_text(font, std::string(form.child_at(2).text()));
            }
        }
    }
    return {};
}

} // namespace

// PHANTOMCRASH_DEV_BUILDS=row,row... puts those used-Scoobee builds into garages 0.. and boards
// garage 0; PHANTOMCRASH_DEV_MONEY=N sets the money (units of 10 NY). For headless captures and
// runs: at boot and again after the title's New Game
void dev_world_seed(FrontEnd& fe) {
    World& w = *fe.world;
    if (const char* builds = std::getenv("PHANTOMCRASH_DEV_BUILDS")) {
        int garage = 0;
        for (const char* at = builds; *at && garage < garage_count;) {
            char* end = nullptr;
            const long row = std::strtol(at, &end, 10);
            if (end == at) {
                break;
            }
            if (row >= 0 && row < static_cast<long>(fe.shop.tables.builds.size())) {
                w.garages.unlocked |= 1u << garage;
                used_deliver(fe.shop.tables, w.garages,
                             fe.shop.tables.builds[static_cast<std::size_t>(row)], garage);
                ++garage;
            }
            at = *end == ',' ? end + 1 : end;
        }
        w.garages.selected = garage > 0 ? 0 : -1;
    }
    if (const char* money = std::getenv("PHANTOMCRASH_DEV_MONEY")) {
        w.story.money = std::atoi(money);
    }
    // PHANTOMCRASH_DEV_CARRY=durability,broken: the machine the ending carries (as the last
    // boss's defeat keeps it): its durability, a bit per broken mount, 3 rounds on the others
    if (const char* carry = std::getenv("PHANTOMCRASH_DEV_CARRY")) {
        EndingCarry& c = w.story.ending_carry;
        int broken = 0;
        std::sscanf(carry, "%d,%d", &c.durability, &broken);
        for (int m = 0; m < 4; ++m) {
            c.broken[m] = (broken >> m) & 1;
            c.fitted[m] = !c.broken[m];
            c.resource[m] = c.broken[m] ? Fx{} : fx(3);
        }
        c.carry = true;
    }
    // PHANTOMCRASH_DEV_DROP=legs,chip...: those parts off garage 0 (body, legs, arms, backs,
    // chip), back into the inventory, for captures of an incomplete machine
    if (const char* drop = std::getenv("PHANTOMCRASH_DEV_DROP")) {
        Garage& l = w.garages.garages[0];
        const std::string list = drop;
        const auto off = [&](std::int16_t& item) {
            if (item >= 0 && item < inventory_size) {
                w.garages.inventory[item].garage = -1;
            }
            item = -1;
        };
        if (list.find("body") != std::string::npos) {
            off(l.body);
        }
        if (list.find("legs") != std::string::npos) {
            off(l.leg);
        }
        if (list.find("arms") != std::string::npos) {
            off(l.arm[0]);
            off(l.arm[1]);
        }
        if (list.find("backs") != std::string::npos) {
            off(l.back[0]);
            off(l.back[1]);
        }
        if (list.find("chip") != std::string::npos) {
            off(l.chip);
        }
    }
    dev_story_seed(fe); // PHANTOMCRASH_DEV_WON, _BEATEN, _DAY (dev_seed.cpp)
}

bool init_front_end(FrontEnd& fe, Ui& ui, World& world, MachineOptions& machine,
                    std::string& error) {
    fe.world = &world;
    fe.machine = &machine;
    const std::string& data_dir = ui.data.data_dir;
    if (!load_states(fe, ui.data, error) ||
        !load_story_tables(fe.story_tables, data_dir, error)) {
        return false;
    }
    // game_state_new: the world's saved part to a new game's (story, garages, profile, rumble),
    // then the music player's part (the stock roll, the battle playlist)
    if (!load_music_catalogue(fe.music, ui.data, error)) {
        return false;
    }
    fe.states[app_pc_config].name = "pc_config";
    game_state_new(world, fe.story_tables);
    music_state_new(fe.music, world.story, world.profile.music);
    if (!load_roster_tables(fe.campaign.roster, data_dir, error)) {
        return false;
    }
    world.story_tables = &fe.story_tables;
    world.roster_tables = &fe.campaign.roster;
    // then rumble_contexts_reset (game_state_task)
    rumble_contexts_reset(world, fe.story_tables, fe.campaign.roster);
    // PHANTOMCRASH_SAVE_DIR overrides the per-user folder (headless runs)
    const char* save_dir = std::getenv("PHANTOMCRASH_SAVE_DIR");
    fe.save_dir = save_dir ? save_dir : save_dir_default();
    load_movie_files(fe.movies, data_dir);
    load_ending_subtitles(fe.subtitles, data_dir);
    fe.talk_context.font = &ui.data.font;
    fe.talk_context.registry = &ui.registry;
    fe.talk_context.pilot_name = table_string(ui.data, default_pilot_name);
    talk_pilot_name_update(fe);
    if (!load_part_tables(fe.shop.tables, ui.data, error)) {
        return false;
    }
    name_target_pilot(fe);
    dev_world_seed(fe);
    ui_close_screen_packs(ui.data); // the text pack, read once
    if (ui.audio) {
        for (int bank = 0; bank < 4; ++bank) {
            load_sound_bank(*ui.audio, bank, bank_names[bank]);
        }
        set_music_level(*ui.audio, machine.music_volume);
        set_sound_level(*ui.audio, machine.se_volume);
    }
    // game_init's one-unit fade to black, so the first frame isn't garbage
    fade_out_start(ui, 1.0f);
    fe.entering = app_phantagram_logo;
    return true;
}

void front_end_update(FrontEnd& fe, Ui& ui) {
    ++fe.updates;
    // tasks_run: the clock, then the AppState task (its entry or its update), then the screen
    ui.task_timer += static_cast<std::uint32_t>(60 / ui.frame_cadence);
    if (fe.updates == 1) {
        talk_chain_test(fe, ui);
    }
    ui_set_anchors(ui, fe.state);
    if (fe.entering >= 0) {
        const int state = fe.entering;
        fe.entering = -1;
        front_end_enter_state(fe, ui, state);
        ui_set_anchors(ui, fe.state);
    }
    front_end_update_state(fe, ui);
    // the lost-controller box blanks the screen's pads while it is up (pad_watch.cpp)
    pad_watch_frame(fe, ui);
    if (fe.screen_alive && !fe.screen_new) {
        front_end_step_screen(fe, ui);
    }
    pad_watch_draw(fe, ui);
    fe.screen_new = false;
    menu_screens_frame(fe, ui);
    // screen_fade_frame
    fade_frame(ui);
}

bool front_end_event(FrontEnd& fe, Ui& ui, const SDL_Event& event) {
    return pc_settings_event(fe, ui, event);
}

bool front_end_pads_blank(const FrontEnd&, const Ui& ui) {
    return ui.fade.state != 0;
}

void shutdown_front_end(FrontEnd& fe, Ui& ui) {
    front_end_destroy_screen(fe, ui);
    story_dump(fe);
}

// decomp: menu_set_state 0xA1B79
void menu_set_state(FrontEnd& fe, Ui& ui, int state) {
    front_end_destroy_screen(fe, ui);
    state = app_transition_resolve(fe.world->story, fe.story_tables, fe.state, state);
    state = talk_chain_skip(fe, ui, state);
    if (state < 0 || state >= app_state_count || state == app_none) {
        std::fprintf(stderr, "front end: no state 0x%x to enter\n", state);
        state = app_title_return;
    }
    // app_state_set_raw: the result cleared, frame_cadence_select, the entry next update
    fe.result = -1;
    ui.frame_cadence = fe.states[static_cast<std::size_t>(state)].cadence;
    fe.entering = state;
}

// decomp: name_entry_target_set 0x2C559
void name_target_set(FrontEnd& fe, int kind, std::int16_t* buffer) {
    if (kind < 0 || kind >= 4 || !buffer) {
        return;
    }
    fe.shop.name.kind = kind;
    fe.shop.name.buffer = buffer;
}

// decomp: name_entry_target_pilot 0x2C534
void name_target_pilot(FrontEnd& fe) {
    name_target_set(fe, 0, fe.world->profile.pilot_name);
}

// decomp: name_entry_target_garage 0x2C4A0
void name_target_garage(FrontEnd& fe) {
    const int g = fe.shop.editing;
    if (g >= 0 && g < garage_count) {
        name_target_set(fe, 1, fe.world->garages.garages[g].name);
    }
}

MachineOptions& game_options(FrontEnd& fe) {
    return *fe.machine;
}

void talk_pilot_name_update(FrontEnd& fe) {
    const Profile& profile = fe.world->profile;
    if (profile.pilot_name[0] < 0) {
        return; // keep the table's default
    }
    GlyphText name;
    for (const std::int16_t glyph : profile.pilot_name) {
        if (glyph < 0) {
            break;
        }
        name.push_back(glyph);
    }
    name.push_back(-1);
    fe.talk_context.pilot_name = name;
}

// decomp: state_music_play 0xA1AFC
void state_music_play(FrontEnd& fe, Ui& ui, int state) {
    const int track = fe.states[static_cast<std::size_t>(state)].music;
    if (track != -1) {
        ui_music(ui, track, true);
    } else {
        ui_music_stop(ui);
    }
}

bool menu_accept(Ui& ui, const PointerRect* rect) {
    if (menu_accept_pressed(ui.pad)) {
        return true;
    }
    return ui.pointer.left && (!rect || pointer_inside(ui.pointer, *rect));
}

bool menu_cancel(Ui& ui) {
    return menu_cancel_pressed(ui.pad) || ui.pointer.right;
}

// decomp: pads_cursor_move 0xDF5D0
int menu_cursor(Ui& ui, int& value, int lo, int count, unsigned flags, int bank, int cue,
                const PointerRect* rects) {
    int step = menu_cursor_move(ui.pad, value, lo, count, flags);
    if (step == 0 && rects && ui.pointer.moved) {
        const int hit = pointer_pick(ui.pointer, rects, count);
        if (hit >= 0 && lo + hit != value) {
            step = lo + hit > value ? 1 : -1;
            value = lo + hit;
        }
    }
    if (step != 0) {
        ui_sound(ui, bank, cue);
        menu_input_mark(ui);
    }
    return step;
}
