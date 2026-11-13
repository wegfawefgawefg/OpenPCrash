// A new game's garages, profile and RumbleContexts (decomp src/od/u_0b9460.c game_state_new and its
// helpers, src/o1/u_02e2fc.c options_defaults_set, src/o1/u_0201ab.c player_config_reset).
#include "world/game_state.hpp"

#include "world/world.hpp"

namespace {

// "Ｍｒ．ＵＳＡ" (D_0025BAE0, Shift-JIS) as the font's glyphs (sjis_to_glyphs; data/text/font.sexp)
constexpr std::int16_t default_pilot_name[] = {169, 200, 4, 177, 175, 157, -1};
// music_state_new: song 77 is the first playlist's only entry
constexpr int first_song = 77;

void glyph_name_clear(GlyphName& name) {
    for (std::int16_t& glyph : name) {
        glyph = -1;
    }
}

} // namespace

// decomp: player_config_flags_update 0x27CDA
int button_control_type(const ButtonConfig& buttons) {
    return (buttons.control == 0 ? 1 : 0) | (buttons.view_change == 0 ? 2 : 0);
}

// decomp: game_state_new_garages 0xB95E8, garages_clear 0xB96F4
void garages_new(Garages& garages) {
    // three garages, colour 0 and the "none" sticker owned, an empty inventory
    // (game_state_new_garages)
    garages.selected = -1;
    garages.unlocked = new_game_garages;
    for (std::uint8_t& owned : garages.colors_owned) {
        owned = 0;
    }
    garages.colors_owned[0] |= 1;
    for (std::uint8_t& owned : garages.stickers_owned) {
        owned = 0;
    }
    garages.stickers_owned[no_sticker] |= 1;
    // the item names aren't touched by the boot; they start as the task heap's zeros in retail
    for (InventoryItem& item : garages.inventory) {
        item = InventoryItem{};
    }

    // every garage empty, colour 0, sticker 0xCD, no name (garages_clear)
    for (Garage& garage : garages.garages) {
        garage = Garage{};
    }
}

// decomp: game_state_new 0xB9460
void profile_new(Profile& profile) {
    // options_defaults_set: the button config's defaults (player_config_reset)
    // and camera view 1; its vibration, volumes and screen shift are MachineOptions{}
    profile.options = GameOptions{};

    // the pilot's name
    for (std::int16_t& glyph : profile.pilot_name) {
        glyph = -1;
    }
    for (int i = 0; default_pilot_name[i] != -1; ++i) {
        profile.pilot_name[i] = default_pilot_name[i];
    }

    // music_state_new: every playlist empty but song 77 first in the first, sequential,
    // names cleared. Its music_shop_generate_stock draws CRT rand, so the front end's music player
    // rolls the stock (src/screens/shops/music_player.cpp music_state_new) after this.
    Music& music = profile.music;
    for (auto& list : music.playlists) {
        for (int& song : list) {
            song = -1;
        }
    }
    music.playlists[0][0] = first_song;
    music.playlist_random = false;
    music.playlist_selected = 0;
    for (GlyphName& name : music.playlist_names) {
        glyph_name_clear(name);
    }
    for (std::int16_t& song : music.sonic_amp_stock) {
        song = -1;
    }

    // records_clear (and play_clock, cleared by game_state_new's mask of +0x9C8)
    profile.records = Records{};
}

void rumble_slot_clear(RumbleSlot& slot) {
    slot = RumbleSlot{};
}

// decomp: game_state_new 0xB9460
void game_state_new(World& world, const StoryTables& tables) {
    story_boot(world.story, tables);
    garages_new(world.garages);
    profile_new(world.profile);
    for (RumbleSlot& slot : world.rumble) {
        rumble_slot_clear(slot);
    }
    // player_config_new: the defaults committed to player 0
    player_config_commit(world, 0);
}

// decomp: player_config_commit 0x28052
void player_config_commit(World& world, int player) {
    if (player >= 0 && player < 4) {
        world.player_buttons[player] = world.profile.options.buttons;
    }
}
