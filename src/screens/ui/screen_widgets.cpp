#include "screens/ui/screen_widgets.hpp"


#include <cmath>
#include <cstdio>

namespace {

// sprite_player_place: a key into the slot for its depth, keeping the part's sprite on a move
// without a position (move 1 at 0, 0 resets the scale and turn only)
void place(Ui& ui, LevelMeter& meter, const TimelinePlace& key) {
    const Timeline& tl = *meter.timeline;
    std::size_t slot = 0;
    while (slot < tl.part_depths.size() && tl.part_depths[slot] != key.depth) {
        ++slot;
    }
    if (slot >= meter.slot_depth.size()) {
        return;
    }
    Sprite& sprite = meter.slot_sprite[slot];
    // matrix2_decompose: the columns' lengths and the turn of the first
    const float sx = std::sqrt(key.matrix[0] * key.matrix[0] + key.matrix[1] * key.matrix[1]);
    const float sy = std::sqrt(key.matrix[2] * key.matrix[2] + key.matrix[3] * key.matrix[3]);
    const float turn = std::atan2(key.matrix[1], key.matrix[0]);
    if (key.x == 0 && key.y == 0 && key.move == 1 && tl.part_depths[slot] == meter.slot_depth[slot]) {
        sprite.scale[0] = 1.0f;
        sprite.scale[1] = 1.0f;
        sprite.rot[2] = 0.0f;
    } else {
        meter.slot_depth[slot] = tl.part_depths[slot];
        const Sprite part = sprite_of(ui, tl.part_sprites[slot]);
        sprite.tex = part.tex;
        sprite.pos[0] = key.x;
        sprite.pos[1] = key.y;
        sprite.scale[0] = sx;
        sprite.scale[1] = sy;
        sprite.blend = key.depth + 0x104;
        sprite.z = 1.0f;
        sprite.rot[2] = turn;
    }
    auto byte = [](float c) { return static_cast<std::uint32_t>(static_cast<int>(c * 255.0f) & 0xff); };
    sprite.color = byte(key.color[0]) << 24 | byte(key.color[1]) << 16 | byte(key.color[2]) << 8 |
                   byte(key.color[3]);
}

void apply_frame(Ui& ui, LevelMeter& meter, const TimelineFrame& frame) {
    if (frame.has_background) {
        ui.clear_colour = frame.background;
    }
    for (const TimelinePlace& key : frame.places) {
        place(ui, meter, key);
    }
}

// sprite_player_start: the slots from frame 0's command count, frame 0 applied
void start(Ui& ui, LevelMeter& meter) {
    const Timeline& tl = *meter.timeline;
    const int count = tl.frames.empty() ? 0 : tl.frames[0].command_count - 1;
    meter.slot_depth.assign(static_cast<std::size_t>(std::max(count, 0)), 0);
    meter.slot_sprite.assign(meter.slot_depth.size(), Sprite{});
    for (Sprite& sprite : meter.slot_sprite) {
        sprite.tex.slot = -2;
    }
    meter.frame = 0;
    meter.frame_ticks = 0;
    meter.state = 1;
    if (!tl.frames.empty()) {
        apply_frame(ui, meter, tl.frames[0]);
    }
}

// sprite_player_step with end mode 0 (loop)
void step(Ui& ui, LevelMeter& meter) {
    const Timeline& tl = *meter.timeline;
    const int count = static_cast<int>(tl.frames.size());
    if (meter.frame_ticks == 0 && meter.frame < count) {
        apply_frame(ui, meter, tl.frames[static_cast<std::size_t>(meter.frame)]);
    }
    if (++meter.frame_ticks >= 60 / tl.fps) {
        meter.frame_ticks = 0;
        if (++meter.frame >= count) {
            meter.state = 0;
            meter.frame = 0;
        }
    }
}

// sprite_player_draw: each slot centred, flags | 4
void draw(Ui& ui, LevelMeter& meter) {
    for (Sprite sprite : meter.slot_sprite) {
        sprite.flags |= 4 | 8;
        sprite_submit(ui, sprite);
    }
}

// the Sonic Amp catalogue's title and artist by music file row (music_record_by_stream)
struct SongNames {
    std::string title, artist;
};
const std::map<int, SongNames>& song_names(Ui& ui) {
    static std::map<int, SongNames> names;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        gsexp::ParseResult parsed;
        std::string error;
        if (!ui_sexp(ui.data, "tables/music.sexp", parsed, error)) {
            std::fprintf(stderr, "ui: %s\n", error.c_str());
            return names;
        }
        for (std::size_t i = 0; i < parsed.root_count(); ++i) {
            const gsexp::FormView form(parsed.root(i));
            if (form.head().is_atom("song")) {
                const int wave = form.get_int("wave").value_or(-1);
                names.emplace(wave, SongNames{form.get_string("title").value_or(""),
                                              form.get_string("artist").value_or("")});
            }
        }
    }
    return names;
}

// D_001DE1A8 "《", the title, D_001DE1AC "　：", the artist, D_001DE1B0 "　》　　　　　"; a full-width
// space when the stream isn't a catalogue song (D_0019B4E8)
std::string strip_text(Ui& ui, int stream) {
    const auto& names = song_names(ui);
    const auto found = names.find(stream);
    if (stream < 0 || found == names.end()) {
        return "　";
    }
    return "《" + found->second.title + "　：" + found->second.artist +
           "　》　　　　　";
}

void build(Ui& ui, NowPlaying& strip, int stream) {
    strip.stream = stream;
    text_strip_init(ui, strip.strip, strip.x, strip.y, strip_text(ui, stream), strip.x, strip.y,
                    strip.x - 0x12 + strip.columns * 0x12, strip.y + 0x12);
}

} // namespace

void level_meter_spawn(Ui& ui, LevelMeter& meter, const std::string& timeline) {
    meter = LevelMeter{};
    meter.timeline = ui_timeline(ui.data, timeline);
    meter.active = meter.timeline != nullptr;
}

void level_meter_update(Ui& ui, LevelMeter& meter) {
    if (!meter.active) {
        return;
    }
    switch (meter.state) {
    case 0:
        start(ui, meter);
        draw(ui, meter);
        break;
    case 1:
        step(ui, meter);
        draw(ui, meter);
        break;
    default:
        break;
    }
}

void now_playing_spawn(Ui& ui, NowPlaying& strip, int x, int y, int columns) {
    strip.active = true;
    strip.x = x;
    strip.y = y;
    strip.columns = columns;
    build(ui, strip, ui_music_current(ui));
}

void now_playing_update(Ui& ui, NowPlaying& strip) {
    if (!strip.active) {
        return;
    }
    const int stream = ui_music_current(ui);
    if (stream != strip.stream) {
        build(ui, strip, stream);
    } else {
        text_strip_draw_scroll(ui, strip.strip, 0xffffffff, 0x107);
    }
}
