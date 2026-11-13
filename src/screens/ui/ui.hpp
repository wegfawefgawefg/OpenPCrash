#pragma once
// The front end's shared pieces: the 2D data, the texture slots texture_set_load fills, the 2D list
// every update writes (the original's overlay queues, emptied each frame by render_queues_reset),
// GenkiLib's task timer and the last-input mark the blinks count from, player 0's pad and the
// mouse pointer, and the screen fade.
#include "render/vk_sprites.hpp"
#include "screens/ui/menu_pointer.hpp"
#include "screens/ui/ui_anchors.hpp"
#include "screens/ui/ui_data.hpp"
#include "talk/draw2d.hpp"
#include "talk/sprite_registry.hpp"
#include "world/pad_record.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct Renderer;
struct Audio;

// a texture record as a sprite names it (the registry's rect pointer): texels on a slot
struct SpriteTex {
    int slot = -1;
    float rect[4] = {};  // left, top, right, bottom in texels
    float sheet[2] = {1, 1}; // the record's logical sheet size (+0x10, +0x14)
};

// Sprite (include/draw.h, 0x34 bytes) without its view
struct Sprite {
    SpriteTex tex;
    float pos[2] = {};
    float scale[2] = {1, 1};
    float rot[3] = {};
    int blend = 0x101;
    std::uint32_t color = 0xffffffff;
    float z = 1;
    std::uint32_t flags = 4;
};

// the screen fade (screen_fade_*, decomp src/od/u_09f1d0.c)
struct UiFade {
    int state = 0;   // 0 idle, 1 fading out, 2 black, 3 fading in
    float timer = 0; // ticks left
    float length = 1;
    std::uint32_t colour = 0;
};

// the texture slot movies are drawn from (movie_draw_rect: 0x225)
inline constexpr int movie_slot = 0x225;
// the font's slot (ascii_font_sys_tex, asset 1684)
inline constexpr int font_slot = 518;
// a registry sprite is its own cut-out texture in the cooked packs (docs/assets.md "Packs"), drawn
// from slot sprite_slot_base + its id; the original's slots below that keep the conversations' and
// the font's sheets (texture_set_load)
inline constexpr int sprite_slot_base = 0x10000;

struct Ui {
    UiData data;
    SpriteRegistry registry;
    SlotBindings slots; // what texture_set_load put in each slot
    std::map<std::string, SlotTexture> loaded; // cooked textures uploaded, by data path
    std::vector<SlotTexture> slot_textures;    // slots resolved for the renderer
    SlotTexture movie;                         // the movie picture (slot 0x225)
    SlotTexture capture; // the renderer's captured frame, slot 0x225 too when no movie plays
    std::uint64_t movie_serial = 0;            // the picture uploaded
    Renderer* renderer = nullptr;
    Audio* audio = nullptr;
    Draw2D draw;  // this update's 2D
    Draw2D shown; // that as the window shows it (moved by the anchors), handed to the renderer
    UiAnchors anchors;                         // the widescreen anchors (ui_anchors.hpp)
    const AnchorScreen* anchor_screen = nullptr; // the screen up now's
    int anchor_state = -1;                       // the front end's state they are for
    std::uint32_t task_timer = 0;                // task_timer: 1 a 60 Hz update
    std::uint32_t input_mark = 0; // D_00CC8F3C (menu_input_mark)
    int frame_cadence = 60;
    PadRecord pad; // player 0's record for this update
    std::array<PadRecord, max_pad_players> pads; // every seat's (the VS screen)
    std::uint32_t pc_held = 0; // player 0's PC actions held (input/bindings.hpp pc_stand_left ...)
    int pads_connected = 1; // func_000DFDA0: controllers in (the keyboard is the first)
    MenuPointer pointer;
    UiFade fade;
    std::uint32_t clear_colour = 0xff000000;
    // this update's 3D (the garage screens' model stand), drawn over the layers under 9
    FrameDraws scene;
    bool scene_drawn = false;
    // the screen adjust (GameState.screen_shift_x/y): the 3D viewports and every 2D pass but the
    // fade move by it, as retail's viewports do (render.md "viewports"; ui_present hands it to the
    // renderer)
    int screen_shift[2] = {0, 0};
    // the split-screen view the sprites submitted now belong to (Sprite.view: the HUD sets its
    // player's while it draws), -1 for none
    int view = -1;
};

struct PackLibrary;
bool init_ui(Ui& ui, Renderer& renderer, PackLibrary& packs, Audio* audio,
             const std::string& data_dir, std::string& error);
// the 2D list for the next update
void ui_begin_update(Ui& ui);
// the slots resolved (PNGs uploaded the first time) and the list handed to the renderer, moved by
// the screen's widescreen anchors
void ui_present(Ui& ui);
// the widescreen anchors of the front end's state (its screen's rules; the menus' hit boxes)
void ui_set_anchors(Ui& ui, int state);

// texture_set_load / texture_set_release (release keeps the slot: the original leaves the last
// set's pixels there)
void texture_set_load(Ui& ui, int asset, int first_slot = -1);

// menu_input_mark / menu_input_age
// decomp: menu_input_mark 0xE0560
inline void menu_input_mark(Ui& ui) {
    ui.input_mark = ui.task_timer;
}
// decomp: menu_input_age 0xE0550
inline int menu_input_age(const Ui& ui) {
    return static_cast<int>(ui.task_timer - ui.input_mark);
}

// draw_params_set_sprite: registry id's record, the defaults (layer 0x101, white, flags 4)
Sprite sprite_of(Ui& ui, int id);
// render_sprite_submit and render_command_submit_pass: blink, centring, the quad
void sprite_submit(Ui& ui, const Sprite& sprite);
// sprite_draw_clipped's cut: the part of a non-rotated sprite inside the rectangle
void sprite_submit_clipped(Ui& ui, const Sprite& sprite, float left, float top, float right,
                           float bottom);
// sprite_rows_draw: rows up to the one with sprite -1
void sprite_rows_draw(Ui& ui, const SpriteSlot* rows);
// SpriteRow::draw: one row
void sprite_row_draw(Ui& ui, const SpriteSlot& row);
// a sprite's rectangle on screen as drawn at x, y (centred with flag 8): the pointer's hit box
PointerRect sprite_rect(Ui& ui, int id, float x, float y, bool centred = false);
// render_registry_width / height
int sprite_width(Ui& ui, int id);
int sprite_height(Ui& ui, int id);
// a registry sprite's own cut-out as a renderer texture (uploaded once), for the 3D passes; -1 none
int sprite_texture(Ui& ui, int id);

// sound_start(bank, cue) for the menus' 2D cues
void ui_sound(Ui& ui, int bank, int cue);
// music_play / stream_stop_request / stream_current_get
void ui_music(Ui& ui, int track, bool loop);
void ui_music_stop(Ui& ui);
int ui_music_current(Ui& ui);

// the fade (screen_fade_out_start, _in_start, _is_idle, _is_black, screen_fade_frame)
bool fade_out_start(Ui& ui, float ticks, std::uint32_t colour = 0);
bool fade_in_start(Ui& ui, float ticks);
inline bool fade_is_idle(const Ui& ui) {
    return ui.fade.state == 0;
}
inline bool fade_is_black(const Ui& ui) {
    return ui.fade.state == 2;
}
// once an update after the tasks: draws the quad and counts down
void fade_frame(Ui& ui);
