// The slot card and the load's commit, shared by the load screen and the save menu (decomp
// src/hand/u_06afc0.c save_slot_draw_summary and save_slot_draw_details,
// src/od/u_028400.c save_commit_selected_load).
#include "screens/system/save_slots.hpp"

#include "audio/audio.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/garage/garage_rules.hpp"
#include "screens/ui/screen_header.hpp"
#include "screens/ui/screen_kit.hpp"

#include <cstdio>
#include <ctime>

namespace {

// ASCII as the font's full-width forms (the font has no ASCII)
GlyphText full_width_glyphs(Ui& ui, const std::string& ascii) {
    std::string out;
    for (const char c : ascii) {
        const unsigned code = c == ' ' ? 0x3000u : static_cast<unsigned char>(c) - 0x20u + 0xff00u;
        out += static_cast<char>(0xe0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (code & 0x3f));
    }
    return utf8_to_glyphs(ui.data, out);
}

// left-aligned from x, the NY mark after it
// decomp: money_amount_draw_shadowed 0xB1DB9
void money_left_draw(Ui& ui, long long money, int x, int y, int blend) {
    const int width = number_draw_ex(ui, 4, money, x, y, blend, 0xfff0f0f0, 9, 10, 3);
    Sprite mark = sprite_of(ui, 0xeaa);
    mark.pos[0] = static_cast<float>(x + 1 - width);
    mark.pos[1] = static_cast<float>(y);
    mark.blend = blend;
    sprite_submit(ui, mark);
}

} // namespace

void save_slots_read(FrontEnd& fe, SaveScreen& s) {
    for (int slot = 0; slot < save_slot_count; ++slot) {
        std::string error;
        s.reads[slot] = save_slot_read(fe.save_dir, slot, s.images[slot], error);
        if (s.reads[slot] == SaveRead::Damaged) {
            std::fprintf(stderr, "save: slot %d: %s\n", slot, error.c_str());
        }
    }
}

bool save_slot_present(const SaveScreen& s, int slot) {
    return s.reads[slot] == SaveRead::Ok && s.images[slot].present;
}

void save_slot_card_draw(FrontEnd& fe, Ui& ui, const SaveScreen& s) {
    // the port's three slots: the number under the card (retail's one card has the Chip where a
    // number would go)
    char line[64];
    std::snprintf(line, sizeof line, "%d/%d", s.slot + 1, save_slot_count);
    text_draw(ui, full_width_glyphs(ui, line), 293, 262, 2, 18, 18, 0xfff0f0f0, 0x20);
    if (s.reads[s.slot] == SaveRead::Damaged) {
        text_draw(ui, full_width_glyphs(ui, "DAMAGED"), 0xe0, 0x11e, 1, 18, 18, 0xffee8866, 0x20);
        return;
    }
    if (!save_slot_present(s, s.slot)) {
        // save_slot_draw_details_sprites: "EMPTY" over the card
        sprite_rows_draw(ui, ui_slots(ui.data, "save", "save_slot_draw_details_sprites"));
        return;
    }
    // save_slot_draw_summary: the pilot, the selected garage's name, the money, the day
    const SaveImage& image = s.images[s.slot];
    text_draw(ui, glyph_name_text(image.profile.pilot_name, pilot_name_length), 0xe0, 0x11e, 1, 18,
              18, 0xffffffff, 9);
    const int garage = image.garages.selected;
    if (garage >= 0 && garage < garage_count) {
        text_draw(ui, glyph_name_text(image.garages.garages[garage].name, glyph_name_length), 0xe0,
                  0x15a, 1, 18, 18, 0xffffffff, 9);
    }
    day_draw_at(ui, image.story.day, 0xe0, 0x17c, 0x102);
    money_left_draw(ui, image.story.money, 0xe0, 0x13e, 0x102);
    // save_slot_draw_details: when it was written in local time (date_draw and time_hms_draw in
    // number font 0), the selected garage's Chip, the beaten mark 0x912. Their places are vec2
    // statics their constructors set at start (src/od/u_11d960.c D_0096CDF8 (279, 215),
    // u_11d980.c D_0096CE00 (287, 230), u_11d9a0.c D_00966D00 (287, 139), u_11d9c0.c D_00966D08
    // (289, 198))
    const std::time_t when = static_cast<std::time_t>(image.written);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &when);
#else
    localtime_r(&when, &local);
#endif
    date_draw(ui, local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, 279, 215, 0x103,
              0xffffffff, 0);
    time_hms_draw(ui, local.tm_hour, local.tm_min, local.tm_sec, 287, 230, 0x103, 0xffffffff, 0);
    if (garage >= 0 && garage < garage_count) {
        const int chip = image.garages.garages[garage].chip;
        if (chip >= 0 && chip < inventory_size) {
            sprite_draw_at(ui,
                           item_chip_identity_sprite(fe.shop.tables, image.garages.inventory[chip]),
                           287, 139, 0x103);
        }
    }
    if (image.story.game_beaten) {
        sprite_draw_at(ui, 0x912, 289, 198, 0x104);
    }
}

void save_commit_load(FrontEnd& fe, Ui& ui, int slot, const SaveImage& image) {
    World& world = *fe.world;
    save_image_put(image, world, *fe.machine);
    fe.menu.selected_save = slot;
    player_config_commit(world, 0);
    const MachineOptions& options = *fe.machine;
    if (ui.audio) {
        set_music_level(*ui.audio, options.music_volume);
        set_sound_level(*ui.audio, options.se_volume);
    }
    music_playlist_compact(fe.music, world.story, world.profile.music);
    talk_pilot_name_update(fe);
}
