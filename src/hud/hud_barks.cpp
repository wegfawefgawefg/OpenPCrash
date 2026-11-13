// The bark window (decomp docs/port/hud.md "Barks and scene text", battle.md "Barks",
// progression.md "Barks and battle scene text"; src/od/u_0493f0.c). The world asks for barks
// (Fighter::barks, a bit per kind, every step's requests); here each request goes through
// actor_bark's speaker rule and chance (CRT rand, never the world's), the lowest kept kind
// per fighter is its bark_pending, and battle_barks_update picks one a step and starts its
// event script from the speaker's reaction profile. The battle's scene text (an Area Ranker's
// defeat line, scene_text_start) runs in the same interpreter. Retail's interpreter runs
// once an update and the pages' delays count updates, so the port runs it on odd steps and draws
// its last page in between.
#include "assets/pack_sexp.hpp"
#include "audio/sound_cues.hpp"
#include "cook/pack_library.hpp"
#include "cook/sexp_read.hpp"
#include "hud/hud_internal.hpp"
#include "story/story.hpp"

#include <cstdio>

namespace {

constexpr int no_bark = bark_kinds;

GlyphText name_glyphs(Ui& ui, const HudData& data, int identity) {
    if (identity < 0 || identity >= static_cast<int>(data.identity_name.size())) {
        return {};
    }
    return utf8_to_glyphs(ui.data, data.identity_name[static_cast<std::size_t>(identity)]);
}

int identity_sprite(const HudData& data, int identity) {
    return identity >= 0 && identity < static_cast<int>(data.identity_sprite.size())
               ? data.identity_sprite[static_cast<std::size_t>(identity)]
               : -1;
}

// the human's chip, the boarded garage's (-1 none)
int human_chip(const World& world, const Fighter& f) {
    const std::vector<BuildRecord>& builds = world.battle->parts.builds;
    if (f.build < 0 || f.build >= static_cast<int>(builds.size())) {
        return -1;
    }
    return builds[static_cast<std::size_t>(f.build)].chip;
}

// the speaker's reaction profile: an AI's pilot's `reaction`, a human's chip's `bark_profile`
int speaker_profile(const HudView& hud, const World& world, int slot) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    if (f.controller == -1) {
        return battle_roster_entry(world, slot).pilot.reaction;
    }
    const int chip = human_chip(world, f);
    return chip >= 0 && chip < static_cast<int>(hud.data.chip_bark_profile.size())
               ? hud.data.chip_bark_profile[static_cast<std::size_t>(chip)]
               : 0;
}

bool script_start(HudView& hud, Ui& ui, std::uint32_t address, std::uint32_t flags) {
    HudBarks& b = hud.barks;
    char name[24];
    std::snprintf(name, sizeof name, "bark_%x", address);
    std::string error;
    if (!load_talk_script(b.script, ui.data.font, ui.data.data_dir, name, error)) {
        std::fprintf(stderr, "hud: %s\n", error.c_str());
        return false;
    }
    talk_interp_start(b.interp, flags, ++b.random);
    b.page.quads.clear();
    b.running = true;
    return true;
}

// battle_barks_update's start: the speaker's names (Battle.bark_name_a, _b) and pictures, the
// script, sound 0x2D
void bark_start(HudView& hud, Ui& ui, const World& world, int slot, int kind, int profile) {
    HudBarks& b = hud.barks;
    if (profile < 0 || profile >= static_cast<int>(hud.data.reaction_barks.size()) || kind < 0 ||
        kind >= bark_kinds) {
        return;
    }
    const std::uint32_t address =
        hud.data.reaction_barks[static_cast<std::size_t>(profile)][static_cast<std::size_t>(kind)];
    if (address == 0) {
        return;
    }
    b.context = TalkContext{};
    b.context.font = &ui.data.font;
    b.context.registry = &ui.registry;
    b.context.frame_cadence = 30;
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    const RosterEntry& entry = battle_roster_entry(world, slot);
    if (f.controller == -1) {
        // an AI: its pilot's and its Scoobee's identities
        b.context.speaker_a = name_glyphs(ui, hud.data, entry.pilot.identity);
        b.context.speaker_b = name_glyphs(ui, hud.data, entry.pilot.scoobee_identity);
        b.context.identity_sprite_a = identity_sprite(hud.data, entry.pilot.identity);
        b.context.identity_sprite_b = identity_sprite(hud.data, entry.pilot.scoobee_identity);
    } else {
        // a human: the pilot's alias and the boarded chip's name (battle_chip_name_get), the
        // chip's identity picture as the second speaker's
        for (std::int16_t glyph : world.profile.pilot_name) {
            if (glyph < 0) {
                break;
            }
            b.context.speaker_a.push_back(glyph);
        }
        b.context.speaker_b = b.context.speaker_a;
        const Garages& g = world.garages;
        const int rental_chip = human_chip(world, f);
        if (world.battle_state.result.rented && rental_chip >= 0 &&
            rental_chip < static_cast<int>(hud.data.chip_identity.size())) {
            // rental_chip_name_get: the rental's chip's identity name, else the chip's
            // own name (else the alias stays)
            const std::size_t c = static_cast<std::size_t>(rental_chip);
            GlyphText name = name_glyphs(ui, hud.data, hud.data.chip_identity[c]);
            if (name.empty() && c < hud.data.chip_name.size() && !hud.data.chip_name[c].empty()) {
                name = utf8_to_glyphs(ui.data, hud.data.chip_name[c]);
            }
            if (!name.empty()) {
                b.context.speaker_b = name;
            }
        } else if (!world.battle_state.result.rented && g.selected >= 0 &&
                   g.selected < garage_count) {
            const int item = g.garages[g.selected].chip;
            if (item >= 0 && item < inventory_size && g.inventory[item].name[0] >= 0) {
                b.context.speaker_b.clear();
                for (std::int16_t glyph : g.inventory[item].name) {
                    if (glyph < 0) {
                        break;
                    }
                    b.context.speaker_b.push_back(glyph);
                }
            }
        }
        const int chip = human_chip(world, f);
        if (chip >= 0 && chip < static_cast<int>(hud.data.chip_identity.size())) {
            b.context.identity_sprite_b =
                identity_sprite(hud.data, hud.data.chip_identity[static_cast<std::size_t>(chip)]);
        }
    }
    if (script_start(hud, ui, address, talk_quiet)) {
        ui_sound(ui, bank_battle, se_radio_bark);
    }
}

void script_stop(HudBarks& b) {
    b.running = false;
    b.page.quads.clear();
}

// actor_bark for every kind the fighter asked this step, lowest first: the speaker rule
// (1 humans only, 2 fixed opponents only), the chance by speaker (CRT rand), kept if lower
// decomp: actor_bark 0x493F0
int kept_bark(HudView& hud, const World& world, int slot) {
    const Fighter& f = world.fighters[static_cast<std::size_t>(slot)];
    const BarkTables& t = hud.barks.tables;
    const bool human = f.controller != -1;
    const bool fixed = battle_roster_entry(world, slot).ticket.progress.role == 2;
    const float* chance = human ? t.chance_human : fixed ? t.chance_fixed : t.chance_ai;
    int kept = no_bark;
    for (int kind = 0; kind < bark_kinds; ++kind) {
        if (!(f.barks & (std::uint64_t{1} << kind))) {
            continue;
        }
        const int rule = t.speaker_rule[kind];
        if ((rule == 1 && !human) || (rule == 2 && !fixed)) {
            continue;
        }
        // rand_float_crt(1.0f) <= chance: an MSVC-style LCG, presentation only
        hud.barks.random = hud.barks.random * 214013u + 2531011u;
        const float roll = static_cast<float>((hud.barks.random >> 16) & 0x7FFF) / 32767.0f;
        if (roll <= chance[kind] && kind < kept) {
            kept = kind;
        }
    }
    return kept;
}

// only in a tournament's fight (not the attract or result screen's demo, a
// replay, free play, the test drive, VS), and not while fighter 0 lies in its death scene
// decomp: barks_allowed 0xC6D9A
bool barks_allowed(const HudView& hud, const World& world) {
    const BattleState& b = world.battle_state;
    if (b.menu_state != 6 || hud.scenes.vs_mode || world.battle_phase == 1 ||
        world.battle_phase == 2) {
        return false;
    }
    return !(world.fighters[0].present && b.slots[0].scene.state == 7);
}

bool ranker_bout(const World& world) {
    if (!world.story_tables) {
        return false;
    }
    const int t = world.story.lineup[world.battle_state.rumble & 3];
    return tournament_rule(*world.story_tables, t) == rule_area_ranker;
}

} // namespace

bool bark_tables_load(BarkTables& out, const Pack& core, std::string& error) {
    out = BarkTables{};
    gsexp::ParseResult parsed;
    if (!pack_sexp(core, "tables/barks.sexp", parsed, error)) {
        error += " (run pc_extract --only scenes)";
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node form = parsed.root(i);
        if (!form.is_list()) {
            continue;
        }
        const auto row = [&](float* values) {
            for (int k = 0; k < bark_kinds; ++k) {
                values[k] = atom_float(form.child_at(static_cast<std::size_t>(1 + k)));
            }
        };
        if (form.head().is_atom("speaker-rule")) {
            for (int k = 0; k < bark_kinds; ++k) {
                out.speaker_rule[k] = atom_int(form.child_at(static_cast<std::size_t>(1 + k)));
            }
        } else if (form.head().is_atom("chance-human")) {
            row(out.chance_human);
        } else if (form.head().is_atom("chance-fixed-opponent")) {
            row(out.chance_fixed);
        } else if (form.head().is_atom("chance-ai")) {
            row(out.chance_ai);
        } else if (form.head().is_atom("ranker-defeat-scripts")) {
            for (int a = 0; a < 3; ++a) {
                out.ranker_scripts[a] = atom_hex(form.child_at(static_cast<std::size_t>(1 + a)));
            }
        }
    }
    out.loaded = true;
    return true;
}

int scene_text_ticks(Ui& ui, std::uint32_t address) {
    if (address == 0) {
        return 0;
    }
    char name[24];
    std::snprintf(name, sizeof name, "bark_%x", address);
    TalkScript script;
    std::string error;
    if (!load_talk_script(script, ui.data.font, ui.data.data_dir, name, error)) {
        std::fprintf(stderr, "hud: %s\n", error.c_str());
        return 0;
    }
    TalkContext context;
    context.font = &ui.data.font;
    context.registry = &ui.registry;
    context.frame_cadence = 30;
    TalkInterp interp;
    talk_interp_start(interp, talk_no_skip | talk_quiet, 1);
    Draw2D draw;
    std::vector<int> sounds;
    // event_interp_enter runs the first step in the update it starts; then one an update
    int updates = 0;
    while (updates < (1 << 16) && talk_interp_step(interp, script, context, TalkPads{}, draw,
                                                   sounds)) {
        draw.quads.clear();
        sounds.clear();
        ++updates;
    }
    return 2 * (updates + 1);
}

void hud_barks_step(HudView& hud, Ui& ui, const World& world) {
    HudBarks& b = hud.barks;
    // the battle's scene text: a line over any bark, or a stop
    for (int e = 0; e < world.battle_state.event_count; ++e) {
        const BattleEvent& ev = world.battle_state.events[static_cast<std::size_t>(e)];
        if (ev.kind != BattleEventKind::SceneText) {
            continue;
        }
        script_stop(b);
        if (ev.a >= 0 && ev.a < 3 && b.tables.loaded) {
            b.context = TalkContext{};
            b.context.font = &ui.data.font;
            b.context.registry = &ui.registry;
            b.context.frame_cadence = 30;
            script_start(hud, ui, b.tables.ranker_scripts[ev.a], talk_no_skip | talk_quiet);
        }
    }
    // battle_barks_update
    if (b.tables.loaded && barks_allowed(hud, world)) {
        for (int slot = 0; slot < fighter_slots; ++slot) {
            b.pending[static_cast<std::size_t>(slot)] =
                world.fighters[static_cast<std::size_t>(slot)].present ? kept_bark(hud, world, slot)
                                                                       : no_bark;
        }
        // the running fighter with the lowest kind, the first on a tie
        int slot = -1, kind = no_bark, profile = 0;
        for (int i = 0; i < fighter_slots; ++i) {
            const int k = b.pending[static_cast<std::size_t>(i)];
            if (world.fighters[static_cast<std::size_t>(i)].present &&
                !world.battle_state.slots[i].parked && k < kind) {
                slot = i;
                kind = k;
                profile = speaker_profile(hud, world, i);
            }
        }
        const Fighter& first = world.fighters[0];
        const bool human_first = first.present && first.controller != -1;
        const int first_scene = world.battle_state.slots[0].scene.state;
        if (kind != bark_killed_new_boss && kind != bark_enter_new_boss &&
            (kind != bark_enter_beaten_boss || !ranker_bout(world))) {
            // fighter 0's entrance or door: only its own bark
            if (human_first && (first_scene == 6 || (first_scene >= 1 && first_scene < 5))) {
                kind = b.pending[0];
                slot = kind != no_bark ? 0 : -1;
                profile = kind != no_bark ? speaker_profile(hud, world, 0) : 0;
            }
        } else {
            script_stop(b); // these interrupt
        }
        if (human_first && first_scene >= 1 && first_scene <= 6) {
            if (slot != 0 || (kind != bark_door && kind != bark_door_at_25 && kind != bark_enter)) {
                kind = no_bark;
            } else if (kind == bark_door || kind == bark_door_at_25) {
                script_stop(b);
            }
        }
        // PC: "Battle chatter: Skip" starts none (barks change nothing)
        if (kind != no_bark && profile >= 0 && !b.running && !b.skipped) {
            bark_start(hud, ui, world, slot, kind, profile);
        }
    }
    b.pending.fill(no_bark);
    // the interpreter once an update
    if (b.running && world.step % 2 == 1) {
        b.page.quads.clear();
        std::vector<int> sounds;
        // player 0's accept finishes a typed line and the chord races through waits and delays
        // (script_flags 1); the scene text's 0x80000000 turns both off in the interpreter
        b.running = talk_interp_step(b.interp, b.script, b.context, b.pads, b.page, sounds);
        b.pads.accept = false;
        for (const int cue : sounds) {
            ui_sound(ui, 0, cue);
        }
        if (!b.running) {
            b.page.quads.clear();
        }
    }
}

void hud_draw_barks(HudView& hud, Ui& ui) {
    for (const Quad2D& quad : hud.barks.page.quads) {
        submit_quad(ui.draw, quad);
    }
}
