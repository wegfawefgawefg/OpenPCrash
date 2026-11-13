#pragma once
// The Vulkan renderer: one device, a swapchain, and the original's frame (decomp docs/port/render.md)
// drawn with its own programs, ported as GLSL in assets/shaders/original/ (docs/shaders.md): the 3D
// scene into a scene target with a depth-stencil buffer, the pass 11 composite (blur and dof, or a
// tinted copy) into the final target, the stage's gamma ramp over that, then a blit to the window
// (and a readback for --capture). The game side builds the draws in pass order with every render
// state and per-draw constant block decided (src/render/mesh_draw.cpp); this side only runs them.
// Built on Mans In My House's renderer bootstrap.
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// a vertex as the game draws it: the original's 36-byte layout (FVF 0x152) with the colour as
// R G B A bytes
struct MeshVertex {
    float position[3];
    float normal[3];
    std::uint8_t colour[4];
    float uv[2];
};

// the original's programs (docs/shaders.md, "Program to port shader"), each a vertex and fragment
// pair with its specialization constants
enum class Program : std::uint8_t {
    fixed,              // fixed function 0x152: ff_mesh.vert, ff_mesh.frag
    fog,                // row 0x17: fog.vert, ff_mesh.frag
    point_lit,          // row 0x10: point.vert, point.frag
    point_lit_opaque,   // row 0x11: point.vert, point.frag (ALPHA_FROM_VERTEX)
    point_unlit,        // row 0x12: point.vert (UNLIT), point.frag
    point_unlit_opaque, // row 0x13: point.vert (UNLIT), point.frag (ALPHA_FROM_VERTEX)
    bump,               // row 2
    reflect,            // row 0x1A
    seabump,            // row 0x14
    fighter,            // row 4: multi_matrix_pl, the bone selector from a per-instance zero
    fighter_shadow,     // row 6: multi_shadow.vert, no fragment stage
    shadow_volume,      // pass 7: ff_mesh.vert, no fragment stage
    shadow_quad,        // shadow_screen_quad: screen_ff (SHADOW_QUAD)
    particle,           // row 0xE: particle.vert, premultiplied.frag; no mesh: index_count
                        // billboards from FrameDraws::storage's first_index (src/effects)
    fighter_camo,       // row 5: multi_optical.vert, blend.frag (pass 5, src/render/camo_draw.cpp)
    rainbow,            // row 0x19: rainbow.vert, rainbow.frag (mesh 0x100000, the shield bubble)
    effopt,             // row 0: effopt.vert, effopt.frag (pass 6, the contact decals)
    cockpit_uv,         // row 0x18: cockpit_uv.vert, ff_mesh.frag (pass 11, the instruments)
    count
};

// colour blending: none, SRC_ALPHA INV_SRC_ALPHA, SRC_ALPHA ONE, colour writes off (the shadow
// volumes' ZERO ONE), or ONE INV_SRC_ALPHA (premultiplied: particles and sprites)
enum class Blend : std::uint8_t { none, alpha, add, no_colour, premultiplied };
// D3D cull modes: back is CULL_CCW (the original's default), front is CULL_CW
enum class Cull : std::uint8_t { none, back, front };
// stencil setups: fighters zero the count they cover (pass 4); volumes count front faces up and
// back faces down with z on, the reverse with z off; the shadow quad draws where the count >= 1
enum class Stencil : std::uint8_t { off, zero_on_pass, volume, volume_inside, quad };

// texture addressing per axis, as the original's layer word picks it
enum class Address : std::uint8_t { Clamp, Wrap, Mirror };

// textures the renderer owns, usable on any stage
enum : int {
    texture_white = 0,     // the first upload, bound for untextured stages
    texture_scene = -2,    // the scene target 0x223, for the composite
    texture_backdrop = -3, // the backdrop 0x222: the scene as the first pass 5 or 6 draw found it
    texture_scene_last = -4, // 0x223 as the last frame with 3D left it: the big screens (0x80000)
    texture_none = -1,
};

struct StageTexture {
    int texture = texture_white;
    Address u = Address::Wrap;
    Address v = Address::Wrap;
    bool point = false; // point filtering, no mips
};

// DrawState (assets/shaders/original/include/draw_state.glsl), the push constants
enum DrawStateFlag : std::uint32_t {
    ds_fog = 0x1,
    ds_alpha_kill = 0x2,
    ds_alpha_test = 0x4, // the reference in bits 8..15
    ds_specular = 0x8,
    ds_lighting = 0x10,
};
struct DrawState {
    std::uint32_t flags = 0;
    std::uint32_t stages = 0; // ff_mesh.frag's stage ops, 8 bits a stage
    std::uint32_t factor = 0xFFFFFFFF;
    std::uint32_t texel = 0;
};

struct DrawItem {
    Program program = Program::fixed;
    int mesh = -1;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    Blend blend = Blend::alpha;
    bool depth_test = true;
    bool depth_write = true;
    Cull cull = Cull::back;
    Stencil stencil = Stencil::off;
    DrawState state;
    StageTexture stages[4];
    std::uint32_t uniform_offset = 0; // the program's per-draw block in FrameDraws::uniforms
    std::uint32_t uniform_size = 0;
    // render pass 13: after the 2D of pass 12, on a depth cleared for it, under passes 14 and 15
    // (the result screen's and the records screen's trophies); a plain mesh through the frame's
    // viewport
    bool late = false;
    // pass 11 (the cockpit instruments, hud_draw_meters): after the composite, before the 2D of
    // pass 12, on the final target untinted and unblurred; a plain mesh through the frame's
    // viewport
    bool cockpit = false;
    // pass_open_camo's copy: the scene so far into the backdrop (texture_backdrop) before this
    // draw; set on a view's first pass 5 or 6 draw (src/render/vk_backdrop.cpp)
    bool backdrop = false;
};

// a frame's draws, in order, and their per-draw blocks; storage is set 1 binding 1's bytes (the
// billboards, src/effects/billboards.hpp)
struct FrameDraws {
    std::vector<DrawItem> items;
    std::vector<std::uint8_t> uniforms;
    std::vector<std::uint8_t> storage;
};
// appends a block, 16-byte aligned; returns its offset
std::uint32_t push_uniform(FrameDraws& draws, const void* block, std::size_t size);

// a gamma ramp that changes nothing, a channel's 256 entries (frames outside a battle)
using GammaRamp = std::array<std::array<std::uint8_t, 256>, 3>;
constexpr GammaRamp identity_gamma_ramp() {
    GammaRamp ramp{};
    for (auto& channel : ramp) {
        for (int i = 0; i < 256; ++i) {
            channel[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
        }
    }
    return ramp;
}

// what the frame needs besides the draws
struct FrameSetup {
    float fog_colour[4] = {0, 0, 0, 0};
    float fog_params[4] = {0, 1, 0, 0}; // start, end, density, D3DFOGMODE
    float clear[4] = {0, 0, 0, 1};
    // the 3D viewport in the original's 640x480 screen: the safe rectangle 32, 16, 576, 448, or
    // the whole screen
    float viewport[4] = {0, 0, 640, 480};
    // split screen: several 3D views in one frame, each a run of the draws (first, count) through
    // its own rectangle, scissored to it (render.md "Views and cameras"); 0 views is one view over
    // every draw through `viewport`
    struct View {
        float viewport[4] = {0, 0, 640, 480};
        std::uint32_t first = 0, count = 0;
    };
    int view_count = 0;
    View views[4];
    // the one view's 3D scissored to where the 2D may reach (render/sprite_frame.hpp): retail's
    // title-safe area in a 4:3 box, the whole window once it is wide (the menus' model stands and
    // trophies; a fight's 3D fills the window at any shape)
    bool clip_to_2d = false;
    // the screen adjust, 640x480 screen pixels (Renderer::screen_shift, set by the draw)
    float screen_shift[2] = {0, 0};
    std::uint32_t shadow_colour = 0; // ARGB, the shadow quad
    // the pass 11 composite (screen_effect_dof in battle, else a tinted copy)
    bool dof = false;
    bool heat_wave = false;
    std::uint32_t tint = 0xFFFFFFFF; // scene_tint, ARGB
    float dof_threshold = 1;         // c2.a, 0.95 + 0.0005 x dof_level
    float bump_scale = 1, bump_shear = 0;
    float wave_phase = 0; // wave_texture_update's phase for the bound wave texture
    // the gamma ramp, 256 entries a channel (gamma_levels_set); identity outside a battle
    GammaRamp gamma = identity_gamma_ramp();
};

// the player's PC settings the renderer applies (docs/settings.md, src/screens/pc_settings.*)
struct RenderOptions {
    bool vsync = true;          // FIFO; off: mailbox, else immediate
    int render_scale = 100;     // the targets at this percent of the window (50..200)
    bool fit_43 = false;        // the 3D and the 2D in the 4:3 box with bars
    int anisotropy = 0;         // the meshes' texture filtering: 0 trilinear, else 2..16
    bool soft_focus = true;     // the battle composite's blur, dof and heat haze (the original's)
    bool antialias = false;     // FXAA over the 3D before the 2D (vk_record_post)
    int brightness = 100;       // percent: a power curve over the 3D alone, 100 changes nothing
    int flashes = 100;          // percent: the effects' lights, muzzle flash, flare, hit flicker
    int camera_shake = 100;     // percent: the cockpit's hit shake (src/view/cockpit_view.hpp)
    int hud_width = 100; // percent: how far edge-anchored 2D moves out of the 4:3 box (0 none)
    int hud_size = 100;  // percent: the battle HUD's pieces scaled about their corners (70..130)
};

struct RendererState; // Vulkan objects (vk_internal.hpp)

struct Renderer {
    RendererState* vk = nullptr;
    int width = 0; // the drawn target, in pixels
    int height = 0;
    RenderOptions options;
    // the screen adjust (GameState.screen_shift_x/y, Ui::screen_shift), 640x480 screen pixels:
    // every 3D viewport moves by it, as retail's (render.md "viewports"), with the 2D over it
    int screen_shift[2] = {0, 0};
};

bool renderer_init(Renderer& renderer, SDL_Window* window, std::string& error);
void renderer_shutdown(Renderer& renderer);
// new options: the swapchain and targets are made again on the next frame when the present mode
// or the scale changed, the samplers when the filtering did
void renderer_set_options(Renderer& renderer, const RenderOptions& options);
// the 3D viewport's aspect in the target's pixels for a rectangle of the 640x480 screen (the
// projection's), following fit_43
float renderer_scene_aspect(const Renderer& renderer, const float viewport[4]);
// handles for the draw items; -1 on failure
int renderer_upload_mesh(Renderer& renderer, const std::vector<MeshVertex>& vertices,
                         const std::vector<std::uint16_t>& indices);
// new vertices for a mesh already uploaded (as many or fewer): the stage's scrolling textures,
// fading lamps and the sky craft's screens rewrite theirs as the original does in place
bool renderer_update_mesh(Renderer& renderer, int mesh, const std::vector<MeshVertex>& vertices);
int renderer_upload_texture(Renderer& renderer, int width, int height,
                            const std::vector<std::uint8_t>& rgba);
// a cooked texture (src/cook/pack_format.hpp): BC blocks for each of `levels` mips, top level
// first, each level whole 4x4 blocks
enum class BlockFormat : std::uint8_t { bc1, bc3, bc7 };
int renderer_upload_blocks(Renderer& renderer, BlockFormat format, int width, int height,
                           int levels, const void* blocks, std::size_t size);
// a mesh or texture no longer wanted: its handle may come back from a later upload, its memory
// goes once the frame in flight is done. A draw naming a freed handle draws nothing (a mesh) or
// white (a texture). The white texture (0) is never freed
void renderer_free_mesh(Renderer& renderer, int mesh);
void renderer_free_texture(Renderer& renderer, int texture);
// uploads from now on belong to `owner` (0, the default, is kept for the run); freeing an owner
// frees every mesh and texture it uploaded (a battle's stage, machines and effects at its end)
inline constexpr int render_owner_battle = 1;
void renderer_set_owner(Renderer& renderer, int owner);
void renderer_free_owner(Renderer& renderer, int owner);
// what the meshes and textures hold now (the memory check between battles)
struct RendererMemory {
    int meshes = 0, textures = 0;
    std::uint64_t mesh_bytes = 0;
    std::uint64_t texture_texels = 0; // bytes at 4 a texel with mips, an upper bound
    int slots = 0;                    // handles ever made, freed ones included
};
RendererMemory renderer_memory(const Renderer& renderer);
// lens_flare_occlusion_test's depth read, a frame late: asks for the depth at a point of the
// original's 640x480 screen in the next frame drawn, and gives what the last frame read at the
// point asked before it (1 the cleared far, 0 before any read)
float renderer_depth_probe(Renderer& renderer, float x, float y);
// the capture texture 0x225 (frame_to_texture: render.md "Targets"), made on the first ask and
// kept for the run; while capture_frames is on every frame's final picture (2D included, before
// the gamma ramp) is copied into it, so turning it off keeps the last frame drawn
int renderer_capture_texture(Renderer& renderer);
void renderer_capture_frames(Renderer& renderer, bool on);
// draws the frame, presents, and writes it to capture_path when given
bool renderer_draw_frame(Renderer& renderer, const FrameSetup& setup, const FrameDraws& draws,
                         const std::string& capture_path);
