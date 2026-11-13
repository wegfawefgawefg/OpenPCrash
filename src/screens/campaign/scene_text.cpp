// The battle's scene text as presentation plays it (decomp scene_text_start: the event
// interpreter with script_flags 0x80000001, no skip and quiet): the free play lessons' lines. The
// world asks for a script by its pointer's address in the lesson tables (BattleState::scene_text,
// battle_tutorial.cpp); this finds the script that address points at (data/scripts/bark.sexp's
// used-by), plays it on player 0's accept at the battle state's cadence, and says when it has ended
// (Campaign::scene_text_done, main's next Inputs). An address with no script ends at once.
#include "audio/sound_cues.hpp"
#include "cook/sexp_read.hpp"
#include "input/menu_input.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

#include <cstdio>
#include <map>

namespace {

// every address a bark-group script is pointed at from, by the script's name
const std::map<int, std::string>& scripts_by_address(Ui& ui) {
    static std::map<int, std::string> by_address;
    static bool loaded = false;
    if (loaded) {
        return by_address;
    }
    loaded = true;
    gsexp::ParseResult parsed;
    std::string error;
    if (!parse_sexp_file(ui.data.data_dir + "/scripts/bark.sexp", parsed, error)) {
        std::fprintf(stderr, "scene text: %s\n", error.c_str());
        return by_address;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node form = parsed.root(i);
        if (!form.is_list() || form.child_count() < 2 || !form.head().is_atom("script")) {
            continue;
        }
        const std::string name(form.child_at(1).text());
        for (gsexp::Node child : form.children()) {
            if (!child.is_list() || !child.head().is_atom("used-by")) {
                continue;
            }
            for (gsexp::Node use : child.children()) {
                if (use.is_list() && use.child_count() == 2 && use.head().is_atom("data")) {
                    by_address[static_cast<int>(std::strtol(std::string(use.child_at(1).text()).c_str(),
                                                            nullptr, 0))] = name;
                }
            }
        }
    }
    return by_address;
}

} // namespace

std::vector<int> scene_text_addresses(Ui& ui) {
    std::vector<int> out;
    for (const auto& [address, name] : scripts_by_address(ui)) {
        out.push_back(address); // a std::map's keys come sorted
    }
    return out;
}

void scene_text_step(FrontEnd& fe, Ui& ui) {
    Campaign& c = fe.campaign;
    const BattleState& b = fe.world->battle_state;
    SceneTextView& v = c.scene_text;
    if (b.scene_text_serial != v.serial && b.scene_text_alive) {
        // a new line asked for: it stops the one running (event_interp_stop)
        v.serial = b.scene_text_serial;
        v.running = false;
        const auto& names = scripts_by_address(ui);
        const auto found = names.find(b.scene_text);
        std::string error;
        std::printf("scene text 0x%x: %s (update %d)\n", b.scene_text,
                    found != names.end() ? found->second.c_str() : "none", fe.updates);
        if (found != names.end() &&
            load_talk_script(v.script, ui.data.font, ui.data.data_dir, found->second, error)) {
            // the window and portrait sprites draw from their own texture sets
            for (const TalkCommand& command : v.script.commands) {
                const std::size_t id = static_cast<std::size_t>(command.sprite);
                if (command.type == op_sprite && command.sprite >= 0 &&
                    id < ui.registry.sprites.size()) {
                    bind_texture_set(ui.slots, ui.registry, ui.registry.sprites[id].asset);
                }
            }
            talk_interp_start(v.interp, talk_no_skip | talk_quiet, ui.task_timer + 1);
            v.running = true;
        } else {
            c.scene_text_done = v.serial; // nothing to show
        }
    }
    if (!v.running || fe.campaign.paused) {
        return;
    }
    TalkPads pads;
    pads.accept = menu_accept(ui);
    pads.chord = text_skip_held(ui.pad);
    fe.talk_context.input_age = menu_input_age(ui);
    fe.talk_context.frame_cadence = ui.frame_cadence;
    std::vector<int> sounds;
    if (!talk_interp_step(v.interp, v.script, fe.talk_context, pads, ui.draw, sounds)) {
        v.running = false;
        c.scene_text_done = v.serial;
    }
    for (const int cue : sounds) {
        ui_sound(ui, bank_sys, cue);
    }
}
