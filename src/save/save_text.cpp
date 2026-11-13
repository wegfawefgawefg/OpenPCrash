// A save image as .sexp text: every field named, one form per garage, item, pool, queue and roster
// entry, so two saves diff line by line (docs/saves.md, "The PC save"). save_parse.cpp reads it.
#include "save/save.hpp"
#include "save/save_names.hpp"

#include <ctime>
#include <gsexp/sexp.hpp>

namespace {

struct Out {
    std::string text;
};

void put(Out& out, std::string_view s) {
    out.text += s;
}
void put(Out& out, long long n) {
    out.text += std::to_string(n);
}
// " (name value)"
void field(Out& out, std::string_view name, long long value) {
    put(out, " (");
    put(out, name);
    put(out, " ");
    put(out, value);
    put(out, ")");
}
void flag(Out& out, std::string_view name, bool value) {
    put(out, " (");
    put(out, name);
    put(out, value ? " true)" : " false)");
}
void named(Out& out, std::string_view name, std::string_view value) {
    put(out, " (");
    put(out, name);
    put(out, " ");
    put(out, value);
    put(out, ")");
}
// " (name a b c ...)"
template <typename T> void list(Out& out, std::string_view name, const T* values, int count) {
    put(out, " (");
    put(out, name);
    for (int i = 0; i < count; ++i) {
        put(out, " ");
        put(out, static_cast<long long>(values[i]));
    }
    put(out, ")");
}
// a glyph name up to its -1 ("(name)" when empty)
void glyphs(Out& out, std::string_view name, const std::int16_t* glyph, int length) {
    int count = 0;
    while (count < length && glyph[count] != -1) {
        ++count;
    }
    list(out, name, glyph, count);
}

std::string iso_time(std::int64_t seconds) {
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

// --- story ---

void write_story(Out& out, const Story& story) {
    put(out, "\n;; progress (src/story/story.hpp)\n(story");
    field(out, "day", story.day);
    field(out, "money", story.money);
    flag(out, "beaten", story.game_beaten);
    field(out, "cycles", story.completed_cycles);
    field(out, "days-played", story.days_played);
    field(out, "area-selected", story.rumble_selected);
    field(out, "map-spin-money", story.map_spin_money);
    put(out, "\n ");
    list(out, "lineup", story.lineup, 4);
    // the trigger rows done, by row
    put(out, "\n  (events-done");
    for (int row = 0; row < 160; ++row) {
        if (story.events_done[row / 32] & (1u << (row % 32))) {
            put(out, " ");
            put(out, row);
        }
    }
    put(out, ")\n  (songs");
    for (int song = 0; song < music_owned_count_max; ++song) {
        const int owned = story.music_owned[song];
        if (owned != 0) {
            put(out, " (");
            put(out, song);
            put(out, " ");
            put(out, song_state_name(owned));
            put(out, ")");
        }
    }
    put(out, ")\n ");
    list(out, "tournament-won", story.tournament_won, tournament_count);
    put(out, ")\n");

    // pools: the primary's byte, then the eight alternates'
    put(out, "\n;; (pool id primary alternate0..7): 0 not beaten, 1 beaten, -1 nobody there\n(pools");
    for (int i = 0; i < pool_count; ++i) {
        const PoolProgress& pool = story.pools[i];
        put(out, "\n  (pool ");
        put(out, i);
        put(out, " ");
        put(out, pool.primary);
        for (const std::int8_t alternate : pool.alternates) {
            put(out, " ");
            put(out, alternate);
        }
        put(out, ")");
    }
    put(out, ")\n");
}

// --- garages ---

void write_garages(Out& out, const Garages& garages) {
    put(out, "\n;; garages and what the player owns (src/world/game_state.hpp)\n(garages");
    field(out, "selected", garages.selected);
    put(out, " (unlocked");
    for (int g = 0; g < 32; ++g) {
        if (garages.unlocked & (1u << g)) {
            put(out, " ");
            put(out, g);
        }
    }
    put(out, ")");
    // owned flags: an id for 1, (id value) for anything else
    const auto owned = [&out](std::string_view name, const std::uint8_t* flags, int count) {
        put(out, "\n  (");
        put(out, name);
        for (int i = 0; i < count; ++i) {
            if (flags[i] == 1) {
                put(out, " ");
                put(out, i);
            } else if (flags[i] != 0) {
                put(out, " (");
                put(out, i);
                put(out, " ");
                put(out, flags[i]);
                put(out, ")");
            }
        }
        put(out, ")");
    };
    owned("colors-owned", garages.colors_owned, color_count);
    owned("stickers-owned", garages.stickers_owned, sticker_count);
    put(out, ")\n");

    for (int g = 0; g < garage_count; ++g) {
        const Garage& garage = garages.garages[g];
        put(out, "(garage ");
        put(out, g);
        glyphs(out, "name", garage.name, glyph_name_length);
        field(out, "body", garage.body);
        field(out, "leg", garage.leg);
        list(out, "arm", garage.arm, 2);
        list(out, "back", garage.back, 2);
        field(out, "chip", garage.chip);
        list(out, "option", garage.option, 10);
        field(out, "color", garage.color);
        field(out, "sticker", garage.sticker);
        put(out, ")\n");
    }

    // inventory: the slots that differ from a free one, by index
    put(out, "\n;; inventory slots in use, by index; the garages' numbers point here\n");
    const InventoryItem empty;
    for (int i = 0; i < inventory_size; ++i) {
        const InventoryItem& item = garages.inventory[i];
        bool named_item = item.name[0] != -1;
        bool same = !named_item && item.garage == empty.garage && item.tune == empty.tune &&
                    item.kind == empty.kind && item.part == empty.part;
        if (same) {
            continue;
        }
        put(out, "(item ");
        put(out, i);
        named(out, "kind", part_kind_name(item.kind));
        field(out, "part", item.part);
        field(out, "tune", item.tune);
        field(out, "garage", item.garage);
        if (named_item) {
            glyphs(out, "name", item.name, glyph_name_length);
        }
        put(out, ")\n");
    }
}

// --- profile ---

void write_profile(Out& out, const Profile& profile, const MachineOptions& machine) {
    put(out, "\n;; the pilot, records, music player, options and button config\n(pilot");
    glyphs(out, "name", profile.pilot_name, pilot_name_length);
    put(out, ")\n(records");
    const Records& r = profile.records;
    field(out, "total-earned", r.total_earned);
    field(out, "best-payout", r.best_payout);
    field(out, "battles", r.battles);
    field(out, "wins", r.wins);
    field(out, "kills", r.kills);
    field(out, "drones-shot-down", r.drones_shot_down);
    put(out, "\n ");
    field(out, "play-time", r.play_time);
    field(out, "longest-battle", r.longest_battle);
    field(out, "shortest-battle", r.shortest_battle);
    field(out, "saves", r.save_count);
    put(out, "\n ");
    field(out, "enemy-weapons-destroyed", r.enemy_weapons_destroyed);
    field(out, "weapons-destroyed", r.weapons_destroyed);
    field(out, "play-clock", r.play_clock);
    put(out, ")\n");

    const Music& music = profile.music;
    put(out, "(music");
    field(out, "selected", music.playlist_selected);
    flag(out, "random", music.playlist_random);
    list(out, "sonic-amp-stock", music.sonic_amp_stock, sonic_amp_stock_size);
    for (int p = 0; p < playlist_count; ++p) {
        put(out, "\n  (playlist ");
        put(out, p);
        glyphs(out, "name", music.playlist_names[p], glyph_name_length);
        int length = playlist_length;
        while (length > 0 && music.playlists[p][length - 1] == -1) {
            --length;
        }
        list(out, "songs", music.playlists[p], length);
        put(out, ")");
    }
    put(out, ")\n");

    const MachineOptions& o = machine;
    put(out, "(options");
    field(out, "music-volume", o.music_volume);
    field(out, "se-volume", o.se_volume);
    const int shift[2] = {o.screen_shift_x, o.screen_shift_y};
    list(out, "screen-shift", shift, 2);
    field(out, "vibration", o.vibration);
    field(out, "flag-31", o.flag_31);
    field(out, "camera-view", profile.options.camera_view);
    put(out, ")\n");

    const ButtonConfig& b = profile.options.buttons;
    put(out, "(buttons");
    named(out, "control", b.control == 0 ? "standard" : b.control == 1 ? "preferred"
                                                                       : std::to_string(b.control));
    named(out, "view-change", b.view_change == 0   ? "normal"
                              : b.view_change == 1 ? "reverse"
                                                   : std::to_string(b.view_change));
    field(out, "beginner", b.beginner);
    put(out, "\n ");
    for (int k = 0; k < button_action_count; ++k) {
        named(out, button_action_name(k), pad_button_name(b.keys[k]));
    }
    put(out, ")\n");
}

// --- today's tournaments ---

void write_ticket(Out& out, const RumbleTicket& ticket) {
    put(out, "(ticket ");
    put(out, ticket.opponent);
    put(out, " ");
    put(out, ticket_role_name(ticket.progress.role));
    if (ticket.progress.tournament >= 0) {
        field(out, "tournament", ticket.progress.tournament);
    } else {
        put(out, " (pool ");
        put(out, ticket.progress.pool);
        if (ticket.progress.alternate >= 0) {
            put(out, " ");
            put(out, ticket.progress.alternate);
        }
        put(out, ")");
    }
    put(out, ")");
}

std::string queue_text(const OpponentQueue& queue, int index) {
    Out out;
    put(out, "\n  (queue ");
    put(out, index);
    field(out, "drawable", queue.drawable);
    field(out, "count", queue.count);
    field(out, "out", queue.out);
    field(out, "ready", queue.ready);
    for (int t = 0; t < queue.count && t < queue_capacity; ++t) {
        put(out, "\n    ");
        write_ticket(out, queue.tickets[t]);
    }
    put(out, ")");
    return out.text;
}

std::string roster_text(const RosterEntry& entry, int index) {
    Out out;
    put(out, "\n  (roster ");
    put(out, index);
    field(out, "player", entry.player);
    named(out, "name", gsexp::quote_string(entry.name));
    field(out, "level", entry.level);
    field(out, "ai-profile", entry.ai_profile);
    field(out, "value", entry.destruction_value);
    field(out, "queue", entry.queue);
    put(out, "\n    ");
    write_ticket(out, entry.ticket);
    const RosterBuild& b = entry.build;
    put(out, "\n    (build");
    field(out, "f_00", b.unused_00);
    put(out, " (body ");
    put(out, b.body);
    put(out, " ");
    put(out, b.body_tune);
    put(out, ") (leg ");
    put(out, b.leg);
    put(out, " ");
    put(out, b.leg_tune);
    put(out, ") (weapons");
    for (const RosterWeapon& weapon : b.weapon) {
        put(out, " (");
        put(out, weapon.id);
        put(out, " ");
        put(out, weapon.tune);
        put(out, ")");
    }
    put(out, ") (chip ");
    put(out, b.chip);
    put(out, " ");
    put(out, b.chip_tune);
    put(out, ")");
    field(out, "color", b.color);
    field(out, "sticker", b.sticker);
    list(out, "option", b.option, 10);
    put(out, ")\n    (pilot");
    const RosterPilot& p = entry.pilot;
    field(out, "identity", p.identity);
    field(out, "scoobee-identity", p.scoobee_identity);
    field(out, "team", p.team);
    field(out, "f_06", p.unused_06);
    const long long curve[5] = {p.value_base, p.value_at_mid, p.value_at_max, p.value_mid,
                                p.value_negative};
    list(out, "destruction-value", curve, 5);
    field(out, "reaction", p.reaction);
    field(out, "f_1a", p.unused_1a);
    put(out, ")\n    (start");
    put(out, entry.start_set ? " true " : " false ");
    put(out, entry.start.x.raw_value());
    put(out, " ");
    put(out, entry.start.y.raw_value());
    put(out, " ");
    put(out, entry.start.z.raw_value());
    put(out, "))");
    return out.text;
}

void write_rumble(Out& out, const RumbleSlot& slot, int index) {
    put(out, "(rumble ");
    put(out, index);
    field(out, "regular-quota", slot.regular_quota);
    field(out, "entrants", slot.entrants);
    flag(out, "fixed-queued", slot.fixed_queued);
    put(out, " (pool-queued");
    for (const bool queued : slot.pool_queued) {
        put(out, queued ? " 1" : " 0");
    }
    put(out, ")");
    // queues and roster entries left as a refill leaves them aren't written
    const OpponentQueue empty_queue;
    const RosterEntry empty_entry;
    for (int q = 0; q < 3; ++q) {
        const std::string text = queue_text(slot.queues[q], q);
        if (text != queue_text(empty_queue, q)) {
            put(out, text);
        }
    }
    for (int r = 0; r < roster_size; ++r) {
        const std::string text = roster_text(slot.roster[r], r);
        if (text != roster_text(empty_entry, r)) {
            put(out, text);
        }
    }
    put(out, ")\n");
}

} // namespace

std::string save_text(const SaveImage& image) {
    Out out;
    put(out, ";; Phantom Crash save (docs/saves.md): the fields of the original's save, by name.\n");
    put(out, "(save");
    field(out, "version", save_version);
    named(out, "written", gsexp::quote_string(iso_time(image.written)));
    put(out, ")\n");
    write_story(out, image.story);
    write_garages(out, image.garages);
    write_profile(out, image.profile, image.machine);
    put(out, "\n;; today's RumbleContexts (the tournament is story's lineup): queues and rosters\n");
    for (int s = 0; s < rumble_count; ++s) {
        write_rumble(out, image.rumble[s], s);
    }
    return out.text;
}
