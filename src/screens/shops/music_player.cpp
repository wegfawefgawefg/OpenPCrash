// The music player (decomp src/hand/music_advance.c, src/o1/u_012ff5.c 0x1BA45..0x1BBD4,
// src/o1/u_028e51.c 0x2A04A..0x2A8F2).
#include "screens/shops/music_player.hpp"

#include "audio/audio.hpp"
#include "screens/ui/ui.hpp"
#include "story/story.hpp"
#include "world/game_state.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

// the four tournament themes battle_music_update plays instead of the list: the First Ranker,
// Rocket Summer, Shibuya's and Aqualine's Area Rankers
struct Theme {
    int tournament, track;
};
constexpr Theme tournament_themes[4] = {{0x31, 0x3a}, {0xe, 0xbe}, {0x1f, 0x35}, {0x30, 0x3f}};
// battle_playlist_defaults: twelve of track 28
constexpr int default_battle_track = 28;
// the stock: 77 songs 0..76 in the list, ten drawn
constexpr int stock_list = 77;

// decomp: rand_crt 0x1141EB
int rand_crt(MusicPlayer& music) {
    music.crt = music.crt * 0x343FDu + 0x269EC3u;
    return static_cast<int>((music.crt >> 16) & 0x7FFF);
}

} // namespace

bool load_music_catalogue(MusicPlayer& music, UiData& data, std::string& error) {
    gsexp::ParseResult parsed;
    if (!ui_sexp(data, "tables/music.sexp", parsed, error)) {
        return false;
    }
    music.catalogue.clear();
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("song")) {
            continue;
        }
        MusicRecord record;
        record.id = form.get_int("id").value_or(-1);
        record.wave = form.get_int("wave").value_or(-1);
        record.title = form.get_string("title").value_or("");
        record.artist = form.get_string("artist").value_or("");
        record.jacket_sprite = form.get_int("jacket_sprite").value_or(-1);
        record.chart_sprite = form.get_int("chart_sprite").value_or(-1);
        // the bio is an address written in hex (0x1e7af8): get_int reads decimal only
        const unsigned bio = static_cast<unsigned>(
            std::strtoul(std::string(form.find_arg("bio", 0).text()).c_str(), nullptr, 0));
        char name[32];
        std::snprintf(name, sizeof name, "bio_%x", bio);
        record.bio = bio ? name : "";
        record.listen_sprite_a = form.get_int("listen_sprite_a").value_or(-1);
        record.listen_sprite_b = form.get_int("listen_sprite_b").value_or(-1);
        if (record.id == static_cast<int>(music.catalogue.size())) {
            music.catalogue.push_back(record);
        }
    }
    if (static_cast<int>(music.catalogue.size()) != music_record_count) {
        error = "tables/music.sexp: expected 81 songs";
        return false;
    }
    return true;
}

// decomp: rand_float_crt 0xD3E3A
float rand_float_crt(MusicPlayer& music, float range) {
    return static_cast<float>(rand_crt(music)) / 32768.0f * range;
}

// decomp: music_record 0x2A04A
const MusicRecord* music_record(const MusicPlayer& music, int id) {
    if (id < 0 || id >= static_cast<int>(music.catalogue.size())) {
        return nullptr;
    }
    return &music.catalogue[static_cast<std::size_t>(id)];
}

// decomp: music_record_owned 0x2A066
const MusicRecord* music_record_owned(const MusicPlayer& music, const Story& story, int id) {
    const MusicRecord* record = music_record(music, id);
    return record && story.music_owned[id] != 0 ? record : nullptr;
}

// decomp: music_record_by_stream 0x2A387
const MusicRecord* music_record_by_stream(const MusicPlayer& music, int wave) {
    if (wave < 0) {
        return nullptr;
    }
    for (const MusicRecord& record : music.catalogue) {
        if (record.wave == wave) {
            return &record;
        }
    }
    return nullptr;
}

std::string music_title_text(const MusicRecord& record) {
    return "《" + record.title + "　：" + record.artist + "　》　　　　　";
}

// decomp: music_state_new 0xBAAF8
void music_state_new(MusicPlayer& music, Story& story, Music& player_music) {
    music_shop_generate_stock(music, player_music);
    battle_playlist_reset(music, story, player_music);
}

// decomp: music_shop_generate_stock 0x2A4C0
void music_shop_generate_stock(MusicPlayer& music, Music& player_music) {
    // retail draws index rand_float_crt(n) from a list of n + 1, so the list's last entry, id 76
    // ("Hit Me"), is never stocked (docs/bugs.md); the port draws over the whole list. The forced
    // low song when no draw took an index below 5 stays as retail has it.
    int list[stock_list];
    for (int i = 0; i < stock_list; ++i) {
        list[i] = i;
    }
    bool low = false;
    int left = stock_list;
    for (int draw = 0; draw < sonic_amp_stock_size; ++draw, --left) {
        int k = static_cast<int>(rand_float_crt(music, static_cast<float>(left)));
        k = k < 0 ? 0 : k >= left ? left - 1 : k;
        low = low || k < 5;
        player_music.sonic_amp_stock[draw] = static_cast<std::int16_t>(list[k]);
        for (; k < stock_list - 1; ++k) {
            list[k] = list[k + 1];
        }
        list[stock_list - 1] = -1;
    }
    if (!low) {
        int slot = static_cast<int>(rand_float_crt(music, 9.0f)); // D_0019B4EC
        if (slot >= 10) {
            slot = 0;
        }
        player_music.sonic_amp_stock[slot] =
            static_cast<std::int16_t>(static_cast<int>(rand_float_crt(music, 4.0f))); // D_0017BDA0
    }
}

// decomp: music_playlist_compact 0x2A097
void music_playlist_compact(MusicPlayer& music, const Story& story, const Music& player_music) {
    BattlePlaylist& list = music.battle;
    list.current = -1;
    const int selected = player_music.playlist_selected;
    for (int i = 0; i < playlist_length; ++i) {
        const int id = selected >= 0 && selected < playlist_count
                           ? player_music.playlists[selected][i]
                           : -1;
        const MusicRecord* record = music_record_owned(music, story, id);
        list.played[i] = record == nullptr;
        list.wave[i] = record ? record->wave : -1;
    }
}

// decomp: battle_playlist_reset 0x9E150
void battle_playlist_reset(MusicPlayer& music, const Story& story, const Music& player_music) {
    BattlePlaylist& list = music.battle;
    list.countdown = 0;
    list.current = static_cast<int>(rand_float_crt(music, 12.0f)) % 12;
    for (int i = 0; i < playlist_length; ++i) {
        list.played[i] = false;
        list.wave[i] = default_battle_track;
    }
    music_playlist_compact(music, story, player_music);
}

// decomp: music_playlist_advance 0x9E3A4
int music_playlist_advance(MusicPlayer& music, const Music& player_music) {
    BattlePlaylist& list = music.battle;
    const float roll = rand_float_crt(music, 1.0f); // retail: rank = (int)rand_float_crt(12) % 12
    if (list.wave[0] == -1) {
        return -1;
    }
    if (!player_music.playlist_random) {
        const int next = (list.current + 1) % 12;
        return list.wave[next] == -1 ? 0 : next;
    }
    // retail walks `rank % total` down with `left <= 0`, so 0 and 1 both pick the first unplayed
    // slot, the last is never picked while two remain, and the modulo leans to the first ones
    // (docs/bugs.md): the port picks uniformly among the unplayed slots with the same one draw
    int total = 0;
    for (int i = 0; i < playlist_length; ++i) {
        total += list.wave[i] != -1 && !list.played[i] ? 1 : 0;
    }
    if (total == 0) {
        return -1;
    }
    int left = static_cast<int>(roll * static_cast<float>(total));
    left = left >= total ? total - 1 : left;
    for (int i = 0; i < playlist_length; ++i) {
        if (list.wave[i] != -1 && !list.played[i]) {
            if (left == 0) {
                return i;
            }
            --left;
        }
    }
    return -1;
}

// decomp: battle_music_update 0x9E1CC
void battle_music_update(MusicPlayer& music, Ui& ui, const Music& player_music, int tournament) {
    BattlePlaylist& list = music.battle;
    const bool playing = ui.audio && music_active(*ui.audio);
    if (list.countdown < 0) {
        if (!playing) {
            list.countdown = ui.frame_cadence * 3;
        }
        return;
    }
    --list.countdown;
    if (list.countdown != 0 || playing) {
        return;
    }
    for (const Theme& theme : tournament_themes) {
        if (theme.tournament == tournament) {
            ui_music(ui, theme.track, false);
            return;
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        const int slot = music_playlist_advance(music, player_music);
        if (slot != -1) {
            // battle_playlist_play
            ui_music(ui, list.wave[slot], false);
            list.played[slot] = true;
            list.current = slot;
            return;
        }
        for (bool& played : list.played) {
            played = false;
        }
    }
}

// decomp: music_shuffle_catalogue 0x1BA45
void music_shuffle_catalogue(MusicPlayer& music) {
    MenuShuffle& shuffle = music.shuffle;
    shuffle.wait = 0;
    for (bool& played : shuffle.played) {
        played = false;
    }
    shuffle.dealt = true;
    int deck[music_record_count];
    for (int i = 0; i < music_record_count; ++i) {
        deck[i] = i;
    }
    // retail draws index rand_float_crt(80 - dealt) from a deck of 81 - dealt, so id 80 is never
    // dealt (docs/bugs.md); the port draws over the whole deck
    for (int dealt = 0; dealt < music_shuffle_size; ++dealt) {
        const int left = music_record_count - dealt;
        int k = static_cast<int>(rand_float_crt(music, static_cast<float>(left)));
        k = k < 0 ? 0 : k >= left ? left - 1 : k;
        shuffle.pool[dealt] = deck[k];
        for (; k < music_record_count - 1; ++k) {
            deck[k] = deck[k + 1];
        }
        deck[music_record_count - 1] = -1;
    }
}

// decomp: music_pick_from_shuffled_pool 0x1BAE7
void music_shuffle_pump(MusicPlayer& music, Ui& ui, float volume, int update) {
    if (music.pumped_update == update) {
        return; // retail's help menu ran two pumps, halving the gap between songs (docs/bugs.md)
    }
    music.pumped_update = update;
    MenuShuffle& shuffle = music.shuffle;
    if (!shuffle.dealt) {
        music_shuffle_catalogue(music);
    }
    const bool playing = ui.audio && music_active(*ui.audio);
    if (shuffle.wait > 0) {
        --shuffle.wait;
    } else if (shuffle.wait == 0) {
        // retail draws a slot and walks on to the next unplayed one, so a slot after played ones
        // comes up more often (docs/bugs.md): the port picks uniformly among the unplayed with
        // the same one CRT draw; all played, the marks start over
        const float roll = rand_float_crt(music, 1.0f);
        int unplayed = 0;
        for (const bool played : shuffle.played) {
            unplayed += played ? 0 : 1;
        }
        if (unplayed == 0) {
            for (bool& played : shuffle.played) {
                played = false;
            }
            unplayed = music_shuffle_size;
        }
        int left = static_cast<int>(roll * static_cast<float>(unplayed));
        left = left < 0 ? 0 : left >= unplayed ? unplayed - 1 : left;
        for (int slot = 0; slot < music_shuffle_size; ++slot) {
            if (shuffle.played[slot]) {
                continue;
            }
            if (left-- > 0) {
                continue;
            }
            const MusicRecord* record = music_record(music, shuffle.pool[slot]);
            if (!record) {
                record = music_record(music, 0);
            }
            if (record && ui.audio) {
                play_music(*ui.audio, record->wave, false, volume);
            }
            // retail never sets the played bit, so its no-repeat guard is dead (docs/bugs.md);
            // the port sets it
            shuffle.played[slot] = true;
            --shuffle.wait;
            break;
        }
    } else if (playing) {
        --shuffle.wait;
    } else if (shuffle.wait <= -2) {
        shuffle.wait = ui.frame_cadence * 5;
    }
}
