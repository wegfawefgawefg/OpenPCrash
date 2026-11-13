#pragma once
// The two child tasks most screens start: the level meter (a sprite player on the level_meter_a
// timeline, sprite_player_* 0xD1A15.., level_meter_a_spawn) and the now-playing strip
// (music_title_strip_spawn: the playing song's title and artist scrolling at the bottom).
#include "screens/ui/screen_text.hpp"

#include <map>
#include <string>
#include <vector>

struct LevelMeter {
    const Timeline* timeline = nullptr;
    bool active = false;
    int state = 0; // 0 start, 1 run, 2 stopped
    int frame = 0;
    int frame_ticks = 0;
    std::vector<int> slot_depth;
    std::vector<Sprite> slot_sprite;
};
// level_meter_a_spawn(task, 0, 0xC): looping
void level_meter_spawn(Ui& ui, LevelMeter& meter, const std::string& timeline = "level_meter_a");
// sprite_player_update: start, step and draw by state
void level_meter_update(Ui& ui, LevelMeter& meter);

struct NowPlaying {
    bool active = false;
    int stream = -2; // the stream the strip was built for
    int x = 0, y = 0, columns = 0;
    TextStrip strip;
};
// music_title_strip_spawn(task, x, y, columns): (0, 0x1A3, 0x17) on most screens
void now_playing_spawn(Ui& ui, NowPlaying& strip, int x, int y, int columns);
// music_title_strip_update: rebuilt when the stream changes, else scrolled at layer 0x107
void now_playing_update(Ui& ui, NowPlaying& strip);
