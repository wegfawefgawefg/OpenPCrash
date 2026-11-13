#include "screens/ui/ui.hpp"

#include "audio/audio.hpp"
#include "render/renderer.hpp"

#include "cook/pack.hpp"
#include "game_paths.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

// the boot texture list D_00291200 (game_init): the font, common_2d, the two fight_chara sets
constexpr int boot_texture_sets[4] = {1684, 1685, 1636, 1637};
// the fade quad: sprite 0xE7C at (-32, -32) scaled 21 x 16 in pass 15 (screen_fade_quad_draw)
constexpr int fade_sprite = 0xe7c;

// a cooked texture by its data path: the BC blocks from its pack, uploaded once
SlotTexture pack_texture(Ui& ui, const std::string& path) {
    const auto found = ui.loaded.find(path);
    if (found != ui.loaded.end()) {
        return found->second;
    }
    SlotTexture out;
    const Pack* pack = ui_pack_for(ui.data, path);
    const PackEntry* entry = pack ? pack_find(*pack, PackKind::texture, path) : nullptr;
    if (entry) {
        const BlockFormat format = entry->format == TextureFormat::bc1   ? BlockFormat::bc1
                                   : entry->format == TextureFormat::bc3 ? BlockFormat::bc3
                                                                         : BlockFormat::bc7;
        out.texture = renderer_upload_blocks(
            *ui.renderer, format, static_cast<int>(entry->width), static_cast<int>(entry->height),
            static_cast<int>(entry->levels), pack_blob(*pack, *entry), entry->size);
        out.width = static_cast<int>(entry->width);
        out.height = static_cast<int>(entry->height);
    } else {
        std::fprintf(stderr, "ui: no texture %s in its pack\n", path.c_str());
    }
    ui.loaded[path] = out;
    return out;
}

void set_slot(Ui& ui, int slot, const SlotTexture& texture) {
    if (static_cast<int>(ui.slot_textures.size()) <= slot) {
        ui.slot_textures.resize(static_cast<std::size_t>(slot) + 1);
    }
    ui.slot_textures[static_cast<std::size_t>(slot)] = texture;
}

// the blink flags pulse z over 80 ticks from the last menu input; 0x100 and 0x200 force it
// (render_command_submit_pass)
float sprite_z(const Ui& ui, const Sprite& sprite) {
    if (sprite.flags & 0x6020) {
        const float period = 80.0f / static_cast<float>(60 / ui.frame_cadence);
        const unsigned wrap = static_cast<unsigned>(period) + 1;
        float z = std::fabs(
            static_cast<float>(static_cast<unsigned>(menu_input_age(ui)) % wrap) / period * 2.0f -
            1.0f);
        if (sprite.flags & 0x2000) {
            z = z * 0.7f + 0.3f;
        } else if ((sprite.flags & 0x4000) != 0) {
            z = z < 0.5f ? 0.0f : 1.0f;
        }
        return z;
    }
    if (sprite.flags & 0x100) {
        return 0.0f;
    }
    if (sprite.flags & 0x200) {
        return 1.0f;
    }
    return sprite.z;
}

// render_command_submit_pass's placement: the pivot at the middle, a centred sprite moved to its
// top left by half its scaled size (by the sheet's without flag 4, render_quad_scale)
Quad2D place(const Ui& ui, const Sprite& sprite) {
    Quad2D quad;
    quad.view = ui.view;
    quad.slot = sprite.tex.slot;
    quad.u0 = std::trunc(sprite.tex.rect[0]);
    quad.v0 = std::trunc(sprite.tex.rect[1]);
    quad.u1 = std::trunc(sprite.tex.rect[2]);
    quad.v1 = std::trunc(sprite.tex.rect[3]);
    quad.sheet_w = sprite.tex.sheet[0];
    quad.sheet_h = sprite.tex.sheet[1];
    const float w = sprite.tex.rect[2] - sprite.tex.rect[0];
    const float h = sprite.tex.rect[3] - sprite.tex.rect[1];
    const float half_w = w * sprite.scale[0] * 0.5f, half_h = h * sprite.scale[1] * 0.5f;
    quad.x = sprite.pos[0];
    quad.y = sprite.pos[1];
    if (sprite.flags & 8) {
        if (sprite.flags & 4) {
            quad.x -= half_w;
            quad.y -= half_h;
        } else {
            quad.x -= sprite.tex.sheet[0] * sprite.scale[0] * 0.5f;
            quad.y -= sprite.tex.sheet[1] * sprite.scale[1] * 0.5f;
        }
    }
    quad.w = (quad.u1 - quad.u0) * sprite.scale[0];
    quad.h = (quad.v1 - quad.v0) * sprite.scale[1];
    quad.rot[0] = sprite.rot[0] + half_w;
    quad.rot[1] = sprite.rot[1] + half_h;
    quad.rot[2] = sprite.rot[2];
    quad.color = sprite.color;
    quad.alpha = sprite_z(ui, sprite);
    quad.layer = sprite.blend;
    quad.flags = sprite.flags;
    return quad;
}

} // namespace

bool init_ui(Ui& ui, Renderer& renderer, PackLibrary& packs, Audio* audio,
             const std::string& data_dir, std::string& error) {
    ui.renderer = &renderer;
    ui.audio = audio;
    if (!load_ui_data(ui.data, packs, data_dir, error) ||
        !load_sprite_registry(ui.registry, data_dir, error)) {
        return false;
    }
    // the port's widescreen anchors (docs/screens.md "Widescreen"); without them all stays boxed.
    // PHANTOMCRASH_DEV_ANCHOR_FILE (port-only) reads another file, for trying rules
    std::string anchor_error;
    const char* anchor_file = std::getenv("PHANTOMCRASH_DEV_ANCHOR_FILE");
    const std::string anchors = anchor_file ? std::string(anchor_file)
                                            : asset_dir() + "/ui/anchors.sexp";
    if (!load_ui_anchors(ui.anchors, anchors, anchor_error)) {
        std::fprintf(stderr, "ui: anchors: %s\n", anchor_error.c_str());
    }
    for (int asset : boot_texture_sets) {
        texture_set_load(ui, asset);
    }
    return true;
}

void ui_begin_update(Ui& ui) {
    ui.draw.quads.clear();
    ui.scene.items.clear();
    ui.scene.uniforms.clear();
    ui.scene_drawn = false;
}

void ui_present(Ui& ui) {
    std::fill(ui.slot_textures.begin(), ui.slot_textures.end(), SlotTexture{});
    // the original's slots: the sheets texture_set_load put there (the conversations, the font)
    const std::string prefix = ui.data.data_dir + "/";
    for (const auto& [slot, path] : ui.slots.files) {
        if (slot >= 0 && slot < sprite_slot_base) {
            const bool under = path.compare(0, prefix.size(), prefix) == 0;
            set_slot(ui, slot, pack_texture(ui, under ? path.substr(prefix.size()) : path));
        }
    }
    // the registry sprites drawn this update, from their packs
    for (const Quad2D& quad : ui.draw.quads) {
        const int id = quad.slot - sprite_slot_base;
        if (id >= 0 && id < static_cast<int>(ui.data.image_file.size())) {
            set_slot(ui, quad.slot,
                     pack_texture(ui, ui.data.image_file[static_cast<std::size_t>(id)]));
        }
    }
    if (ui.movie.texture >= 0) {
        set_slot(ui, movie_slot, ui.movie);
    } else if (ui.capture.texture >= 0) {
        set_slot(ui, movie_slot, ui.capture); // frame_to_texture's 0x225
    }
    // the window's shape: every quad moved by its anchor, backgrounds extended (ui_anchors.hpp),
    // by the rules of the state's page drawn now
    ui.anchor_screen = ui_anchor_screen(ui.anchors, ui.anchor_state, &ui.draw);
    ui.pointer.anchors = ui.anchor_screen;
    anchors_dev_dump(ui.anchor_screen, *ui.renderer, ui.draw);
    Draw2D& shown = ui.shown;
    anchors_apply(ui.anchor_screen, *ui.renderer, ui.draw, shown);
    // the screen adjust: the 3D viewports move by it (the renderer's), every 2D pass with them but
    // pass 15 (the fade) and the full-window layers, which cover the glass
    ui.renderer->screen_shift[0] = ui.screen_shift[0];
    ui.renderer->screen_shift[1] = ui.screen_shift[1];
    if (ui.screen_shift[0] != 0 || ui.screen_shift[1] != 0) {
        for (Quad2D& quad : shown.quads) {
            if (overlay_pass(quad.layer) != 15 && !(quad.flags & quad_full_window)) {
                quad.x += static_cast<float>(ui.screen_shift[0]);
                quad.y += static_cast<float>(ui.screen_shift[1]);
            }
        }
    }
    renderer_set_sprites(*ui.renderer, shown, ui.slot_textures);
}

void ui_set_anchors(Ui& ui, int state) {
    // a new state starts with its plain rules; within one, the page drawn last picked them
    // (ui_present), which the screen's own updates (its scrolling lines) go on with
    if (state != ui.anchor_state || !ui.anchor_screen) {
        ui.anchor_state = state;
        ui.anchor_screen = ui_anchor_screen(ui.anchors, state);
    }
    ui.pointer.anchors = ui.anchor_screen;
    if (ui.renderer) {
        ui.pointer.frame = renderer_sprite_frame(*ui.renderer, -1);
    }
}

// decomp: texture_set_load 0xC709A
void texture_set_load(Ui& ui, int asset, int first_slot) {
    if (!bind_texture_set(ui.slots, ui.registry, asset, first_slot)) {
        std::fprintf(stderr, "ui: no texture set %d\n", asset);
    }
}

Sprite sprite_of(Ui& ui, int id) {
    Sprite sprite;
    if (id >= 0 && id < static_cast<int>(ui.registry.sprites.size())) {
        // its own cut-out: the rectangle's size, from its top left
        const RegistrySprite& entry = ui.registry.sprites[static_cast<std::size_t>(id)];
        const float w = entry.rect[2] - entry.rect[0];
        const float h = entry.rect[3] - entry.rect[1];
        sprite.tex.slot = sprite_slot_base + id;
        sprite.tex.rect[2] = w;
        sprite.tex.rect[3] = h;
        sprite.tex.sheet[0] = w;
        sprite.tex.sheet[1] = h;
    } else {
        sprite.tex.slot = -2; // render_sprite_submit draws nothing for an id out of range
    }
    return sprite;
}

// decomp: sprite_submit 0xBBB00
void sprite_submit(Ui& ui, const Sprite& sprite) {
    if (sprite.tex.slot == -2) {
        return;
    }
    submit_quad(ui.draw, place(ui, sprite));
}

void sprite_submit_clipped(Ui& ui, const Sprite& sprite, float left, float top, float right,
                           float bottom) {
    if (sprite.tex.slot == -2) {
        return;
    }
    Quad2D quad = place(ui, sprite);
    if (left >= right || top >= bottom) {
        left = 0, top = 0, right = 640, bottom = 480;
    }
    const float sx = sprite.scale[0] != 0 ? sprite.scale[0] : 1.0f;
    const float sy = sprite.scale[1] != 0 ? sprite.scale[1] : 1.0f;
    if (quad.x < left) {
        quad.u0 += (left - quad.x) / sx;
        quad.w -= left - quad.x;
        quad.x = left;
    }
    if (quad.y < top) {
        quad.v0 += (top - quad.y) / sy;
        quad.h -= top - quad.y;
        quad.y = top;
    }
    if (quad.x + quad.w > right) {
        quad.u1 -= (quad.x + quad.w - right) / sx;
        quad.w = right - quad.x;
    }
    if (quad.y + quad.h > bottom) {
        quad.v1 -= (quad.y + quad.h - bottom) / sy;
        quad.h = bottom - quad.y;
    }
    if (quad.w <= 0 || quad.h <= 0) {
        return;
    }
    submit_quad(ui.draw, quad);
}

// decomp: SpriteRow::draw 0xBBC45
void sprite_row_draw(Ui& ui, const SpriteSlot& row) {
    Sprite sprite = sprite_of(ui, row.sprite);
    sprite.pos[0] = row.x;
    sprite.pos[1] = row.y;
    sprite.blend = row.blend;
    sprite.color = row.color;
    sprite.z = row.z;
    sprite.flags |= row.flags;
    sprite_submit(ui, sprite);
}

// decomp: sprite_rows_draw 0xBBBBD
void sprite_rows_draw(Ui& ui, const SpriteSlot* rows) {
    if (!rows) {
        return;
    }
    for (; rows->sprite >= 0; ++rows) {
        sprite_row_draw(ui, *rows);
    }
}

PointerRect sprite_rect(Ui& ui, int id, float x, float y, bool centred) {
    const float w = static_cast<float>(sprite_width(ui, id));
    const float h = static_cast<float>(sprite_height(ui, id));
    if (centred) {
        x -= w * 0.5f;
        y -= h * 0.5f;
    }
    return {x, y, x + w, y + h, id};
}

int sprite_width(Ui& ui, int id) {
    const Sprite sprite = sprite_of(ui, id);
    return static_cast<int>(sprite.tex.rect[2] - sprite.tex.rect[0]);
}

int sprite_height(Ui& ui, int id) {
    const Sprite sprite = sprite_of(ui, id);
    return static_cast<int>(sprite.tex.rect[3] - sprite.tex.rect[1]);
}

void ui_sound(Ui& ui, int bank, int cue) {
    if (ui.audio && bank >= 0 && cue >= 0) {
        play_sound(*ui.audio, bank, cue);
    }
}

void ui_music(Ui& ui, int track, bool loop) {
    if (ui.audio) {
        play_music(*ui.audio, track, loop);
    }
}

void ui_music_stop(Ui& ui) {
    if (ui.audio) {
        stop_music(*ui.audio);
    }
}

int ui_music_current(Ui& ui) {
    return ui.audio ? music_track(*ui.audio) : -1;
}

bool fade_out_start(Ui& ui, float ticks, std::uint32_t colour) {
    if (ui.fade.state != 0) {
        return false;
    }
    ui.fade.length = ticks != 0.0f ? ticks : 1.0f;
    ui.fade.timer = ticks;
    ui.fade.colour = colour;
    ui.fade.state = 1;
    return true;
}

bool fade_in_start(Ui& ui, float ticks) {
    if (ui.fade.state != 2) {
        return false;
    }
    ui.fade.length = ticks != 0.0f ? ticks : 1.0f;
    ui.fade.timer = ticks;
    ui.fade.state = 3;
    return true;
}

void fade_frame(Ui& ui) {
    float alpha = -1.0f;
    switch (ui.fade.state) {
    case 1:
        alpha = 1.0f - ui.fade.timer / ui.fade.length;
        if (ui.fade.timer <= 0.0f) {
            ui.fade.state = 2;
        }
        break;
    case 2:
        alpha = 1.0f;
        break;
    case 3:
        alpha = ui.fade.timer / ui.fade.length;
        if (ui.fade.timer <= 0.0f) {
            ui.fade.state = 0;
        }
        break;
    default:
        break;
    }
    if (alpha >= 0.0f) {
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        Sprite sprite = sprite_of(ui, fade_sprite);
        sprite.pos[0] = -32.0f;
        sprite.pos[1] = -32.0f;
        sprite.blend = 0x7fff;
        // over the whole window: 22 x 17 where retail's 21 x 16 stops at the safe area's right and
        // bottom edges (608, 464), where its 3D ends; the port's goes on to the window's
        sprite.scale[0] = 22.0f;
        sprite.scale[1] = 17.0f;
        sprite.flags |= quad_full_window;
        // D_0017AAF0 is 255.0
        sprite.color = (ui.fade.colour & 0xffffff) |
                       static_cast<std::uint32_t>(static_cast<int>(255.0f * alpha)) << 24;
        sprite_submit(ui, sprite);
    }
    if (ui.fade.timer > 0.0f) {
        ui.fade.timer -= static_cast<float>(60 / ui.frame_cadence);
    }
}

int sprite_texture(Ui& ui, int id) {
    if (id < 0 || id >= static_cast<int>(ui.data.image_file.size()) || !ui.renderer) {
        return -1;
    }
    return pack_texture(ui, ui.data.image_file[static_cast<std::size_t>(id)]).texture;
}
