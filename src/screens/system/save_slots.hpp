#pragma once
// What the load screen (0x11) and the save menu (0x1F) share over src/save's three slots: reading
// them, the slot card's summary (save_slot_draw_summary, save_slot_draw_details),
// and the rest of save_commit_selected_load after the copy.
#include "save/save.hpp"

struct FrontEnd;
struct Ui;
struct SaveScreen;

// every slot read; a damaged one says why on stderr
void save_slots_read(FrontEnd& fe, SaveScreen& s);
bool save_slot_present(const SaveScreen& s, int slot);
// the card: the slot's number under it (the port has three), the pilot, the selected garage's
// name, the money and the day as save_slot_draw_summary places them, and save_slot_draw_details'
// date, time, Chip and beaten mark; "EMPTY" for a slot with nothing in it and "DAMAGED" for one
// that doesn't read
void save_slot_card_draw(FrontEnd& fe, Ui& ui, const SaveScreen& s);
// save_commit_selected_load past the copy: selected_save, the button config to player 0
// (player_config_commit), the two volumes, the music player's queue (music_playlist_compact)
void save_commit_load(FrontEnd& fe, Ui& ui, int slot, const SaveImage& image);
