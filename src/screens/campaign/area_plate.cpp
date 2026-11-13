#include "screens/campaign/area_plate.hpp"

#include "screens/ui/ui_data.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

// the tracks' and the pictures' addresses (.data, decomp include/data.h)
constexpr unsigned old_tracks = 0x1DABD8;
constexpr unsigned new_tracks = 0x1DAC78;
constexpr unsigned static_tracks = 0x1DACB4;
constexpr unsigned static2_tracks = 0x1DACF0;
constexpr unsigned cycle_pictures = 0x1DAB28;
constexpr int static_sprite = 0x35e;
// a key list ends at a negative frame; none of retail's has more than 6 keys
constexpr int max_keys = 16;

// one 32-bit word of the area_select file: the number the extractor wrote, or raw bits (a
// SpriteSlot's colour and flags, which the tables the tracks run through hold floats in)
struct Word {
    double number = 0;
    bool bits = false;
};

// the file's words by address: the extractor splits the original's data at every name, and the
// tracks point across those tables, so they are read back as memory
const std::map<unsigned, Word>& words(Ui& ui) {
    static std::map<unsigned, Word> by_address;
    if (!by_address.empty()) {
        return by_address;
    }
    const std::string screen = "area_select";
    const UiFile& file = ui_file(ui.data, screen);
    std::map<std::string, unsigned> named;
    for (const UiTable& t : file.tables) {
        named[t.name] = t.va;
    }
    // a pointer word as the extractor writes it: a symbol and an offset (D_001DA200+0x948)
    const auto symbol = [&](const std::string& text) -> unsigned {
        const std::size_t plus = text.find('+');
        const std::string name = text.substr(0, plus);
        const unsigned offset =
            plus == std::string::npos
                ? 0u
                : static_cast<unsigned>(std::strtoul(text.c_str() + plus + 1, nullptr, 16));
        if (const auto at = named.find(name); at != named.end()) {
            return at->second + offset;
        }
        if (name.rfind("D_", 0) == 0) {
            return static_cast<unsigned>(std::strtoul(name.c_str() + 2, nullptr, 16)) + offset;
        }
        return 0;
    };
    for (const UiTable& t : file.tables) {
        unsigned va = t.va;
        if (!t.slots.empty()) {
            for (const SpriteSlot& s : t.slots) {
                by_address[va] = {static_cast<double>(s.sprite)};
                by_address[va + 4] = {s.x};
                by_address[va + 8] = {s.y};
                by_address[va + 12] = {static_cast<double>(s.blend)};
                by_address[va + 16] = {static_cast<double>(s.color), true};
                by_address[va + 20] = {s.z};
                by_address[va + 24] = {static_cast<double>(s.flags), true};
                va += 28;
            }
        } else if (!t.rows.empty() && t.layout.rfind("Words_", 0) == 0) {
            for (std::size_t r = 0; r < t.rows.size(); ++r) {
                for (std::size_t k = 0; k < t.rows[r].size(); ++k) {
                    const std::string& text = t.row_strings[r][k];
                    by_address[va] =
                        text.empty() ? Word{t.rows[r][k]} : Word{static_cast<double>(symbol(text))};
                    va += 4;
                }
            }
        } else if (t.layout == "int" || t.layout == "float") {
            for (double v : t.values) {
                by_address[va] = {v};
                va += 4;
            }
        }
    }
    return by_address;
}

Word word_at(Ui& ui, unsigned va) {
    const auto& map = words(ui);
    const auto at = map.find(va);
    return at == map.end() ? Word{} : at->second;
}

int word_int(Ui& ui, unsigned va) {
    const Word w = word_at(ui, va);
    return w.bits ? static_cast<int>(static_cast<std::uint32_t>(w.number))
                  : static_cast<int>(w.number);
}

float word_float(Ui& ui, unsigned va) {
    const Word w = word_at(ui, va);
    if (w.bits) {
        const auto raw = static_cast<std::uint32_t>(w.number);
        float f = 0;
        std::memcpy(&f, &raw, sizeof f);
        return f;
    }
    return static_cast<float>(w.number);
}

// KeyPos {frame, x, y} ints, KeyScale {frame, x, y} floats, KeyAlpha {frame, a}: a key as the
// frame and up to two values
struct Key {
    int frame = -1;
    float v[2] = {};
};
// a track's keys from its pointer word, up to the negative frame; empty for a null pointer
std::vector<Key> track(Ui& ui, unsigned pointer, int values, bool floats) {
    std::vector<Key> keys;
    unsigned va = static_cast<unsigned>(word_int(ui, pointer));
    if (va == 0) {
        return keys;
    }
    for (int i = 0; i <= max_keys; ++i, va += 4u * static_cast<unsigned>(1 + values)) {
        Key key;
        key.frame = word_int(ui, va);
        for (int k = 0; k < values; ++k) {
            const unsigned at = va + 4u * static_cast<unsigned>(1 + k);
            key.v[k] = floats ? word_float(ui, at) : static_cast<float>(word_int(ui, at));
        }
        keys.push_back(key);
        if (key.frame < 0) {
            break;
        }
    }
    return keys;
}

struct Tracks {
    std::vector<Key> pos, scale, alpha;
};
Tracks tracks_at(Ui& ui, unsigned va) {
    return {track(ui, va, 2, false), track(ui, va + 4, 2, true), track(ui, va + 8, 1, true)};
}

// the key at or after t on a track (the end marker once past the last), the one before it, and
// how far between them; false once the track has ended (its last key then holds)
bool key_blend(const std::vector<Key>& keys, unsigned t, int& prev, int& cur, float& a) {
    int i = 0;
    while (i < static_cast<int>(keys.size()) && keys[i].frame >= 0 &&
           t >= static_cast<unsigned>(keys[i].frame)) {
        ++i;
    }
    cur = i < static_cast<int>(keys.size()) ? i : static_cast<int>(keys.size()) - 1;
    prev = i > 0 ? i - 1 : 0;
    a = 0.0f;
    if (keys[cur].frame > 0) {
        a = static_cast<float>(t - static_cast<unsigned>(keys[prev].frame)) /
            static_cast<float>(keys[cur].frame - keys[prev].frame);
        a = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
        return true;
    }
    return false;
}

// true once every track has ended
// decomp: keyframed_sprite_draw 0x143BF
bool keyframed_draw(Ui& ui, unsigned t, int tex, const Tracks& tr, int x, int blend, int y,
                    int xoff) {
    bool done[3] = {true, true, true};
    x += sprite_width(ui, tex) / 2;
    y += sprite_height(ui, tex) / 2;
    int px = x, py = y;
    float sx = 1.0f, sy = 1.0f, alpha = 1.0f;
    int prev = 0, cur = 0;
    float a = 0.0f;
    if (!tr.pos.empty()) {
        done[0] = !key_blend(tr.pos, t, prev, cur, a);
        if (!done[0]) {
            px = static_cast<int>(tr.pos[cur].v[0] * a + tr.pos[prev].v[0] * (1.0f - a));
            py = static_cast<int>(tr.pos[cur].v[1] * a + tr.pos[prev].v[1] * (1.0f - a));
        } else {
            px = static_cast<int>(tr.pos[prev].v[0]), py = static_cast<int>(tr.pos[prev].v[1]);
        }
    }
    if (!tr.scale.empty()) {
        done[1] = !key_blend(tr.scale, t, prev, cur, a);
        if (!done[1]) {
            sx = a * tr.scale[cur].v[0] + (1.0f - a) * tr.scale[prev].v[0];
            sy = a * tr.scale[cur].v[1] + (1.0f - a) * tr.scale[prev].v[1];
        } else {
            sx = tr.scale[prev].v[0], sy = tr.scale[prev].v[1];
        }
    }
    if (!tr.alpha.empty()) {
        done[2] = !key_blend(tr.alpha, t, prev, cur, a);
        alpha = done[2] ? tr.alpha[prev].v[0]
                        : (1.0f - a) * tr.alpha[prev].v[0] + a * tr.alpha[cur].v[0];
    }
    Sprite s = sprite_of(ui, tex);
    s.pos[0] = static_cast<float>(px + xoff);
    s.pos[1] = static_cast<float>(py);
    s.flags |= 8;
    s.blend = blend;
    s.scale[0] = sx;
    s.scale[1] = sy;
    s.z = alpha;
    sprite_submit(ui, s);
    return done[0] && done[1] && done[2];
}

} // namespace

// decomp: area_plate_play 0x15305
bool area_plate_play(Ui& ui, int tex_new, int tex_old, int start, int x, int y, int blend,
                     int xoff) {
    if (start == -1) {
        Sprite s = sprite_of(ui, tex_new);
        s.pos[0] = static_cast<float>(xoff + x);
        s.pos[1] = static_cast<float>(y);
        s.blend = blend;
        sprite_submit(ui, s);
        return false;
    }
    const Tracks old_track = tracks_at(ui, old_tracks);
    const Tracks new_track = tracks_at(ui, new_tracks);
    const Tracks static_track = tracks_at(ui, static_tracks);
    const Tracks static2_track = tracks_at(ui, static2_tracks);
    const unsigned t = static_cast<unsigned>(ui.task_timer - static_cast<std::uint32_t>(start)) *
                       static_cast<unsigned>(60 / ui.frame_cadence);
    const bool a = keyframed_draw(ui, t, tex_old, old_track, x, blend, y, xoff);
    const bool b = keyframed_draw(ui, t, tex_new, new_track, x, blend + 1, y, xoff);
    const bool c = keyframed_draw(ui, t, static_sprite, static_track, x, blend + 2, y, xoff);
    const bool d = keyframed_draw(ui, t, static_sprite, static2_track, x, blend + 3, y, xoff);
    return !(a && b && c && d);
}

int area_cycle_picture(Ui& ui, int pick) {
    return word_int(ui, cycle_pictures + 4u * static_cast<unsigned>(pick & 7));
}
