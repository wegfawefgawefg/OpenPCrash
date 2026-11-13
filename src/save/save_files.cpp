// Save slots on disk and the copies between the world and an image (save_write_slot,
// save_commit_selected_load, the load screen's newest-slot pick in save_load_screen_step2).
#include "save/save.hpp"

#include <SDL3/SDL_filesystem.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

// save_write_slot caps the count here
constexpr int save_count_cap = 9999;

// the story's saved fields from one story into another; the rest stay as they are
void story_copy_saved(const Story& from, Story& to) {
    for (int i = 0; i < pool_count; ++i) {
        to.pools[i] = from.pools[i];
    }
    for (int t = 0; t < tournament_count; ++t) {
        to.tournament_won[t] = from.tournament_won[t];
    }
    to.day = from.day;
    to.money = from.money;
    to.game_beaten = from.game_beaten;
    for (int s = 0; s < music_owned_count_max; ++s) {
        to.music_owned[s] = from.music_owned[s];
    }
    for (int w = 0; w < 5; ++w) {
        to.events_done[w] = from.events_done[w];
    }
    for (int s = 0; s < 4; ++s) {
        to.lineup[s] = from.lineup[s];
    }
    to.completed_cycles = from.completed_cycles;
    to.days_played = from.days_played;
    // retail leaves these two out (past +0x60C4); the port saves them (docs/bugs.md)
    to.rumble_selected = from.rumble_selected;
    to.map_spin_money = from.map_spin_money;
}

} // namespace

void save_image_take(SaveImage& image, const World& world, const MachineOptions& machine,
                     std::int64_t now) {
    image.present = true;
    image.written = now;
    image.story = Story{};
    story_copy_saved(world.story, image.story);
    image.garages = world.garages;
    image.profile = world.profile;
    for (int s = 0; s < rumble_count; ++s) {
        image.rumble[s] = world.rumble[s];
    }
    image.machine = machine;
}

void save_image_put(const SaveImage& image, World& world, MachineOptions& machine) {
    story_copy_saved(image.story, world.story);
    world.garages = image.garages;
    world.profile = image.profile;
    for (int s = 0; s < rumble_count; ++s) {
        world.rumble[s] = image.rumble[s];
    }
    machine = image.machine;
}

std::string save_dir_default() {
    std::string dir;
    if (char* prefs = SDL_GetPrefPath("PhantomCrash", "PhantomCrash")) {
        dir = prefs;
        SDL_free(prefs);
    }
    return dir + "saves";
}

std::string save_slot_path(const std::string& dir, int slot) {
    return dir + "/slot" + std::to_string(slot) + ".sexp";
}

SaveRead save_slot_read(const std::string& dir, int slot, SaveImage& image, std::string& error) {
    image = SaveImage{};
    const std::string path = save_slot_path(dir, slot);
    std::error_code code;
    if (!std::filesystem::exists(path, code)) {
        return SaveRead::Missing;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "can't open " + path;
        return SaveRead::Damaged;
    }
    std::stringstream text;
    text << in.rdbuf();
    if (!save_parse(text.str(), image, error)) {
        error = path + ": " + error;
        image = SaveImage{};
        return SaveRead::Damaged;
    }
    return SaveRead::Ok;
}

bool save_slot_write(const std::string& dir, int slot, World& world, const MachineOptions& machine,
                     std::string& error) {
    // the count goes up first, so the save holds itself
    Records& records = world.profile.records;
    records.save_count = std::min(records.save_count + 1, save_count_cap);
    SaveImage image;
    save_image_take(image, world, machine, static_cast<std::int64_t>(std::time(nullptr)));
    const std::string text = save_text(image);

    // a temporary file moved over the slot, so a failed write leaves the old save whole
    const std::string path = save_slot_path(dir, slot);
    const std::string temporary = path + ".tmp";
    std::error_code code;
    std::filesystem::create_directories(dir, code);
    bool ok = false;
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        ok = static_cast<bool>(out);
    }
    if (ok) {
        std::filesystem::rename(temporary, path, code);
        ok = !code;
    }
    if (!ok) {
        error = "can't write " + path;
        std::filesystem::remove(temporary, code);
        if (records.save_count > 0) {
            records.save_count -= 1;
        }
    }
    return ok;
}

int save_newest_slot(const SaveImage* images, int count) {
    int newest = -1;
    for (int s = 0; s < count; ++s) {
        if (images[s].present && (newest < 0 || images[s].written > images[newest].written)) {
            newest = s;
        }
    }
    return newest;
}
