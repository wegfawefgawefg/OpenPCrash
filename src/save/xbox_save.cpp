// A retail phantomcrash.sav into save images: GameState's head read field by field at its offsets
// (decomp include/game_state.h, include/actor.h; docs/saves.md has the table).
#include "save/xbox_save.hpp"

#include "cook/sexp_read.hpp"

#include <cmath>
#include <cstring>

namespace {

// little-endian reads inside one slot's GameState bytes
struct Bytes {
    const std::uint8_t* p = nullptr;
    std::uint32_t u32(std::size_t at) const {
        return static_cast<std::uint32_t>(p[at]) | static_cast<std::uint32_t>(p[at + 1]) << 8 |
               static_cast<std::uint32_t>(p[at + 2]) << 16 |
               static_cast<std::uint32_t>(p[at + 3]) << 24;
    }
    std::int32_t i32(std::size_t at) const {
        return static_cast<std::int32_t>(u32(at));
    }
    std::int16_t i16(std::size_t at) const {
        return static_cast<std::int16_t>(p[at] | p[at + 1] << 8);
    }
    std::int8_t i8(std::size_t at) const {
        return static_cast<std::int8_t>(p[at]);
    }
    std::uint8_t u8(std::size_t at) const {
        return p[at];
    }
};

// a signed bitfield of a word (MSVC packs from bit 0)
int bits_signed(std::uint32_t word, int first, int count) {
    const std::uint32_t value = (word >> first) & ((1u << count) - 1);
    const std::uint32_t sign = 1u << (count - 1);
    return static_cast<int>(value ^ sign) - static_cast<int>(sign);
}
int bits(std::uint32_t word, int first, int count) {
    return static_cast<int>((word >> first) & ((1u << count) - 1));
}

// a glyph string, up to its -1; all zeros (memory nobody wrote) reads as empty
void read_glyphs(const Bytes& g, std::size_t at, std::int16_t* out, int length) {
    bool zero = true;
    for (int i = 0; i < length; ++i) {
        zero = zero && g.i16(at + static_cast<std::size_t>(i) * 2) == 0;
    }
    bool ended = zero;
    for (int i = 0; i < length; ++i) {
        const std::int16_t glyph = g.i16(at + static_cast<std::size_t>(i) * 2);
        ended = ended || glyph == -1;
        out[i] = ended ? std::int16_t{-1} : glyph;
    }
}

// OpponentTicket.flag_at, a GameState offset, as the byte's place: a pool's primary (pool * 9),
// an alternate (+ 1 + k), or tournament_won (0x990 + t)
void read_ticket(const Bytes& g, std::size_t at, RumbleTicket& ticket, XboxSlot& slot) {
    ticket.opponent = g.i32(at);
    const int flag_at = g.i32(at + 4);
    ticket.progress.role = g.i32(at + 8);
    ticket.progress.pool = -1;
    ticket.progress.alternate = -1;
    ticket.progress.tournament = -1;
    if (flag_at >= 0 && flag_at < pool_count * 9) {
        ticket.progress.pool = flag_at / 9;
        ticket.progress.alternate = flag_at % 9 - 1;
    } else if (flag_at >= 0x990 && flag_at < 0x990 + tournament_count) {
        ticket.progress.tournament = flag_at - 0x990;
    } else {
        slot.notes.push_back("a ticket's flag_at " + std::to_string(flag_at) + " is no progress byte");
    }
}

// a roster name's Shift-JIS bytes as UTF-8
void read_name(const std::uint8_t* bytes, int size, const SjisTable& sjis, char* out, int capacity,
               XboxSlot& slot) {
    std::string text;
    for (int i = 0; i < size && bytes[i] != 0; ++i) {
        const int lead = bytes[i];
        if (lead < 0x80) {
            text += static_cast<char>(lead);
            continue;
        }
        if (((lead >= 0x81 && lead <= 0x9F) || (lead >= 0xE0 && lead <= 0xFC)) && i + 1 < size) {
            const auto found = sjis.find(lead << 8 | bytes[i + 1]);
            text += found != sjis.end() ? found->second : "?";
            ++i;
            continue;
        }
        text += "?";
    }
    if (static_cast<int>(text.size()) >= capacity) {
        slot.notes.push_back("a roster name is too long: " + text);
        text.resize(static_cast<std::size_t>(capacity) - 1);
    }
    std::memset(out, 0, static_cast<std::size_t>(capacity));
    std::memcpy(out, text.data(), text.size());
}

Fx fx_of_float(float value) {
    const double raw = std::round(static_cast<double>(value) * 65536.0);
    return Fx::from_raw(static_cast<std::int32_t>(std::fmax(-2147483648.0, std::fmin(2147483647.0, raw))));
}

void read_roster(const Bytes& g, std::size_t at, const std::uint8_t* name, const SjisTable& sjis,
                 RosterEntry& e, XboxSlot& slot) {
    RosterBuild& b = e.build;
    b.unused_00 = g.i16(at);
    b.body = g.i16(at + 0x02);
    b.body_tune = g.i8(at + 0x04);
    b.leg = g.i16(at + 0x06);
    b.leg_tune = g.i8(at + 0x08);
    for (int w = 0; w < 4; ++w) {
        b.weapon[w].id = g.i16(at + 0x0A + static_cast<std::size_t>(w) * 4);
        b.weapon[w].tune = g.i8(at + 0x0C + static_cast<std::size_t>(w) * 4);
    }
    b.chip = g.i16(at + 0x1A);
    b.chip_tune = g.i8(at + 0x1C);
    b.color = g.i16(at + 0x1E);
    b.sticker = g.i16(at + 0x20);
    for (int o = 0; o < 10; ++o) {
        b.option[o] = g.i16(at + 0x22 + static_cast<std::size_t>(o) * 2);
    }
    // +0x38 is the name pointer, cleared around a write (roster_names_clear)
    RosterPilot& p = e.pilot;
    p.identity = g.i16(at + 0x3C);
    p.scoobee_identity = g.i16(at + 0x3E);
    p.team = g.i16(at + 0x40);
    p.unused_06 = g.i16(at + 0x42);
    p.value_base = g.i32(at + 0x44);
    p.value_at_mid = g.i32(at + 0x48);
    p.value_at_max = g.i32(at + 0x4C);
    p.value_mid = g.i8(at + 0x50);
    p.value_negative = g.i8(at + 0x51);
    p.reaction = g.i16(at + 0x54);
    p.unused_1a = g.i16(at + 0x56);
    e.level = g.i16(at + 0x58);
    e.ai_profile = g.i16(at + 0x5A);
    e.player = g.i16(at + 0x5C);
    e.destruction_value = g.i32(at + 0x60);
    read_ticket(g, at + 0x64, e.ticket, slot);
    e.queue = g.i32(at + 0x70);
    float start[3];
    for (int k = 0; k < 3; ++k) {
        const std::uint32_t word = g.u32(at + 0x74 + static_cast<std::size_t>(k) * 4);
        std::memcpy(&start[k], &word, sizeof word);
    }
    e.start = {fx_of_float(start[0]), fx_of_float(start[1]), fx_of_float(start[2])};
    e.start_set = g.u8(at + 0x80) != 0;
    read_name(name, 0x13, sjis, e.name, roster_name_bytes, slot);
}

void read_rumble(const Bytes& g, std::size_t at, const SjisTable& sjis, RumbleSlot& r, int& lineup,
                 XboxSlot& slot) {
    for (int q = 0; q < 3; ++q) {
        const std::size_t base = at + static_cast<std::size_t>(q) * 0x370;
        OpponentQueue& queue = r.queues[q];
        for (int t = 0; t < queue_capacity; ++t) {
            read_ticket(g, base + static_cast<std::size_t>(t) * 12, queue.tickets[t], slot);
        }
        queue.drawable = g.i32(base + 0x360);
        queue.count = g.i32(base + 0x364);
        queue.out = g.i32(base + 0x368);
        queue.ready = g.i32(base + 0x36C);
    }
    r.regular_quota = g.i32(at + 0xA50);
    r.entrants = g.i32(at + 0xA54);
    lineup = g.i32(at + 0xA58);
    for (int e = 0; e < roster_size; ++e) {
        read_roster(g, at + 0xA5C + static_cast<std::size_t>(e) * 0x84,
                    g.p + at + 0xD74 + static_cast<std::size_t>(e) * 0x13, sjis, r.roster[e], slot);
    }
    r.fixed_queued = g.u8(at + 0xDE6) != 0;
    for (int k = 0; k < 8; ++k) {
        r.pool_queued[k] = g.u8(at + 0xDE7 + static_cast<std::size_t>(k)) != 0;
    }
}

void read_game(const Bytes& g, const SjisTable& sjis, SaveImage& image, XboxSlot& slot) {
    Story& story = image.story;
    Garages& garages = image.garages;
    Profile& profile = image.profile;

    // pools and tournaments (+0x000, +0x990)
    for (int i = 0; i < pool_count; ++i) {
        story.pools[i].primary = g.i8(static_cast<std::size_t>(i) * 9);
        for (int k = 0; k < 8; ++k) {
            story.pools[i].alternates[k] = g.i8(static_cast<std::size_t>(i) * 9 + 1 + k);
        }
    }
    for (int t = 0; t < tournament_count; ++t) {
        story.tournament_won[t] = g.i8(0x990 + static_cast<std::size_t>(t));
    }

    // the three bitfield words (+0x9C8, +0x9CC, +0x9D0)
    const std::uint32_t w9c8 = g.u32(0x9C8);
    const std::uint32_t w9cc = g.u32(0x9CC);
    const std::uint32_t w9d0 = g.u32(0x9D0);
    garages.selected = bits_signed(w9c8, 0, 5);
    profile.records.play_clock = bits(w9c8, 5, 19);
    image.machine.music_volume = bits(w9c8, 24, 8);
    image.machine.se_volume = bits(w9cc, 0, 8);
    image.machine.screen_shift_x = bits_signed(w9cc, 8, 6);
    image.machine.screen_shift_y = bits_signed(w9cc, 14, 6);
    story.day = bits(w9cc, 20, 10);
    image.machine.vibration = bits(w9cc, 30, 1);
    image.machine.flag_31 = bits(w9cc, 31, 1);
    story.money = bits(w9d0, 0, 31);
    story.game_beaten = bits(w9d0, 31, 1) != 0;

    // songs, playlists, Sonic Amp, colours and stickers (+0x9D4 .. +0xD43)
    for (int s = 0; s < music_owned_count_max; ++s) {
        story.music_owned[s] = g.u8(0x9D4 + static_cast<std::size_t>(s));
    }
    Music& music = profile.music;
    for (int p = 0; p < playlist_count; ++p) {
        for (int k = 0; k < playlist_length; ++k) {
            music.playlists[p][k] = g.i32(0xAA8 + static_cast<std::size_t>(p * playlist_length + k) * 4);
        }
    }
    for (int s = 0; s < sonic_amp_stock_size; ++s) {
        music.sonic_amp_stock[s] = g.i16(0xC28 + static_cast<std::size_t>(s) * 2);
    }
    for (int c = 0; c < color_count; ++c) {
        garages.colors_owned[c] = g.u8(0xC3C + static_cast<std::size_t>(c));
    }
    for (int c = 0; c < sticker_count; ++c) {
        garages.stickers_owned[c] = g.u8(0xC75 + static_cast<std::size_t>(c));
    }

    // garages unlocked, the events, the media player's choice and names (+0xD44 .. +0xE07)
    garages.unlocked = g.u32(0xD44);
    for (int w = 0; w < 5; ++w) {
        if (g.u32(0xD48 + static_cast<std::size_t>(w) * 4) != 0) {
            slot.notes.push_back("events_unused isn't zero");
        }
        story.events_done[w] = g.u32(0xD5C + static_cast<std::size_t>(w) * 4);
    }
    music.playlist_random = g.i32(0xD70) != 0;
    music.playlist_selected = g.i32(0xD74);
    for (int p = 0; p < playlist_count; ++p) {
        read_glyphs(g, 0xD78 + static_cast<std::size_t>(p) * 18, music.playlist_names[p],
                    glyph_name_length);
    }

    // inventory (+0xE08), garages (+0x25F0), the pilot (+0x280C)
    for (int i = 0; i < inventory_size; ++i) {
        const std::size_t at = 0xE08 + static_cast<std::size_t>(i) * 0x18;
        InventoryItem& item = garages.inventory[i];
        item.garage = g.i8(at);
        item.tune = g.i8(at + 1);
        item.kind = g.i16(at + 2);
        item.part = g.i16(at + 4);
        read_glyphs(g, at + 6, item.name, glyph_name_length);
    }
    for (int n = 0; n < garage_count; ++n) {
        const std::size_t at = 0x25F0 + static_cast<std::size_t>(n) * 0x36;
        Garage& garage = garages.garages[n];
        garage.body = g.i16(at);
        garage.leg = g.i16(at + 2);
        garage.arm[0] = g.i16(at + 4);
        garage.arm[1] = g.i16(at + 6);
        garage.back[0] = g.i16(at + 8);
        garage.back[1] = g.i16(at + 0xA);
        garage.chip = g.i16(at + 0xC);
        for (int o = 0; o < 10; ++o) {
            garage.option[o] = g.i16(at + 0xE + static_cast<std::size_t>(o) * 2);
        }
        read_glyphs(g, at + 0x22, garage.name, glyph_name_length);
        garage.color = g.u8(at + 0x34);
        garage.sticker = g.u8(at + 0x35);
    }
    read_glyphs(g, 0x280C, profile.pilot_name, pilot_name_length);

    // today's RumbleContexts (+0x2820)
    for (int s = 0; s < rumble_count; ++s) {
        read_rumble(g, 0x2820 + static_cast<std::size_t>(s) * 0xDF0, sjis, image.rumble[s],
                    story.lineup[s], slot);
    }

    // records (+0x5FE0); f_5ff0 is only ever cleared
    Records& r = profile.records;
    r.total_earned = g.i32(0x5FE0);
    r.best_payout = g.i32(0x5FE4);
    r.battles = g.i32(0x5FE8);
    r.wins = g.i32(0x5FEC);
    if (g.i32(0x5FF0) != 0) {
        slot.notes.push_back("records f_5ff0 isn't zero");
    }
    r.kills = g.i32(0x5FF4);
    r.drones_shot_down = g.i32(0x5FF8);
    story.days_played = g.i32(0x5FFC);
    r.play_time = g.i32(0x6000);
    r.longest_battle = g.i32(0x6004);
    r.shortest_battle = g.i32(0x6008);
    r.save_count = g.i32(0x600C);
    r.enemy_weapons_destroyed = g.i32(0x6010);
    r.weapons_destroyed = g.i32(0x6014);

    // the button config (PlayerConfig +0x6018); flags at +0x6020 follow from the two stick rows
    ButtonConfig& b = profile.options.buttons;
    b.control = g.i32(0x6018);
    b.view_change = g.i32(0x601C);
    if (g.i32(0x6020) != button_control_type(b)) {
        slot.notes.push_back("the button config's control type word doesn't match its rows");
    }
    b.beginner = g.u8(0x6024);
    for (int k = 0; k < button_action_count; ++k) {
        b.keys[k] = g.i32(0x6028 + static_cast<std::size_t>(k) * 4);
    }

    // the camera view and the post-game cycles (+0x6048, +0x604C); the rest to +0x60C4 unused
    profile.options.camera_view = g.i32(0x6048);
    story.completed_cycles = g.i32(0x604C);
    for (std::size_t at = 0x6050; at < xbox_game_size; ++at) {
        if (g.u8(at) != 0) {
            slot.notes.push_back("GameState +0x6050..+0x60C3 isn't all zero");
            break;
        }
    }
}

} // namespace

// decomp: save_checksum 0xA0FC5
std::uint32_t xbox_save_checksum(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

bool load_sjis_table(SjisTable& table, const std::string& data_dir, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/text/font.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView glyph(parsed.root(i));
        if (!glyph.head().is_atom("glyph") || !glyph.find("sjis").valid()) {
            continue;
        }
        const int pair = static_cast<int>(atom_hex(glyph.find_arg("sjis", 0)));
        table.emplace(pair, std::string(glyph.arg(1).text()));
    }
    if (table.empty()) {
        error = "no sjis pairs in " + data_dir + "/text/font.sexp";
        return false;
    }
    return true;
}

bool xbox_save_read(const std::vector<std::uint8_t>& file, const SjisTable& sjis,
                    XboxSlot slots[3], std::string& error) {
    if (file.size() != xbox_save_size) {
        error = "phantomcrash.sav is " + std::to_string(xbox_save_size) + " bytes; this is " +
                std::to_string(file.size());
        return false;
    }
    for (int s = 0; s < 3; ++s) {
        const std::uint8_t* slot = file.data() + static_cast<std::size_t>(s) * xbox_slot_size;
        XboxSlot& out = slots[s];
        const Bytes header{slot};
        out.crc_ok = xbox_save_checksum(slot, 0x60D0) == header.u32(0x60D0);
        out.present = slot[0] == 1;
        out.image = SaveImage{};
        if (!out.present) {
            continue;
        }
        // the FILETIME (100 ns since 1601) as seconds since 1970
        const std::uint64_t filetime =
            static_cast<std::uint64_t>(header.u32(4)) | static_cast<std::uint64_t>(header.u32(8)) << 32;
        out.image.present = true;
        out.image.written = static_cast<std::int64_t>(filetime / 10000000u) - 11644473600LL;
        read_game(Bytes{slot + 0xC}, sjis, out.image, out);
    }
    return true;
}
