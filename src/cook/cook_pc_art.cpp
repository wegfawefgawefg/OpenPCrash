// PC CONFIG's letters (docs/settings.md, "The look"), found in the cook: the key config's white
// labels, the number font's digits and signs, and the option buttons' yellow words are scanned for
// their text's column runs, and each letter's cut is written to the core pack as pc_art.sexp.
// The game reads the cuts and draws them from the cooked sprites (src/screens/pc/pc_settings_art.cpp).
#include "cook/cook_pc_art.hpp"

#include "cook/sexp_read.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cstdio>
#include <map>

namespace {

// the key config's white labels (key_config_tex.bin): their words, for their letters
struct ThinSource {
    int sprite;
    const char* text;
};
constexpr ThinSource thin_sources[] = {
    {1600, "CONTROL TYPE"},
    {1603, "VIEW CHANGE TYPE"},
    {1617, "BEGINNER MODE"},
    {1618, "DASH"},
    {1619, "DEFAULT"},
    {1620, "EXIT"},
    {1621, "JUMP"},
    {1622, "CUSTOM"},
    {1623, "LEFT ARM WEAPONS"},
    {1624, "LEFT BACKPACK WEAPONS"},
    {1625, "LEFT STEP"},
    {1628, "OPTICAL CAMOUFLAGE"},
    {1629, "RIGHT ARM WEAPONS"},
    {1630, "RIGHT BACKPACK WEAPONS"},
    {1631, "RIGHT STEP"},
    {1632, "TYPE C"},
    {1601, "NORMAL"},
    {1626, "OFF"},
};
// the number font the sound config draws its values with (number_fonts font 3, common_2d)
constexpr int digit_zero = 3734;
constexpr ThinSource number_signs[] = {
    {3744, ","}, {3746, ":"}, {3747, "."}, {3748, "-"}, {3749, "+"}, {3750, "/"},
};
// "SHOOTDOWN×" (common_2d): its last letter is the only multiplication sign
constexpr int times_sprite = 3756;

// the option buttons (option_tex, sound_config_tex, system_tex): normal, lit, word
struct ButtonSource {
    int sprite, lit;
    const char* text;
};
constexpr ButtonSource button_sources[] = {
    {2118, 2119, "SOUND CONFIG"}, {2125, 2126, "SCREEN ADJUST"}, {2108, 2109, "EXIT"},
    {2515, 2516, "BGM TEST"},     {2517, 2518, "BGM VOLUME"},    {2639, 2640, "KEEP SV"},
    {2524, 2525, "SE TEST"},      {2526, 2527, "SE VOLUME"},     {2110, 2111, "DEFAULT"},
};
// where the words' baseline is put (SOUND CONFIG's)
constexpr int button_baseline = 22;

struct Glyph {
    int sprite = -1;
    int x0 = 0, x1 = 0;
    int baseline = 0;
    int height = 0;
};
struct Font {
    std::map<char, Glyph> glyphs;
    int baseline = 0;
    int space = 7;
};

struct Pixels {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
    const std::uint8_t* at(int x, int y) const {
        return &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                      static_cast<std::size_t>(x)) *
                     4];
    }
};

struct Scan {
    const std::string& data_dir;
    std::map<int, std::string> files; // registry id to its PNG, relative to data/
    std::vector<std::string>& sources;
};

bool load_pixels(Scan& scan, int id, Pixels& out) {
    const auto file = scan.files.find(id);
    if (file == scan.files.end()) {
        return false;
    }
    const std::string path = scan.data_dir + "/" + file->second;
    SDL_Surface* loaded = IMG_Load(path.c_str());
    if (!loaded) {
        std::fprintf(stderr, "cook: pc art: no %s\n", path.c_str());
        return false;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!rgba) {
        return false;
    }
    out.w = rgba->w;
    out.h = rgba->h;
    out.rgba.resize(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h) * 4);
    for (int y = 0; y < out.h; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(rgba->pixels) + y * rgba->pitch;
        std::copy(row, row + out.w * 4,
                  out.rgba.begin() + static_cast<std::ptrdiff_t>(y) * out.w * 4);
    }
    SDL_DestroySurface(rgba);
    scan.sources.push_back(file->second);
    return true;
}

bool white_text(const std::uint8_t* p) {
    return p[3] > 100 && p[0] > 140;
}
bool yellow_text(const std::uint8_t* p) {
    return p[3] >= 128 && p[0] > 150 && p[1] > 110 && p[2] < 90;
}

struct Run {
    int x0 = 0, x1 = 0, bottom = 0;
};

// the column runs holding text pixels in rows y0..y1 and columns x0..x1
std::vector<Run> text_runs(const Pixels& px, bool (*is_text)(const std::uint8_t*), int x0, int x1,
                           int y0, int y1) {
    std::vector<Run> runs;
    const auto column_has = [&](int cx) {
        for (int y = y0; y < y1; ++y) {
            if (is_text(px.at(cx, y))) {
                return true;
            }
        }
        return false;
    };
    for (int x = x0; x < x1;) {
        if (!column_has(x)) {
            ++x;
            continue;
        }
        Run run;
        run.x0 = x;
        while (x < x1 && column_has(x)) {
            for (int y = y0; y < y1; ++y) {
                if (is_text(px.at(x, y))) {
                    run.bottom = std::max(run.bottom, y);
                }
            }
            ++x;
        }
        run.x1 = x;
        runs.push_back(run);
    }
    return runs;
}

// the most common bottom row: the baseline (round letters overshoot it by one)
int baseline_of(const std::vector<Run>& runs) {
    std::map<int, int> votes;
    for (const Run& run : runs) {
        ++votes[run.bottom];
    }
    int best = 0, count = -1;
    for (const auto& [row, n] : votes) {
        if (n > count) {
            best = row;
            count = n;
        }
    }
    return best;
}

// each run's cut: a column of outline either side, or up to halfway across a narrower gap
void add_glyphs(Font& font, int sprite, const Pixels& px, const std::vector<Run>& runs,
                const std::string& letters, int baseline) {
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const char c = letters[i];
        if (font.glyphs.count(c)) {
            continue; // the first source keeps it
        }
        Glyph glyph;
        glyph.sprite = sprite;
        glyph.x0 = std::max(0, runs[i].x0 - 1);
        if (i > 0) {
            glyph.x0 = std::max(glyph.x0, (runs[i - 1].x1 + runs[i].x0) / 2);
        }
        glyph.x1 = std::min(px.w, runs[i].x1 + 1);
        if (i + 1 < runs.size()) {
            glyph.x1 = std::min(glyph.x1, (runs[i].x1 + runs[i + 1].x0 + 1) / 2);
        }
        glyph.baseline = baseline;
        glyph.height = px.h;
        font.glyphs[c] = glyph;
    }
}

std::string letters_of(const char* text) {
    std::string out;
    for (const char* p = text; *p; ++p) {
        if (*p != ' ') {
            out.push_back(*p);
        }
    }
    return out;
}

void scan_thin(Font& font, Scan& scan) {
    std::vector<int> baselines;
    for (const ThinSource& source : thin_sources) {
        Pixels px;
        if (!load_pixels(scan, source.sprite, px)) {
            continue;
        }
        const std::vector<Run> runs = text_runs(px, white_text, 0, px.w, 0, px.h);
        const std::string letters = letters_of(source.text);
        if (runs.size() != letters.size()) {
            continue; // letters touching: this label can't be cut
        }
        const int baseline = baseline_of(runs);
        baselines.push_back(baseline);
        add_glyphs(font, source.sprite, px, runs, letters, baseline);
    }
    std::sort(baselines.begin(), baselines.end());
    font.baseline = baselines.empty() ? 17 : baselines[baselines.size() / 2];
    font.space = 9;

    // the digits and signs, on the digits' baseline
    Pixels zero;
    if (!load_pixels(scan, digit_zero, zero)) {
        return;
    }
    const std::vector<Run> zero_runs = text_runs(zero, white_text, 0, zero.w, 0, zero.h);
    const int digit_baseline = zero_runs.empty() ? zero.h - 1 : zero_runs.front().bottom;
    const auto one = [&](int sprite, char c) {
        Pixels px;
        if (!load_pixels(scan, sprite, px)) {
            return;
        }
        const std::vector<Run> runs = text_runs(px, white_text, 0, px.w, 0, px.h);
        if (runs.empty()) {
            return;
        }
        // one glyph from the first ink to the last
        const Run whole{runs.front().x0, runs.back().x1, digit_baseline};
        add_glyphs(font, sprite, px, {whole}, std::string(1, c), digit_baseline);
    };
    for (int d = 0; d < 10; ++d) {
        one(digit_zero + d, static_cast<char>('0' + d));
    }
    for (const ThinSource& sign : number_signs) {
        one(sign.sprite, sign.text[0]);
    }
    // the multiplication sign, the last run of SHOOTDOWN×, as 'x'
    Pixels times;
    if (load_pixels(scan, times_sprite, times)) {
        const std::vector<Run> runs = text_runs(times, white_text, 0, times.w, 0, times.h);
        if (runs.size() >= 2) {
            add_glyphs(font, times_sprite, times, {runs.back()}, "x", digit_baseline);
        }
    }
}

void scan_buttons(Font& font, std::map<int, int>& lit_of, Scan& scan) {
    for (const ButtonSource& source : button_sources) {
        Pixels px;
        if (!load_pixels(scan, source.sprite, px)) {
            continue;
        }
        const std::vector<Run> runs = text_runs(px, yellow_text, 6, px.w - 6, 7, px.h - 7);
        const std::string letters = letters_of(source.text);
        if (runs.size() != letters.size()) {
            continue;
        }
        add_glyphs(font, source.sprite, px, runs, letters, baseline_of(runs));
        lit_of[source.sprite] = source.lit;
    }
    font.baseline = button_baseline;
    font.space = 6;
}

void write_font(std::string& out, const char* name, const Font& font) {
    char line[160];
    std::snprintf(line, sizeof line, "(font %s (baseline %d) (space %d))\n", name, font.baseline,
                  font.space);
    out += line;
    for (const auto& [c, g] : font.glyphs) {
        std::snprintf(line, sizeof line, "(glyph %s %d %d %d %d %d %d)\n", name,
                      static_cast<int>(static_cast<unsigned char>(c)), g.sprite, g.x0, g.x1,
                      g.baseline, g.height);
        out += line;
    }
}

} // namespace

bool cook_pc_art(const std::string& data_dir, std::vector<std::uint8_t>& out,
                 std::vector<std::string>& sources, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/ui/images.sexp", parsed, error)) {
        return false;
    }
    Scan scan{data_dir, {}, sources};
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (form.head().is_atom("image")) {
            scan.files[form.get_int("id").value_or(-1)] =
                "ui/images/" + form.get_string("file").value_or("");
        }
    }
    Font thin, button;
    std::map<int, int> lit_of;
    scan_thin(thin, scan);
    scan_buttons(button, lit_of, scan);

    // (font NAME (baseline B) (space S)), (glyph NAME CHAR SPRITE X0 X1 BASELINE HEIGHT),
    // (lit SPRITE LIT)
    std::string text = "; PC CONFIG's letters, cut from the original's art by the cook "
                       "(src/cook/cook_pc_art.cpp)\n";
    write_font(text, "thin", thin);
    write_font(text, "button", button);
    for (const auto& [sprite, lit] : lit_of) {
        char line[64];
        std::snprintf(line, sizeof line, "(lit %d %d)\n", sprite, lit);
        text += line;
    }
    out.assign(text.begin(), text.end());
    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    return true;
}
