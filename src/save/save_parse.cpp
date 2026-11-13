// A save's .sexp text back into an image (save_text.cpp writes it). Every form and field the writer
// always writes must be there; inventory slots, queues and roster entries it leaves out are free
// or as a refill leaves them. Anything missing, out of range or of another version fails the read
// as a damaged save, the way retail's checksum test fails a slot.
#include "save/save.hpp"
#include "save/save_read.hpp"

#include <cstdio>
#include <ctime>

using namespace save_read;

namespace {


std::int64_t parse_iso_time(const std::string& text) {
    std::tm utc{};
    if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d", &utc.tm_year, &utc.tm_mon, &utc.tm_mday,
                    &utc.tm_hour, &utc.tm_min, &utc.tm_sec) != 6) {
        return 0;
    }
    utc.tm_year -= 1900;
    utc.tm_mon -= 1;
#ifdef _WIN32
    return static_cast<std::int64_t>(_mkgmtime(&utc));
#else
    return static_cast<std::int64_t>(timegm(&utc));
#endif
}

// --- story ---

bool read_story(In& in, FormView form, Story& story) {
    bool ok = field(in, form, "day", story.day) && field(in, form, "money", story.money) &&
              flag(in, form, "beaten", story.game_beaten) &&
              field(in, form, "cycles", story.completed_cycles) &&
              field(in, form, "days-played", story.days_played) &&
              field(in, form, "area-selected", story.rumble_selected) &&
              field(in, form, "map-spin-money", story.map_spin_money) &&
              list(in, form, "lineup", story.lineup, 4, -1, true) &&
              list(in, form, "tournament-won", story.tournament_won, tournament_count,
                   std::int8_t{-1}, true);
    if (!ok) {
        return false;
    }
    for (std::uint32_t& word : story.events_done) {
        word = 0;
    }
    const Node events = form.find("events-done");
    if (!events.valid()) {
        return fail(in, "missing (events-done)");
    }
    for (std::size_t i = 1; i < events.child_count(); ++i) {
        long long row = 0;
        if (!number(in, events.child_at(i), row) || !index_in(in, row, 160, "event row")) {
            return false;
        }
        story.events_done[row / 32] |= 1u << (row % 32);
    }
    for (std::uint8_t& owned : story.music_owned) {
        owned = 0;
    }
    const Node songs = form.find("songs");
    if (!songs.valid()) {
        return fail(in, "missing (songs)");
    }
    for (std::size_t i = 1; i < songs.child_count(); ++i) {
        const Node song = songs.child_at(i);
        long long id = 0;
        int state = 0;
        if (!number(in, song.child_at(0), id) ||
            !index_in(in, id, music_owned_count_max, "song") ||
            !word(in, song.child_at(1), save_names::song_states, state)) {
            return false;
        }
        story.music_owned[id] = static_cast<std::uint8_t>(state);
    }
    return true;
}

bool read_pools(In& in, Node form, Story& story) {
    int seen = 0;
    for (std::size_t i = 1; i < form.child_count(); ++i) {
        const Node pool = form.child_at(i);
        long long id = 0;
        if (!pool.head().is_atom("pool") || pool.child_count() != 11 ||
            !number(in, pool.child_at(1), id) || !index_in(in, id, pool_count, "pool")) {
            return fail(in, "bad (pool id primary alternate0..7)");
        }
        PoolProgress& progress = story.pools[id];
        if (!number_as(in, pool.child_at(2), progress.primary)) {
            return false;
        }
        for (int k = 0; k < 8; ++k) {
            if (!number_as(in, pool.child_at(static_cast<std::size_t>(k) + 3),
                           progress.alternates[k])) {
                return false;
            }
        }
        ++seen;
    }
    return seen == pool_count || fail(in, "(pools) needs all " + std::to_string(pool_count));
}

// --- garages ---

bool read_owned(In& in, FormView form, std::string_view name, std::uint8_t* flags, int count) {
    const Node node = form.find(name);
    if (!node.valid()) {
        return fail(in, "missing (" + std::string(name) + ")");
    }
    for (int i = 0; i < count; ++i) {
        flags[i] = 0;
    }
    for (std::size_t i = 1; i < node.child_count(); ++i) {
        const Node entry = node.child_at(i);
        long long id = 0;
        long long value = 1;
        if (entry.is_list()) {
            if (!number(in, entry.child_at(0), id) || !number(in, entry.child_at(1), value)) {
                return false;
            }
        } else if (!number(in, entry, id)) {
            return false;
        }
        if (!index_in(in, id, count, name)) {
            return false;
        }
        flags[id] = static_cast<std::uint8_t>(value);
    }
    return true;
}

bool read_garages(In& in, FormView form, Garages& garages) {
    if (!field(in, form, "selected", garages.selected)) {
        return false;
    }
    const Node unlocked = form.find("unlocked");
    if (!unlocked.valid()) {
        return fail(in, "missing (unlocked)");
    }
    garages.unlocked = 0;
    for (std::size_t i = 1; i < unlocked.child_count(); ++i) {
        long long bit = 0;
        if (!number(in, unlocked.child_at(i), bit) || !index_in(in, bit, 32, "garage bit")) {
            return false;
        }
        garages.unlocked |= 1u << bit;
    }
    return read_owned(in, form, "colors-owned", garages.colors_owned, color_count) &&
           read_owned(in, form, "stickers-owned", garages.stickers_owned, sticker_count);
}

bool read_garage(In& in, FormView form, Garages& garages, int& seen) {
    long long index = 0;
    if (!number(in, form.arg(0), index) || !index_in(in, index, garage_count, "garage")) {
        return false;
    }
    Garage& g = garages.garages[index];
    ++seen;
    return glyphs(in, form, "name", g.name, glyph_name_length) && field(in, form, "body", g.body) &&
           field(in, form, "leg", g.leg) && list(in, form, "arm", g.arm, 2, std::int16_t{-1}, true) &&
           list(in, form, "back", g.back, 2, std::int16_t{-1}, true) &&
           field(in, form, "chip", g.chip) &&
           list(in, form, "option", g.option, 10, std::int16_t{-1}, true) &&
           field(in, form, "color", g.color) && field(in, form, "sticker", g.sticker);
}

bool read_item(In& in, FormView form, Garages& garages) {
    long long index = 0;
    if (!number(in, form.arg(0), index) || !index_in(in, index, inventory_size, "item")) {
        return false;
    }
    InventoryItem& item = garages.inventory[index];
    int kind = 0;
    if (!word(in, form.find("kind").child_at(1), save_names::part_kinds, kind)) {
        return false;
    }
    item.kind = static_cast<std::int16_t>(kind);
    if (form.find("name").valid() && !glyphs(in, form, "name", item.name, glyph_name_length)) {
        return false;
    }
    return field(in, form, "part", item.part) && field(in, form, "tune", item.tune) &&
           field(in, form, "garage", item.garage);
}

// --- profile ---

bool read_records(In& in, FormView form, Records& r) {
    return field(in, form, "total-earned", r.total_earned) &&
           field(in, form, "best-payout", r.best_payout) && field(in, form, "battles", r.battles) &&
           field(in, form, "wins", r.wins) && field(in, form, "kills", r.kills) &&
           field(in, form, "drones-shot-down", r.drones_shot_down) &&
           field(in, form, "play-time", r.play_time) &&
           field(in, form, "longest-battle", r.longest_battle) &&
           field(in, form, "shortest-battle", r.shortest_battle) &&
           field(in, form, "saves", r.save_count) &&
           field(in, form, "enemy-weapons-destroyed", r.enemy_weapons_destroyed) &&
           field(in, form, "weapons-destroyed", r.weapons_destroyed) &&
           field(in, form, "play-clock", r.play_clock);
}

bool read_music(In& in, FormView form, Music& music) {
    if (!field(in, form, "selected", music.playlist_selected) ||
        !flag(in, form, "random", music.playlist_random) ||
        !list(in, form, "sonic-amp-stock", music.sonic_amp_stock, sonic_amp_stock_size,
              std::int16_t{-1}, true)) {
        return false;
    }
    int seen = 0;
    for (std::size_t i = 1; i < form.node().child_count(); ++i) {
        const FormView playlist(form.node().child_at(i));
        if (!playlist.head().is_atom("playlist")) {
            continue;
        }
        long long p = 0;
        if (!number(in, playlist.arg(0), p) || !index_in(in, p, playlist_count, "playlist") ||
            !glyphs(in, playlist, "name", music.playlist_names[p], glyph_name_length) ||
            !list(in, playlist, "songs", music.playlists[p], playlist_length, -1, false)) {
            return false;
        }
        ++seen;
    }
    return seen == playlist_count || fail(in, "(music) needs all eight playlists");
}

bool read_options(In& in, FormView form, MachineOptions& o, GameOptions& seat) {
    int shift[2] = {};
    if (!list(in, form, "screen-shift", shift, 2, 0, true)) {
        return false;
    }
    o.screen_shift_x = shift[0];
    o.screen_shift_y = shift[1];
    return field(in, form, "music-volume", o.music_volume) &&
           field(in, form, "se-volume", o.se_volume) && field(in, form, "vibration", o.vibration) &&
           field(in, form, "flag-31", o.flag_31) && field(in, form, "camera-view", seat.camera_view);
}

bool read_buttons(In& in, FormView form, ButtonConfig& b) {
    constexpr std::string_view controls[] = {"standard", "preferred"};
    constexpr std::string_view views[] = {"normal", "reverse"};
    if (!word(in, form.find("control").child_at(1), controls, b.control) ||
        !word(in, form.find("view-change").child_at(1), views, b.view_change) ||
        !field(in, form, "beginner", b.beginner)) {
        return false;
    }
    for (int k = 0; k < button_action_count; ++k) {
        const Node key = form.find(save_names::button_actions[k]).child_at(1);
        if (!word(in, key, save_names::pad_buttons, b.keys[k])) {
            return false;
        }
    }
    return true;
}

} // namespace

bool save_parse(std::string_view text, SaveImage& image, std::string& error) {
    const gsexp::ParseResult parsed = gsexp::parse(text);
    if (!parsed.ok) {
        error = "can't parse the save";
        if (!parsed.diagnostics.empty()) {
            error += ": " + parsed.diagnostics.front().message + " at line " +
                     std::to_string(parsed.diagnostics.front().line);
        }
        return false;
    }

    // every form the writer writes, by head; the image starts as a fresh one
    image = SaveImage{};
    In in;
    bool header = false;
    int sections = 0;
    int garages_seen = 0;
    int rumbles_seen = 0;
    bool ok = true;
    for (std::size_t i = 0; ok && i < parsed.root_count(); ++i) {
        const FormView form(parsed.root(i));
        const Node head = form.head();
        if (head.is_atom("save")) {
            int version = 0;
            ok = field(in, form, "version", version);
            if (ok && version != save_version) {
                ok = fail(in, "save version " + std::to_string(version) + ", this game reads " +
                                  std::to_string(save_version));
            }
            image.written = parse_iso_time(form.get_string("written").value_or(""));
            header = true;
        } else if (head.is_atom("story")) {
            ok = read_story(in, form, image.story);
            ++sections;
        } else if (head.is_atom("pools")) {
            ok = read_pools(in, form.node(), image.story);
            ++sections;
        } else if (head.is_atom("garages")) {
            ok = read_garages(in, form, image.garages);
            ++sections;
        } else if (head.is_atom("garage")) {
            ok = read_garage(in, form, image.garages, garages_seen);
        } else if (head.is_atom("item")) {
            ok = read_item(in, form, image.garages);
        } else if (head.is_atom("pilot")) {
            ok = glyphs(in, form, "name", image.profile.pilot_name, pilot_name_length);
            ++sections;
        } else if (head.is_atom("records")) {
            ok = read_records(in, form, image.profile.records);
            ++sections;
        } else if (head.is_atom("music")) {
            ok = read_music(in, form, image.profile.music);
            ++sections;
        } else if (head.is_atom("options")) {
            ok = read_options(in, form, image.machine, image.profile.options);
            ++sections;
        } else if (head.is_atom("buttons")) {
            ok = read_buttons(in, form, image.profile.options.buttons);
            ++sections;
        } else if (head.is_atom("rumble")) {
            ok = read_rumble(in, form, image.rumble, rumbles_seen);
        }
    }
    if (ok && (!header || sections != 8 || garages_seen != garage_count ||
               rumbles_seen != rumble_count)) {
        ok = fail(in, "the save is missing forms");
    }
    if (!ok) {
        error = in.error;
        return false;
    }
    image.present = true;
    return true;
}
