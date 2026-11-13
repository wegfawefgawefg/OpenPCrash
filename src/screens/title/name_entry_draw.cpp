// The name entry's drawing (name_entry_screen_draw, decomp src/od/u_02c4a0.c): the title,
// the grid's page and the page fading out, the three cursors, the name box and the name, the
// banner, the help line, day and money.
#include "screens/flow/front_end.hpp"
#include "world/world.hpp"

#include <cmath>

NameEntryLayout name_entry_layout(Ui& ui) {
    const UiTable* t = ui_table(ui.data, "name_entry", "name_entry_layout");
    return {t && t->rows.size() > 2 ? t->rows[2] : std::vector<double>()};
}

int name_entry_cell_glyph(Ui& ui, int page, int row, int col) {
    const UiTable* grid = ui_table(ui.data, "name_entry", "name_entry_grid");
    const std::size_t i = static_cast<std::size_t>(page * 90 + row * 15 + col);
    if (!grid || i >= grid->strings.size() || grid->strings[i].empty()) {
        return -1;
    }
    const GlyphText glyphs = utf8_to_glyphs(ui.data, grid->strings[i]);
    return glyphs.empty() ? -1 : glyphs[0];
}

// decomp: name_entry_screen_draw 0x2D94A
void name_entry_draw(FrontEnd& fe, Ui& ui, NameEntryScreen& n, bool cursors_lit) {
    const NameEntryLayout l = name_entry_layout(ui);
    const World& world = *fe.world;
    sprite_rows_draw(ui, ui_slots(ui.data, "name_entry", "name_entry_sprites"));
    sprite_draw_at(ui, 0x828, n.title_x, 107, 4);
    const std::uint32_t title = static_cast<std::uint32_t>(static_cast<int>(n.title_alpha * 255.0f))
                                    << 24 |
                                0xffffff;
    const struct {
        int id;
        float x, y;
    } pieces[7] = {{0xc39, 314, 115}, {0xc3b, 334, 117}, {0xc3a, 432, 115}, {0xc51, 450, 117},
                   {0xc35, 394, 139}, {0xc36, 420, 139}, {0xc42, 446, 141}};
    for (const auto& p : pieces) {
        sprite_draw_at(ui, p.id, p.x, p.y, 5, title);
    }
    const std::uint32_t grid = static_cast<std::uint32_t>(static_cast<int>(n.grid_alpha * 255.0f))
                                   << 24 |
                               0xffffff;
    sprite_draw_at(ui, 0x832, 64, 173, 2, grid);
    sprite_draw_at(ui, 0x833, 64, 331, 3, grid);
    if (fe.shop.name.kind == 1) {
        sprite_draw_at(ui, 0x834, 236, 331, 4, grid);
    }
    if (n.grid_alpha >= 1.0f) {
        // the page, and the page before it fading out in the same cells
        const auto alpha = [](float a) {
            return static_cast<std::uint32_t>(static_cast<int>(a * 255.0f)) << 24 | 0xc0c0c0;
        };
        for (int col = 0; col < 15; ++col) {
            for (int row = 0; row < 6; ++row) {
                const int x = col * static_cast<int>(l.at(32)) + 0x48;
                const int y = row * static_cast<int>(l.at(33)) + 0xb4;
                if (n.page != 2) {
                    const GlyphText cell{static_cast<std::int16_t>(name_entry_cell_glyph(ui, n.page, row, col)), -1};
                    text_draw(ui, cell, x, y, 0, 18, 18, alpha(n.page_fade), 2);
                }
                if (n.prev_page != 2 && n.prev_page != n.page) {
                    const GlyphText cell{static_cast<std::int16_t>(name_entry_cell_glyph(ui, n.prev_page, row, col)),
                                         -1};
                    text_draw(ui, cell, x, y, 0, 18, 18, alpha(1.0f - n.page_fade), 2);
                }
            }
        }
        // the three cursors (name_entry_cursor_sprites_update): grid, tab, caret
        const std::uint32_t pulse =
            static_cast<std::uint32_t>(static_cast<int>(std::sin(n.pulse) * 128.0f)) << 24 |
            0xffffff;
        const float offsets[3][2] = {
            {static_cast<float>(n.col) * static_cast<float>(l.at(32)),
             static_cast<float>(n.row) * static_cast<float>(l.at(33))},
            {static_cast<float>(n.tab) * static_cast<float>(l.at(34)),
             static_cast<float>(n.tab_row) * static_cast<float>(l.at(35))},
            {static_cast<float>(n.length == 8 ? 7 : n.length) * static_cast<float>(l.at(36)), 0}};
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t colour =
                k == 0 && (n.on_tabs || !cursors_lit) ? 0x00ffffff : cursors_lit ? pulse : 0x60ffffff;
            sprite_draw_at(ui, static_cast<int>(l.at(1 + 4 * k)),
                           static_cast<float>(l.at(2 + 4 * k)) + offsets[k][0],
                           static_cast<float>(l.at(3 + 4 * k)) + offsets[k][1],
                           static_cast<int>(l.at(4 + 4 * k)), colour,
                           static_cast<std::uint32_t>(l.at(13 + k)));
        }
    }
    float pop[2];
    box_pop_scale(pop, 5, 10, n.pop_step);
    Sprite box = sprite_of(ui, 0x82e);
    box.pos[0] = 171;
    box.pos[1] = 135;
    box.blend = 2;
    box.scale[0] = pop[0];
    box.scale[1] = pop[1];
    box.flags |= 8;
    sprite_submit(ui, box);
    if (n.pop_step == 10) {
        const int kind = fe.shop.name.kind;
        const std::size_t at = 16 + static_cast<std::size_t>(kind) * 4;
        sprite_draw_at(ui, static_cast<int>(l.at(at)), static_cast<float>(l.at(at + 1)),
                       static_cast<float>(l.at(at + 2)), static_cast<int>(l.at(at + 3)));
        // wide_text_draw: the name in 18 px cells
        const NameTarget& t = fe.shop.name;
        const std::int16_t* name = t.buffer ? t.buffer : t.scratch;
        for (int i = 0; i < n.length; ++i) {
            text_draw(ui, GlyphText{name[i], -1}, 0x5f + i * 18, 0x83, 0, 18, 18, 0xffffffff, 1);
        }
    }
    if (world.garages.selected != -1) {
        garage_banner_draw(ui, world, n.timer, 0x17a, 6);
    }
    text_strip_draw_scroll(ui, n.help[n.page], 0xffc0c0c0);
    day_draw(ui, world, 0x108);
    money_draw(ui, world, 0x108);
    level_meter_update(ui, fe.screen.meter);
    now_playing_update(ui, fe.screen.strip);
}
