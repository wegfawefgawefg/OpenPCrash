#pragma once
// The area picture's TV-static switch (decomp src/o1/u_012ff5.c area_plate_play,
// keyframed_sprite_draw), which area select's area page and VS setup's area page both
// play, and Random's cycling pictures (area_cycle_pictures). The four pieces' key tracks
// (area_plate_old_tracks, _new 0x1DAC78, _static 0x1DACB4, _static2 0x1DACF0) are read
// from the area_select layout file, where the extractor wrote the words they point into.
#include "screens/ui/ui.hpp"

// the switch from `start` (a task timer reading) to tex_new over tex_old, or tex_new still when
// start is -1, at (x, y) + xoff with blends blend..blend + 3; true while it plays
bool area_plate_play(Ui& ui, int tex_new, int tex_old, int start, int x, int y, int blend,
                     int xoff);
// area_cycle_pictures[pick & 7]: the pictures Random cycles through
int area_cycle_picture(Ui& ui, int pick);
