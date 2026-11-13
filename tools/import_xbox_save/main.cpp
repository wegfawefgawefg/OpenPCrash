// pc_import_xbox_save: a retail phantomcrash.sav (the raw file out of the Xbox's "Game Records" save
// game) into the port's save, and a hand-run round trip of the save format (docs/saves.md).
//
//   pc_import_xbox_save [--data DIR] [--slot N] phantomcrash.sav out.sexp
//   pc_import_xbox_save --roundtrip [--data DIR] out.sexp
//
// The import reads every slot, checks each one's CRC-32, writes the chosen slot (the newest present
// one by default), then reads what it wrote back and writes that again, which must give the same
// text. The round trip does the same for a new game's world with every kind of field filled in.
#include "save/save.hpp"
#include "save/xbox_save.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace {

#ifdef PHANTOMCRASH_DATA_DIR_DEFAULT
constexpr const char* data_dir_default = PHANTOMCRASH_DATA_DIR_DEFAULT;
#else
constexpr const char* data_dir_default = "data";
#endif

bool write_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

// text -> image -> text must give the same text
bool check_round_trip(const std::string& text) {
    SaveImage again;
    std::string error;
    if (!save_parse(text, again, error)) {
        std::fprintf(stderr, "round trip: the written save doesn't read back: %s\n", error.c_str());
        return false;
    }
    if (save_text(again) != text) {
        std::fprintf(stderr, "round trip: the save reads back different\n");
        return false;
    }
    std::printf("round trip: the save reads back the same (%zu bytes)\n", text.size());
    return true;
}

int run_import(const std::string& data_dir, int slot, const std::string& in_path,
               const std::string& out_path) {
    std::ifstream in(in_path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "can't open %s\n", in_path.c_str());
        return 1;
    }
    const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    SjisTable sjis;
    std::string error;
    XboxSlot slots[3];
    if (!load_sjis_table(sjis, data_dir, error) || !xbox_save_read(file, sjis, slots, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    // retail rejects the whole file when any slot's checksum is wrong
    SaveImage images[3];
    bool all_ok = true;
    for (int s = 0; s < 3; ++s) {
        const XboxSlot& x = slots[s];
        images[s] = x.image;
        all_ok = all_ok && x.crc_ok;
        std::printf("slot %d: %s, crc %s", s, x.present ? "present" : "empty",
                    x.crc_ok ? "ok" : "WRONG");
        if (x.present) {
            std::printf(", day %d, money %d, beaten %d", x.image.story.day + 1, x.image.story.money * 10,
                        x.image.story.game_beaten ? 1 : 0);
        }
        std::printf("\n");
        for (const std::string& note : x.notes) {
            std::printf("  note: %s\n", note.c_str());
        }
    }
    if (!all_ok) {
        std::fprintf(stderr, "a slot's checksum is wrong; the original would call this save damaged\n");
        return 1;
    }
    if (slot < 0) {
        slot = save_newest_slot(images, 3);
    }
    if (slot < 0 || slot >= 3 || !images[slot].present) {
        std::fprintf(stderr, "no saved game in that slot\n");
        return 1;
    }
    const std::string text = save_text(images[slot]);
    if (!write_file(out_path, text)) {
        std::fprintf(stderr, "can't write %s\n", out_path.c_str());
        return 1;
    }
    std::printf("slot %d written to %s\n", slot, out_path.c_str());
    return check_round_trip(text) ? 0 : 1;
}

// a new game, then a value in every kind of field the format has
void fill_world(World& world, MachineOptions& machine) {
    Story& story = world.story;
    story.day = 37;
    story.money = 1234567;
    story.game_beaten = true;
    story.completed_cycles = 2;
    story.days_played = 157;
    story.events_done[0] = 0x8000000F;
    story.events_done[3] = 0x00010000;
    story.music_owned[12] = 1;
    story.music_owned[200] = 7;
    story.tournament_won[3] = 1;
    story.pools[152].alternates[2] = 1;

    Garages& garages = world.garages;
    garages.selected = 1;
    garages.unlocked = 0x2F;
    garages.colors_owned[5] = 1;
    garages.stickers_owned[17] = 3;
    garages.inventory[0] = {1, -12, item_body, 12, {-1, -1, -1, -1, -1, -1, -1, -1, -1}};
    garages.inventory[4] = {-1, 99, item_chip, 95, {169, 200, -1, -1, -1, -1, -1, -1, -1}};
    garages.inventory[9] = {3, 0, -1, -1, {-1, -1, -1, -1, -1, -1, -1, -1, -1}}; // stale free slot
    garages.garages[1].body = 0;
    garages.garages[1].chip = 4;
    garages.garages[1].name[0] = 177;
    garages.garages[1].color = 5;

    Profile& profile = world.profile;
    profile.records.battles = 40;
    profile.records.wins = 31;
    profile.records.play_clock = 99;
    profile.music.playlists[3][0] = 12;
    profile.music.playlists[3][5] = 40;
    profile.music.playlist_random = true;
    profile.music.playlist_names[3][0] = 157;
    profile.music.sonic_amp_stock[0] = 3;
    machine.screen_shift_x = -32;
    machine.screen_shift_y = 14;
    profile.options.camera_view = 4;
    profile.options.buttons.control = 1;
    profile.options.buttons.beginner = 1;
    profile.options.buttons.keys[0] = 4;
    profile.options.buttons.keys[1] = 5;

    RumbleSlot& slot = world.rumble[1];
    slot.regular_quota = 2;
    slot.entrants = 4;
    slot.fixed_queued = true;
    slot.pool_queued[0] = true;
    slot.queues[0].count = 2;
    slot.queues[0].drawable = 2;
    slot.queues[0].ready = 2;
    slot.queues[0].tickets[0] = {812, {0, 152, 3, -1}};
    slot.queues[0].tickets[1] = {900, {2, -1, -1, 14}};
    RosterEntry& entry = slot.roster[2];
    entry.player = -1;
    entry.level = -5;
    entry.destruction_value = 1500;
    entry.ticket = {812, {0, 152, 3, -1}};
    entry.queue = 1;
    entry.build.body = 12;
    entry.build.body_tune = -99;
    entry.build.weapon[0] = {39, 7};
    entry.pilot.value_at_mid = 5000;
    entry.pilot.value_negative = 1;
    entry.start = {Fx::from_raw(-65536 * 12 - 3), Fx::from_raw(0), Fx::from_raw(77)};
    entry.start_set = true;
    std::strcpy(entry.name, "ＤＲＯＮＥＳ \"x\"");
}

int run_round_trip(const std::string& data_dir, const std::string& out_path) {
    StoryTables tables;
    std::string error;
    if (!load_story_tables(tables, data_dir, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    World world;
    game_state_new(world, tables);
    story_new_game(world.story, tables, 0);
    MachineOptions machine;
    fill_world(world, machine);

    // save_slot_write's path: the count bump, the file, and back
    const std::string dir = out_path + ".d";
    if (!save_slot_write(dir, 0, world, machine, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    SaveImage image;
    if (save_slot_read(dir, 0, image, error) != SaveRead::Ok) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    World loaded;
    MachineOptions loaded_machine;
    game_state_new(loaded, tables);
    save_image_put(image, loaded, loaded_machine);
    SaveImage first;
    SaveImage second;
    save_image_take(first, world, machine, image.written);
    save_image_take(second, loaded, loaded_machine, image.written);
    const std::string text = save_text(first);
    if (save_text(second) != text) {
        std::fprintf(stderr, "round trip: the loaded world saves differently\n");
        write_file(out_path + ".loaded", save_text(second));
        return 1;
    }
    if (!write_file(out_path, text)) {
        std::fprintf(stderr, "can't write %s\n", out_path.c_str());
        return 1;
    }
    std::printf("a filled-in new game written to %s and %s\n", out_path.c_str(),
                save_slot_path(dir, 0).c_str());
    return check_round_trip(text) ? 0 : 1;
}

void usage() {
    std::fprintf(stderr, "usage: pc_import_xbox_save [--data DIR] [--slot N] phantomcrash.sav out.sexp\n"
                         "       pc_import_xbox_save --roundtrip [--data DIR] out.sexp\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string data_dir = data_dir_default;
    int slot = -1;
    bool round_trip = false;
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "--slot" && i + 1 < argc) {
            slot = std::atoi(argv[++i]);
        } else if (arg == "--roundtrip") {
            round_trip = true;
        } else if (!arg.empty() && arg[0] == '-') {
            usage();
            return 2;
        } else {
            paths.push_back(arg);
        }
    }
    if (round_trip && paths.size() == 1) {
        return run_round_trip(data_dir, paths[0]);
    }
    if (!round_trip && paths.size() == 2) {
        return run_import(data_dir, slot, paths[0], paths[1]);
    }
    usage();
    return 2;
}
