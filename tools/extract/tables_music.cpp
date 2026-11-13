// Music: the Sonic Amp catalogue and the 202-name music file table (docs/port/data.md "Music"
// and disc section 20; src/data/music.c, misc_b.c). Track numbers everywhere are rows of the
// file table, so it keeps all 202 rows although only 89 files are on the US disc.
#include "addresses.hpp"
#include "sexp_out.hpp"
#include "tables.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

namespace {

constexpr int catalogue_count = 81;
constexpr int music_file_count = 202;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool write_files(Tables& t) {
    const Xbe& x = *t.xbe;
    // the disc's sound folder, compared without case as the Xbox does
    std::set<std::string> on_disc;
    for (const std::string& name : disc_list(t.disc->source, "Media/sound")) {
        on_disc.insert(lower(name));
    }
    TableFile file{"music_files",
                   "music_files (char *)",
                   xbe_at::music_files,
                   music_file_count,
                   {"the music file per track number, opened from D:\\Media\\sound at boot "
                    "(music_catalogue_register 0x9DE10); state_music, the catalogue's wave and "
                    "the trigger table's music are these rows",
                    "on_disc: 1 when the extracted disc has the file (89 of 202 on the US disc; "
                    "a missing one fails to open silently)"},
                   {}};
    int found = 0;
    for (int id = 0; id < music_file_count; ++id) {
        const std::uint32_t name =
            xbe_u32(x, xbe_at::music_files + static_cast<std::uint32_t>(id) * 4);
        const bool present = name != 0 && on_disc.count(lower(xbe_cstring(x, name))) > 0;
        found += present ? 1 : 0;
        add_row(file, {"music_file",
                       {field("id", id), field("file", text_at(t, name)),
                        field("on_disc", present ? 1 : 0)}});
    }
    std::printf("tables: %d of %d music files on the disc\n", found, music_file_count);
    return write_table(t, file);
}

bool write_catalogue(Tables& t) {
    const Xbe& x = *t.xbe;
    TableFile file{"music",
                   "music_sonic_amp_catalogue (MusicRecord)",
                   xbe_at::music_catalogue,
                   catalogue_count,
                   {"the Sonic Amp catalogue; wave: row of music_files.sexp",
                    "jacket_sprite: render registry id of the art beside the song under the "
                    "cursor; chart_sprite: the song's tile in the chart grid",
                    "bio: XBE address of the artist bio conversation (EventCommand array)",
                    "listen_sprite_a, listen_sprite_b (+0x1A4, +0x1A8): the listen view's artist "
                    "pictures (1786..1908, inside the render registry's "
                    "range)"},
                   {}};
    for (int id = 0; id < catalogue_count; ++id) {
        const std::uint32_t r = xbe_at::music_catalogue + static_cast<std::uint32_t>(id) * 0x1AC;
        const std::uint32_t bio = xbe_u32(x, r + 0x1A0);
        add_row(file, {"song",
                       {field("id", id), field("wave", xbe_i32(x, r)),
                        field("title", text_in(t, r + 0x004, 0x65)),
                        field("artist", text_in(t, r + 0x069, 0x65)),
                        field("lyricist", text_in(t, r + 0x0CE, 0x65)),
                        field("composer", text_in(t, r + 0x133, 0x65)),
                        field("jacket_sprite", xbe_i32(x, r + 0x198)),
                        field("chart_sprite", xbe_i32(x, r + 0x19C)),
                        field("bio", bio == 0 ? std::string("nil") : sx_hex(bio)),
                        field("listen_sprite_a", xbe_i32(x, r + 0x1A4)),
                        field("listen_sprite_b", xbe_i32(x, r + 0x1A8))}});
    }
    return write_table(t, file);
}

} // namespace

bool write_music_tables(Tables& tables) {
    return write_files(tables) && write_catalogue(tables);
}
